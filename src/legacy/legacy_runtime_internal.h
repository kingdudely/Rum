// Shared declarations for the split legacy runtime. Each name is
// defined once; the .cc files below only carry implementations.
#ifndef MOCKTAIL_LEGACY_LEGACY_RUNTIME_INTERNAL_H_
#define MOCKTAIL_LEGACY_LEGACY_RUNTIME_INTERNAL_H_

#include <jni.h>
#include <netdb.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <setjmp.h>
#include <signal.h>

#include "jnivm/jnivm.h"
#include "legacy/legacy_runtime.h"
#include "runtime/auth_runtime_composition.h"
#include "runtime/roblox_experience_launch_bridge.h"
#include "runtime/display_size.h"
#include "runtime/roblox_experience_composition.h"
#include "runtime/game_session_coordinator.h"
#include "runtime/roblox_fresh_game_launch_controller.h"
#include "runtime/roblox_game_session_native_adapter.h"
#include "mocktail/graphics/bionic_egl_bridge.h"
#include "mocktail/platform/display_refresh_capabilities.h"
#include "mocktail/platform/posix_primitives.h"

namespace legacy_internal {

constexpr sig_atomic_t kStage6RecoveryInactive = 0;
constexpr sig_atomic_t kStage6RecoveryInline = 1;

using JniOnLoadFn = jint (*)(JavaVM*, void*);
using NativeGameGlobalInitFn = void (*)(JNIEnv*, jclass);
using NativeInitClientSettingsFn = jint (*)(JNIEnv*, jclass, jstring, jstring,
                                            jstring);
using NativeInitClientSettingsSignedFn = jint (*)(JNIEnv*, jclass, jstring,
                                                  jstring, jstring, jstring);
using NativeInitClientSettingsCachedFn = jint (*)(JNIEnv*, jclass, jstring,
                                                  jstring, jstring, jstring,
                                                  jlong);
using NativeInitClientSettingsCachedCompressedFn =
    jint (*)(JNIEnv*, jclass, jbyteArray, jstring, jstring, jstring, jlong,
             jboolean);
using NativePostClientSettingsFn = void (*)(JNIEnv*, jclass, jobject);
using NativeInitializeNativeFlagsFn = jobject (*)(JNIEnv*, jclass,
                                                  jobjectArray);
using NativeAppBridgeAppStartFn = void (*)(JNIEnv*, jclass, jstring, jstring,
                                           jboolean, jstring, jstring,
                                           jstring);
using NativeSetIsFirstInstallFn = void (*)(JNIEnv*, jclass, jboolean);
using NativeAppBridgeObjectParamsFn = void (*)(JNIEnv*, jclass, jobject);
using NativeAppBridgeSetInitParamsFn = void (*)(JNIEnv*, jclass, jobject);
using NativeUpdateSurfaceAppFn = void (*)(JNIEnv*, jclass, jobject, jobject);
using NativeUpdateAppUiSizesFn = void (*)(JNIEnv*, jclass, jint, jint, jint,
                                          jint, jint);
using NativeUpdateScreenOrientationFn = void (*)(JNIEnv*, jclass, jint);
using NativeSendAppReadyFn = void (*)(JNIEnv*, jclass, jstring, jstring,
                                      jstring, jstring);
using NativeSendGameLoadedFn = void (*)(JNIEnv*, jclass, jstring, jstring,
                                        jstring);
using NativePassSupportedRefreshRatesFn = void (*)(JNIEnv*, jclass,
                                                   jfloatArray);
using NativePassCurrentDisplayRefreshRateFn = void (*)(JNIEnv*, jclass, jfloat);
using NativeSetStringParamFn = void (*)(JNIEnv*, jclass, jstring);
using NativeSetBaseUrlFn = void (*)(JNIEnv*, jclass, jstring, jstring);
using NativeSetTaskSchedulerBackgroundModeFn = void (*)(JNIEnv*, jclass,
                                                        jboolean, jstring);
using NativeObjectInitFn = void (*)(JNIEnv*, jclass, jobject);
// The engine's WebLoginProtocol cold-start entry: handed the raw launch URI,
// returns non-zero when it consumed a web-login ticket.
using NativeColdStartUriFn = jboolean (*)(JNIEnv*, jclass, jstring);
using NativeSetTwoStringParamsFn = void (*)(JNIEnv*, jclass, jstring, jstring);
using NativeSetThreeStringParamsFn = void (*)(JNIEnv*, jclass, jstring,
                                              jstring, jstring);
using NativeSetHttpClientProxyFn = void (*)(JNIEnv*, jclass, jstring, jlong);
using NativeInitStorageManagerFn = void (*)(JNIEnv*, jobject, jobject, jstring,
                                            jstring);
using NativeSetPlatformImplFn = jobject (*)(JNIEnv*, jclass, jobject);
using NativeActivityLifecycleStringFn = void (*)(JNIEnv*, jobject, jstring);
using NativeNoArgFn = void (*)(JNIEnv*, jclass);
using NativeDirectNoArgFn = void (*)();
using NativeGameActivityInitFn = jlong (*)(JNIEnv*, jobject, jstring, jstring,
                                           jstring, jobject, jbyteArray,
                                           jobject);
using GameActivityLifecycleFn = void (*)(JNIEnv*, jobject, jlong);
using GameActivitySurfaceCreatedFn = void (*)(JNIEnv*, jobject, jlong, jobject);
using GameActivitySurfaceChangedFn = void (*)(JNIEnv*, jobject, jlong, jobject,
                                              jint, jint, jint);

struct NativeActivityLifecycleCallbacks {
  NativeActivityLifecycleStringFn on_pre_created;
  NativeActivityLifecycleStringFn on_created;
  NativeActivityLifecycleStringFn on_post_created;
  NativeActivityLifecycleStringFn on_pre_started;
  NativeActivityLifecycleStringFn on_started;
  NativeActivityLifecycleStringFn on_post_started;
  NativeActivityLifecycleStringFn on_pre_resumed;
  NativeActivityLifecycleStringFn on_resumed;
  NativeActivityLifecycleStringFn on_post_resumed;
};

struct JniOnLoadAsyncContext {
  JniOnLoadFn fn;
  jint result = JNI_ERR;
  jnivm::VM* vm;
};


struct EngineStartupContext {
  jnivm::VM* vm;
  JavaVM* java_vm;
  // Raw roblox:// argument the engine's own protocols may consume (web-login
  // tickets). Empty when the launch was already normalized to a place.
  const std::string* raw_launch_uri;
  jnivm::RobloxAuthIdentity account_identity;
  const mocktail::runtime::SecureRobloxCredential* roblox_credential;
  mocktail::runtime::RobloxGameSessionRuntime* game_session_runtime;
  bool run_prepare_jni;
  bool run_set_asset_path;
  bool call_real_set_asset_path;
  bool run_global_init;
  bool run_init_client_settings;
  bool run_post_client_settings;
  bool run_app_bridge_app_start;
  bool run_native_settings;
  bool run_set_init_params;
  bool run_init_with_params;
  bool call_real_init_with_params;
  bool run_update_screen_orientation;
  bool run_start_app_with_params;
  bool call_real_start_app_with_params;
  bool run_game_activity_init;
  bool run_app_lifecycle_active;
  bool run_native_fragment_start;
  bool run_display_refresh_rate;
  bool run_start_lua_app_dm;
  NativeGameGlobalInitFn native_global_init;
  NativeInitClientSettingsFn native_init_client_settings;
  NativeInitClientSettingsSignedFn native_init_client_settings_signed;
  NativeInitClientSettingsCachedFn native_init_client_settings_cached;
  NativeInitClientSettingsCachedCompressedFn
      native_init_client_settings_cached_compressed;
  NativePostClientSettingsFn native_post_client_settings;
  NativeInitializeNativeFlagsFn native_initialize_native_flags;
  NativeAppBridgeAppStartFn native_app_bridge_app_start;
  NativeSetIsFirstInstallFn native_set_is_first_install;
  NativeSetBaseUrlFn native_set_base_url;
  NativeObjectInitFn native_set_device_info;
  NativeObjectInitFn native_base_url_protocol_init;
  NativeObjectInitFn native_web_login_protocol_init;
  NativeColdStartUriFn native_web_login_cold_start;
  NativeSetStringParamFn native_set_roblox_channel;
  NativeSetStringParamFn native_override_channel_platform_name;
  NativeSetStringParamFn native_set_roblox_version;
  NativeSetStringParamFn native_set_exception_reason_filename;
  NativeSetTwoStringParamsFn native_set_base_data_directories;
  NativeSetStringParamFn native_set_cache_directory;
  NativeSetStringParamFn native_set_files_directory;
  NativeSetStringParamFn native_set_external_directory;
  NativeSetStringParamFn native_set_preferences_file;
  NativeSetStringParamFn native_set_default_app_policy_file;
  NativeSetHttpClientProxyFn native_set_http_client_proxy;
  NativeNoArgFn native_init_fast_log;
  NativeSetTwoStringParamsFn native_set_multiple_cookies;
  NativeSetTwoStringParamsFn native_cookie_manager_set_cookie;
  NativeSetThreeStringParamsFn native_set_platform_headers_with_idfa;
  NativeSetStringParamFn native_set_user_id;
  NativeObjectInitFn native_init_asset_manager;
  NativeInitStorageManagerFn native_init_storage_manager;
  NativeSetPlatformImplFn native_local_storage_set_platform_impl;
  NativeAppBridgeSetInitParamsFn native_set_init_params;
  NativeNoArgFn native_retry_init;
  NativeAppBridgeObjectParamsFn native_init_with_params;
  NativeNoArgFn native_update_adapter_init;
  NativeUpdateScreenOrientationFn native_update_screen_orientation;
  NativeSetTaskSchedulerBackgroundModeFn
      native_set_task_scheduler_background_mode;
  NativeAppBridgeObjectParamsFn native_start_app_with_params;
  NativeSendAppReadyFn native_send_app_ready;
  NativeSetStringParamFn native_set_asset_path;
  NativeGameActivityInitFn native_game_activity_init;
  NativeNoArgFn native_app_lifecycle_set_active;
  NativeNoArgFn native_on_fragment_start;
  NativePassSupportedRefreshRatesFn native_pass_supported_refresh_rates;
  NativePassCurrentDisplayRefreshRateFn native_pass_current_display_refresh_rate;
  NativeNoArgFn native_start_lua_app_dm;
};

struct NativeSettingsValues {
  std::string data_dir;
  std::string files_dir;
  std::string settings_cache_dir;
  std::string cache_dir;
  std::string external_base;
  std::string external_dir;
  std::string preferences_file;
  std::string default_policy_file;
  std::string base_url;
  std::string api_url;
  std::string roblox_channel;
  std::string channel_platform_name;
  std::string roblox_version;
  std::string exception_reason_filename;
  std::string http_proxy_host;
  jlong http_proxy_port = 0;
  std::string cookie_base_url;
  std::string cookie_domain;
  const mocktail::runtime::SecureRobloxCredential* credential = nullptr;
  std::string_view roblox_cookies;
  std::string cookie_manager_cookies;
  std::string android_id;
  std::string advertising_id;
  std::string account_user_id;
};

struct RobloxEngineSymbols {
  NativeGameGlobalInitFn                       global_init = nullptr;
  NativeNoArgFn                                update_adapter_init = nullptr;
  NativeAppBridgeObjectParamsFn                init_with_params = nullptr;
  NativeNoArgFn                                start_lua_app_dm = nullptr;
  NativeAppBridgeObjectParamsFn                start_app_with_params = nullptr;
  NativeUpdateSurfaceAppFn                     update_surface_app = nullptr;
  NativeNoArgFn                                call_messages_from_main_thread = nullptr;
  NativeInitClientSettingsFn                   init_client_settings = nullptr;
  NativeInitClientSettingsSignedFn             init_client_settings_signed = nullptr;
  NativeInitClientSettingsCachedFn             init_client_settings_cached = nullptr;
  NativeInitClientSettingsCachedCompressedFn   init_client_settings_cached_compressed = nullptr;
  NativePostClientSettingsFn                   post_client_settings = nullptr;
  NativeInitializeNativeFlagsFn                initialize_native_flags = nullptr;
  NativeAppBridgeAppStartFn                    app_bridge_app_start = nullptr;
  NativeSetIsFirstInstallFn                    set_is_first_install = nullptr;
  NativeSetBaseUrlFn                           set_base_url = nullptr;
  NativeObjectInitFn                           set_device_info = nullptr;
  NativeObjectInitFn                           base_url_protocol_init = nullptr;
  NativeObjectInitFn                           web_login_protocol_init = nullptr;
  NativeColdStartUriFn                         web_login_cold_start = nullptr;
  NativeSetStringParamFn                       set_roblox_channel = nullptr;
  NativeSetStringParamFn                       override_channel_platform_name = nullptr;
  NativeSetStringParamFn                       set_roblox_version = nullptr;
  NativeSetStringParamFn                       set_exception_reason_filename = nullptr;
  NativeSetTwoStringParamsFn                   set_base_data_directories = nullptr;
  NativeSetStringParamFn                       set_cache_directory = nullptr;
  NativeSetStringParamFn                       set_files_directory = nullptr;
  NativeSetStringParamFn                       set_external_directory = nullptr;
  NativeSetStringParamFn                       set_preferences_file = nullptr;
  NativeSetStringParamFn                       set_default_app_policy_file = nullptr;
  NativeSetHttpClientProxyFn                   set_http_client_proxy = nullptr;
  NativeNoArgFn                                init_fast_log = nullptr;
  NativeSetTwoStringParamsFn                   set_multiple_cookies = nullptr;
  jnivm::RobloxCookieGetter                    get_cookies_for_domain = nullptr;
  NativeSetTwoStringParamsFn                   cookie_manager_set_cookie = nullptr;
  NativeSetThreeStringParamsFn                 set_platform_headers_with_idfa = nullptr;
  NativeSetStringParamFn                       set_user_id = nullptr;
  NativeObjectInitFn                           init_asset_manager = nullptr;
  NativeInitStorageManagerFn                   init_storage_manager = nullptr;
  NativeSetPlatformImplFn                      local_storage_set_platform_impl = nullptr;
  NativeAppBridgeSetInitParamsFn               set_init_params = nullptr;
  NativeNoArgFn                                retry_init = nullptr;
  NativeUpdateScreenOrientationFn              update_screen_orientation = nullptr;
  NativeUpdateAppUiSizesFn                     update_app_ui_sizes = nullptr;
  NativeNoArgFn                                on_fragment_start = nullptr;
  NativePassCurrentDisplayRefreshRateFn        pass_current_display_refresh_rate = nullptr;
  NativePassSupportedRefreshRatesFn            pass_supported_refresh_rates = nullptr;
  NativeSetTaskSchedulerBackgroundModeFn       set_task_scheduler_background_mode = nullptr;
  NativeSendAppReadyFn                         send_app_ready = nullptr;
  NativeSendGameLoadedFn                       send_game_loaded = nullptr;
  NativeSetStringParamFn                       set_asset_path = nullptr;
  NativeActivityLifecycleCallbacks             activity_lifecycle = {};
  NativeGameActivityInitFn                     game_activity_init = nullptr;
  NativeNoArgFn                                app_lifecycle_set_active = nullptr;
};

struct StartupDecisions {
  bool run_set_asset_path = false;
  bool call_real_set_asset_path = false;
  bool run_global_init = false;
  bool run_init_client_settings = false;
  bool run_post_client_settings = false;
  bool run_app_bridge_app_start = false;
  bool run_init_with_params = false;
  bool call_real_init_with_params = false;
  bool run_update_screen_orientation = false;
  bool run_game_activity_init = false;
  bool run_set_init_params = false;
  bool run_app_lifecycle_active = false;
  bool run_native_fragment_start = false;
  bool run_display_refresh_rate = false;
  bool run_start_app_with_params = false;
  bool call_real_start_app_with_params = false;
  bool run_start_lua_app_dm = false;
  bool run_native_settings = false;
  bool run_prepare_jni = false;
};

struct GlSymbolStats {
  int registered = 0;
  int total_symbols = 0;
  int gl_symbol_count = 0;
  int gl_from_window = 0;
  int gl_from_real_gles = 0;
  int gl_from_stub = 0;
  int gl_from_host = 0;
  int gl_unresolved = 0;

  // Folds another pass's GL breakdown in. registered/total_symbols stay
  // per-pass, because each pass reports its own total on its own log line.
  void MergeGlResolution(const GlSymbolStats& other) {
    gl_symbol_count += other.gl_symbol_count;
    gl_from_window += other.gl_from_window;
    gl_from_real_gles += other.gl_from_real_gles;
    gl_from_stub += other.gl_from_stub;
    gl_from_host += other.gl_from_host;
    gl_unresolved += other.gl_unresolved;
  }
};

struct BionicStubPreload {
  std::vector<const char*> names;
  std::vector<void*> handles;
  void* vulkan_adapter = nullptr;
};

struct ExperienceLifecycleTarget {
  std::weak_ptr<mocktail::runtime::RobloxExperienceComposition> composition;
};

extern volatile uintptr_t g_stage6_jni_env;
extern volatile uintptr_t g_libroblox_base;
extern volatile uintptr_t g_game_activity_native_handle;
extern jobject g_saved_game_activity;
extern volatile sig_atomic_t g_jni_onload_in_progress;
extern volatile sig_atomic_t g_jni_onload_timings_printed;
extern volatile sig_atomic_t g_jni_onload_soft_timeout;
extern volatile sig_atomic_t g_jni_onload_jmp_armed;
extern thread_local sigjmp_buf g_jni_onload_jmp_buf;
extern thread_local sigjmp_buf g_init_with_params_jmp_buf;
extern thread_local volatile sig_atomic_t g_init_with_params_recovery_in_progress;
extern thread_local sigjmp_buf g_start_app_with_params_jmp_buf;
extern thread_local volatile sig_atomic_t g_start_app_with_params_recovery_in_progress;
extern thread_local sigjmp_buf g_start_lua_app_dm_jmp_buf;
extern thread_local volatile sig_atomic_t g_start_lua_app_dm_recovery_in_progress;
extern thread_local sigjmp_buf g_send_app_ready_jmp_buf;
extern thread_local volatile sig_atomic_t g_send_app_ready_recovery_in_progress;
extern thread_local sigjmp_buf g_set_asset_path_jmp_buf;
extern thread_local volatile sig_atomic_t g_set_asset_path_recovery_in_progress;
extern thread_local sigjmp_buf g_game_global_init_jmp_buf;
extern thread_local volatile sig_atomic_t g_game_global_init_recovery_in_progress;
extern thread_local sigjmp_buf g_init_client_settings_jmp_buf;
extern thread_local volatile sig_atomic_t g_init_client_settings_recovery_in_progress;
extern thread_local sigjmp_buf g_post_client_settings_jmp_buf;
extern thread_local volatile sig_atomic_t g_post_client_settings_recovery_in_progress;
extern thread_local sigjmp_buf g_initialize_native_flags_jmp_buf;
extern thread_local sigjmp_buf g_cookie_setter_jmp_buf;
extern thread_local volatile sig_atomic_t g_cookie_setter_recovery_in_progress;
extern thread_local sigjmp_buf g_native_settings_jmp_buf;
extern thread_local volatile sig_atomic_t g_native_settings_recovery_in_progress;
extern thread_local const char* g_native_settings_recovery_name;
extern thread_local sigjmp_buf g_app_bridge_app_start_jmp_buf;
extern thread_local volatile sig_atomic_t g_app_bridge_app_start_recovery_in_progress;
extern thread_local volatile sig_atomic_t g_game_activity_init_recovery_in_progress;
extern thread_local sigjmp_buf g_game_activity_init_jmp_buf;
extern thread_local sigjmp_buf g_update_screen_orientation_jmp_buf;
extern thread_local sigjmp_buf g_update_surface_app_jmp_buf;
extern thread_local volatile sig_atomic_t g_update_surface_app_recovery_in_progress;
extern thread_local sigjmp_buf g_native_fragment_start_jmp_buf;
extern thread_local sigjmp_buf g_display_refresh_rate_jmp_buf;
extern thread_local volatile sig_atomic_t g_display_refresh_rate_recovery_in_progress;
extern std::atomic<int> g_main_thread_message_pump_ready;
extern std::atomic<int> g_pending_main_thread_task_scheduler_state;
extern std::atomic<int> g_pending_main_thread_task_scheduler_recovered;
extern thread_local volatile sig_atomic_t g_initialize_native_flags_recovery_in_progress;
extern thread_local volatile sig_atomic_t g_native_fragment_start_recovery_in_progress;
extern thread_local volatile sig_atomic_t g_update_screen_orientation_recovery_in_progress;
extern NativeNoArgFn g_native_call_messages_from_main_thread;
extern NativeNoArgFn g_pending_main_thread_start_lua_app_dm;
extern jclass g_native_gl_class_for_main_thread;
extern jnivm::VM* g_vm_for_main_thread_pump;
extern uint64_t g_pending_main_thread_start_lua_due_ms;
extern bool g_pending_main_thread_start_lua_started;
extern jclass g_pending_main_thread_task_scheduler_class;

// from legacy_jni_builders.cc
void SetObjectField(JNIEnv* env, jobject object, const char* name,
                    const char* signature, jobject value);
void SetStringField(JNIEnv* env, jobject object, const char* name,
                    const char* value);
void SetJStringField(JNIEnv* env, jobject object, const char* name,
                    jstring value);
void SetIntField(JNIEnv* env, jobject object, const char* name, jint value);
void SetLongField(JNIEnv* env, jobject object, const char* name, jlong value);
void SetFloatField(JNIEnv* env, jobject object, const char* name,
                   jfloat value);
void SetBooleanField(JNIEnv* env, jobject object, const char* name,
                     jboolean value);
void SetRobloxServiceUrlFields(JNIEnv* env, jobject object);
jobject NewObject(JNIEnv* env, const char* class_name);
void InstallNativeGlJavaImplementation(JNIEnv* env, jclass native_gl_java_class);
mocktail::runtime::DisplaySize HostDisplaySize();
mocktail::runtime::DisplaySize HostWindowPixelSize();
jobject BuildDeviceParams(JNIEnv* env);
jobject BuildApplicationExitInfoList(JNIEnv* env);
jobject BuildPlatformParams(JNIEnv* env, jobject surface);
jobject BuildMockSurface(JNIEnv* env);
jobject BuildConfiguration(JNIEnv* env);
JNIEnv* EnsureStartupEnv(EngineStartupContext* context, JNIEnv*& env);
// from legacy_native_settings.cc
NativeSettingsValues ResolveNativeSettingsValues(
    const EngineStartupContext* context);
void EnsureNativeSettingsDirectories(const NativeSettingsValues& values);
void ConfigureNativeSettings(JNIEnv* env, jclass settings_class,
                             const EngineStartupContext* context);
void ConfigureLocalStorage(JNIEnv* env, const EngineStartupContext* context);
void ReportDisplayRefreshRates(EngineStartupContext* context, JNIEnv*& env,
                            jclass native_gl_class);
std::string CookieForJNICookieManager(std::string_view cookie_header,
                                      const std::string& domain);
// from legacy_engine_symbols.cc
enum class SymbolResolveSource {
  kMissing = 0,
  kWindow = 1,
  kRealGles = 2,
  kStub = 3,
  kHost = 4,
};

struct SymbolResolveResult {
  void* address = nullptr;
  SymbolResolveSource source = SymbolResolveSource::kMissing;
};

SymbolResolveResult ResolveSymbolForBionic(const char* name, bool has_window,
                                          void* real_gles_handle,
                                          const std::vector<void*>& stub_handles);
bool StubOwnsSymbolAddress(void* handle, void* address);
bool IsGlSymbol(const char* name);
void* OpenRealGlesLibrary();
bool IsMocktailStubGlesPath(const char* path);
void ReportResolvedEngineSymbols(const RobloxEngineSymbols& symbols);
RobloxEngineSymbols ResolveRobloxEngineSymbols(
    void* roblox_handle,
    const mocktail::runtime::RobloxStartupSymbols& startup_symbols,
    jnivm::VM* jni_vm);
void TallyGlSymbol(const char* name, SymbolResolveSource source, void* address,
                   GlSymbolStats* stats);
BionicStubPreload PreloadBionicStubs(
    const mocktail::graphics::BionicEglBridge& bionic_egl_bridge);
GlSymbolStats RegisterEngineSymbols(bool has_window,
                                     void* real_gles_handle,
                                     const std::vector<void*>& stub_handles);
void RegisterVulkanAdapterExports(void* bionic_vulkan_adapter_handle);
GlSymbolStats RegisterUndefinedElfImports(const std::string& library_path,
                                          bool has_window,
                                          void* real_gles_handle,
                                          const std::vector<void*>& stub_handles);
bool StartupSymbolsMissing(const RobloxEngineSymbols& symbols,
                           void* message_bus_publish_response_raw);
struct EngineSymbolDump;


// shared helpers (defined in legacy_env.cc)
void* ResolveRobloxCapabilitySymbol(void* context, const char* symbol_name);
void RestoreGameSessionJniEnvironment(void* context);
void* RunJniOnLoadWorker(void* arg);
const char* CachedGetenv(const char* name);
bool IsEnabled(const char* name);
bool IsDisabled(const char* name);
bool StartsWith(const char* value, const char* prefix);
bool TraceAllEnabled();
void SetEnvDefault(const char* name, const char* value);
void ApplyRuntimeDefaults();
int GetEnvInt(const char* name, int default_value);
jlong GetEnvLong(const char* name, jlong default_value);
uint64_t MonotonicMillis();
uint64_t MonotonicNanos();
JNIEnv* AttachMainThreadJniEnv();
bool InvokeTaskSchedulerForeground(
    JNIEnv* env, jclass native_gl_class,
    NativeSetTaskSchedulerBackgroundModeFn native_set_task_scheduler_background_mode,
    const char* log_scope);
void RunPendingMainThreadTaskSchedulerForeground();
bool RunTaskSchedulerForegroundOnMainThread(
    NativeSetTaskSchedulerBackgroundModeFn native_set_task_scheduler_background_mode,
    jclass native_gl_class);
void PumpRobloxMainThreadMessagesOnce();
void PumpStartupOwnerThread(void* /*context*/);
std::string GetEnvString(const char* name, const char* default_value);
bool HasEnvValue(const char* name);
bool DnsTraceEnabled();
struct BionicAddrInfo {
  int ai_flags;
  int ai_family;
  int ai_socktype;
  int ai_protocol;
  socklen_t ai_addrlen;
  char* ai_canonname;
  struct sockaddr* ai_addr;
  BionicAddrInfo* ai_next;
};

addrinfo HostHintsFromBionic(const BionicAddrInfo* hints);
BionicAddrInfo* BionicAddrInfoFromHost(const addrinfo* host);
void RegisterBionicDnsWrappers();
void RegisterBionicPathWrappers();
mocktail::runtime::RuntimePaths CurrentRuntimePaths();
std::string MocktailCacheRoot();
std::string MocktailConfigRoot();
std::string DefaultAssetPath();
bool RegisterFreshGamePresentObserver(
    void* context, mocktail::runtime::FreshLaunchPresentObserver observer,
    void* observer_context);
void ClearFreshGamePresentObserver(void* context);
jobject CreateExperienceRawCallback(
    void* context, std::shared_ptr<void> callback_context,
    void (*run)(void*, JNIEnv*, jstring));
void ClearExperienceRawCallback(void* context, jobject callback);
jobject CreateAsyncMessageBusRequestHandler(
    void* context, std::shared_ptr<void> callback_context,
    void (*run)(void*, JNIEnv*, jstring, jstring));
void ClearAsyncMessageBusRequestHandler(void* context, jobject handler);
void NotifyLuaAppDidReturn(void* context);
mocktail::runtime::GameSessionUpdateResult ExperienceSurfaceCreated(
    void* context, uint64_t generation);
mocktail::runtime::GameSessionUpdateResult ExperienceSurfaceChanged(
    void* context, mocktail::runtime::GameSurface surface);
mocktail::runtime::GameSessionUpdateResult ExperienceSurfaceDestroyed(
    void* context, uint64_t generation);
std::string GetEnvStringDefaultPath(const char* name,
                                    const std::string& default_value);
bool EnsureDirectory(const std::string& path);
void EnsureAndroidDirectory(const std::string& android_path);
size_t GetEngineStackSize();
void PrintStage(int stage, const char* description);
bool IsUnsafeSoftTimeoutModule(void* rip);
void JniOnLoadTimeoutAlarm(int, siginfo_t* info, void* context);
void PublishCurrentJniEnv(JNIEnv* env);
bool EngineTraceEnabled();
void PreloadPthreadSymbols();
void EngineLog(const char* message);
void EngineLogPtr(const char* name, const void* ptr);
void PrintBacktraceNoSig(const char* prefix);
void PrintContextBacktrace(ucontext_t* ucontext, const char* prefix);
std::string JStringToString(JNIEnv* env, jstring value);
void MocktailSetAssetPath(JNIEnv* env, jstring asset_path);
void MocktailAppBridgeInit(JNIEnv* env, jstring app_params);
void MocktailAppBridgeStart(JNIEnv*, jstring);
jstring NewStringFromEnvDefault(JNIEnv* env, const char* env_name,
                                const char* default_value);
void ApplyAuthStartupDefaults(bool credential_available,
                              bool user_overrode_start_lua_app_dm,
                              bool user_overrode_start_lua_step,
                              bool user_overrode_start_app_step,
                              bool user_overrode_call_start_app);
std::string ResolveClientSettingsJson();
jstring NewClientSettingsString(JNIEnv* env);
jobject BuildAppBridgeInitParams(JNIEnv* env, jstring client_settings,
                                 jstring fast_flags, jstring app_params,
                                 jstring asset_path);
jobject BuildStartGameParams(JNIEnv* env, jobject platform_params,
                            jobject device_params, jobject surface,
                            jobject vr_context, const char* launch_data_env,
                            const jnivm::RobloxAuthIdentity& identity);

// C entry points defined in legacy_env.cc for the guest linker.
extern "C" {
int mocktail_mprotect(void* addr, size_t len, int prot);
int mocktail_recover_stack_chk_fail();
}  // extern "C"

extern "C" void* mocktail_gameactivity_on_trim_memory_native;

}  // namespace legacy_internal

#endif  // MOCKTAIL_LEGACY_LEGACY_RUNTIME_INTERNAL_H_
