#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <iomanip>
#include <iostream>
#include <csignal>
#include <netdb.h>
#include <memory>
#include <mutex>
#include <new>
#include <poll.h>
#include <unordered_map>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <string>
#include <string_view>
#include <optional>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <execinfo.h>
#include <ucontext.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/prctl.h>
#if defined(__x86_64__)
#include <asm/prctl.h>
#endif
#include <sys/syscall.h>
#include <vector>

#include <jni.h>
#include <SDL3/SDL_video.h>

#include "compat/bionic_abi_exports.h"
#include "compat/bionic_prctl_runtime.h"
#include "compat/bionic_pthread_create_runtime.h"
#include "compat/bionic_socket_runtime.h"
#include "compat/guest_abi.h"
#include "jnivm/jnivm.h"
#include "legacy/legacy_runtime.h"
#include "libc_shim/libc_shim.h"
#include "linker/linker.h"
#include "mocktail/graphics/bionic_egl_bridge.h"
#include "mocktail/platform/posix_primitives.h"
#include "runtime/device_memory_profile.h"
#include "runtime/display_size.h"
#include "runtime/environment.h"
#include "runtime/jnivm_platform_web_callbacks.h"
#include "runtime/owned_pthread.h"
#include "runtime/platform_cache_migration.h"
#include "runtime/roblox_app_lifecycle.h"
#include "runtime/roblox_capability_resolver.h"
#include "runtime/roblox_platform_protocol_symbols.h"
#include "runtime/roblox_experience_composition.h"
#include "runtime/roblox_game_session_native_adapter.h"
#include "runtime/roblox_window_input_runtime.h"
#include "runtime/roblox_text_input_jni_bridge.h"
#include "runtime/runtime_config.h"
#include "runtime/runtime_paths.h"
#include "runtime/texture_memory_policy.h"
#include "services/client_settings_service.h"
#include "services/http_client.h"
#include "window/window.h"
#include "window/window_game_surface_bridge.h"

#ifdef MOCKTAIL_USE_BIONIC_LINKER
#include <mcpelauncher/linker.h>
#endif

// Private implementation helpers for the legacy runtime. Named rather than
// anonymous because the startup/window/protocol translation units in this
// directory call into it; see legacy_runtime_internal.h for the subset each
// one is allowed to see. Nothing outside src/legacy should include that header.
namespace legacy_internal {

void* ResolveRobloxCapabilitySymbol(void* context, const char* symbol_name) {
  if (context == nullptr || symbol_name == nullptr) {
    return nullptr;
  }
  auto* handle = static_cast<linker::LibraryHandle*>(context);
  return linker::ResolveSymbol(*handle, symbol_name);
}

void RestoreGameSessionJniEnvironment(void* context) {
  auto* vm = static_cast<jnivm::VM*>(context);
  if (vm != nullptr) {
    vm->RestoreFunctions();
  }
}

volatile uintptr_t g_stage6_jni_env = 0;
volatile uintptr_t g_libroblox_base = 0;
volatile uintptr_t g_game_activity_native_handle = 0;
jobject g_saved_game_activity = nullptr;
extern "C" void* mocktail_gameactivity_on_trim_memory_native;
volatile sig_atomic_t g_jni_onload_in_progress = 0;
volatile sig_atomic_t g_jni_onload_timings_printed = 0;
volatile sig_atomic_t g_jni_onload_soft_timeout = 0;
volatile sig_atomic_t g_jni_onload_jmp_armed = 0;
thread_local sigjmp_buf g_jni_onload_jmp_buf;
thread_local sigjmp_buf g_init_with_params_jmp_buf;
thread_local volatile sig_atomic_t g_init_with_params_recovery_in_progress = 0;
thread_local sigjmp_buf g_start_app_with_params_jmp_buf;
thread_local volatile sig_atomic_t g_start_app_with_params_recovery_in_progress = 0;
thread_local sigjmp_buf g_start_lua_app_dm_jmp_buf;
thread_local volatile sig_atomic_t g_start_lua_app_dm_recovery_in_progress = 0;
thread_local sigjmp_buf g_send_app_ready_jmp_buf;
thread_local volatile sig_atomic_t g_send_app_ready_recovery_in_progress = 0;
thread_local sigjmp_buf g_set_asset_path_jmp_buf;
thread_local volatile sig_atomic_t g_set_asset_path_recovery_in_progress = 0;
thread_local sigjmp_buf g_game_global_init_jmp_buf;
thread_local volatile sig_atomic_t g_game_global_init_recovery_in_progress = 0;
thread_local sigjmp_buf g_init_client_settings_jmp_buf;
thread_local volatile sig_atomic_t g_init_client_settings_recovery_in_progress = 0;
thread_local sigjmp_buf g_post_client_settings_jmp_buf;
thread_local volatile sig_atomic_t g_post_client_settings_recovery_in_progress = 0;
thread_local sigjmp_buf g_initialize_native_flags_jmp_buf;
thread_local volatile sig_atomic_t
    g_initialize_native_flags_recovery_in_progress = 0;
thread_local sigjmp_buf g_cookie_setter_jmp_buf;
thread_local volatile sig_atomic_t g_cookie_setter_recovery_in_progress = 0;
thread_local sigjmp_buf g_native_settings_jmp_buf;
thread_local volatile sig_atomic_t g_native_settings_recovery_in_progress = 0;
thread_local const char* g_native_settings_recovery_name = nullptr;
thread_local sigjmp_buf g_app_bridge_app_start_jmp_buf;
thread_local volatile sig_atomic_t g_app_bridge_app_start_recovery_in_progress =
    0;
thread_local sigjmp_buf g_game_activity_init_jmp_buf;
thread_local volatile sig_atomic_t g_game_activity_init_recovery_in_progress =
    0;
thread_local sigjmp_buf g_update_screen_orientation_jmp_buf;
thread_local volatile sig_atomic_t
    g_update_screen_orientation_recovery_in_progress = 0;
thread_local sigjmp_buf g_update_surface_app_jmp_buf;
thread_local volatile sig_atomic_t g_update_surface_app_recovery_in_progress = 0;
thread_local sigjmp_buf g_native_fragment_start_jmp_buf;
thread_local volatile sig_atomic_t
    g_native_fragment_start_recovery_in_progress = 0;
thread_local sigjmp_buf g_display_refresh_rate_jmp_buf;
thread_local volatile sig_atomic_t
    g_display_refresh_rate_recovery_in_progress = 0;
constexpr sig_atomic_t kStage6RecoveryInactive = 0;
constexpr sig_atomic_t kStage6RecoveryInline = 1;

void PrintBacktraceNoSig(const char* prefix);
void PrintContextBacktrace(ucontext_t* ucontext, const char* prefix);

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

NativeNoArgFn g_native_call_messages_from_main_thread = nullptr;
NativeNoArgFn g_pending_main_thread_start_lua_app_dm = nullptr;
jclass g_native_gl_class_for_main_thread = nullptr;
jnivm::VM* g_vm_for_main_thread_pump = nullptr;
std::atomic<int> g_main_thread_message_pump_ready{0};
uint64_t g_pending_main_thread_start_lua_due_ms = 0;
bool g_pending_main_thread_start_lua_started = false;
NativeSetTaskSchedulerBackgroundModeFn
    g_pending_main_thread_task_scheduler_background_mode = nullptr;
jclass g_pending_main_thread_task_scheduler_class = nullptr;
std::atomic<int> g_pending_main_thread_task_scheduler_state{0};
std::atomic<int> g_pending_main_thread_task_scheduler_recovered{0};
constexpr int kMainThreadTaskSchedulerIdle = 0;
constexpr int kMainThreadTaskSchedulerPending = 1;
constexpr int kMainThreadTaskSchedulerRunning = 2;
constexpr int kMainThreadTaskSchedulerComplete = 3;
constexpr int kMainThreadTaskSchedulerTimedOut = 4;

void PublishCurrentJniEnv(JNIEnv* env);

struct JniOnLoadAsyncContext {
  JniOnLoadFn fn;
  jint result = JNI_ERR;
  jnivm::VM* vm;
};

void* RunJniOnLoadWorker(void* arg) {
  auto* context = static_cast<JniOnLoadAsyncContext*>(arg);
  if (context == nullptr || context->vm == nullptr) {
    return nullptr;
  }
  auto context_class = context->vm->RegisterClass("android/content/Context");
  auto activity_class =
      context->vm->RegisterClass("com/roblox/client/RobloxActivity");
  auto lifecycle_callbacks_class = context->vm->RegisterClass(
      "com/roblox/universalapp/activitylifecyclecallbacks/"
      "JNIActivityLifecycleCallbacks");
  auto settings_class = context->vm->RegisterClass("rbx/JNIRobloxSettings");
  settings_class->RegisterMethod(
      "nativeInitClientSettings", "()V",
      [](JNIEnv* /*env*/, jobject /*obj*/) {
        std::cout << "  [JNI callback] nativeInitClientSettings invoked\n";
      });
  static_cast<void>(context_class);
  static_cast<void>(activity_class);
  static_cast<void>(lifecycle_callbacks_class);
  PublishCurrentJniEnv(context->vm->GetJNIEnv());
  context->result = context->fn(context->vm->GetJavaVM(), nullptr);
  return nullptr;
}

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


constexpr size_t kDefaultEngineStackSize = 1024ULL * 1024 * 1024;
constexpr size_t kMinEngineStackSize = 16ULL * 1024 * 1024;


bool IsEnabled(const char* name);
bool IsDisabled(const char* name);


const char* CachedGetenv(const char* name) {
  if (name == nullptr) {
    return nullptr;
  }
  // Launch flags are string literals and do not change after startup. Cache by
  // pointer identity so the main-thread pump does not walk environ + allocate
  // on every tick.
  static std::mutex mutex;
  static std::unordered_map<const char*, const char*> cache;
  std::lock_guard<std::mutex> lock(mutex);
  const auto existing = cache.find(name);
  if (existing != cache.end()) {
    return existing->second;
  }
  const char* value = std::getenv(name);
  cache.emplace(name, value);
  return value;
}

bool IsEnabled(const char* name) {
  const char* value = CachedGetenv(name);
  return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

bool IsDisabled(const char* name) {
  const char* value = CachedGetenv(name);
  return value != nullptr && std::strcmp(value, "0") == 0;
}

bool StartsWith(const char* value, const char* prefix) {
  if (value == nullptr || prefix == nullptr) {
    return false;
  }
  return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}

bool IsGlSymbol(const char* name) {
  return StartsWith(name, "gl");
}

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

// dlsym searches the object and its dependency chain, so a
// stub that transitively links SDL or another host library would
// satisfy EGL/GL/Vulkan lookups with unrelated host symbols
//
// So, we should only accept the symbol if its stub owns it.
bool StubOwnsSymbolAddress(void* handle, void* address) {
  if (handle == nullptr || address == nullptr) {
    return false;
  }

  Dl_info info = {};
  if (::dladdr(address, &info) == 0 || info.dli_fbase == nullptr) {
    return false;
  }

	link_map* map = nullptr;
  if (::dlinfo(handle, RTLD_DI_LINKMAP, &map) != 0 || map == nullptr) {
    return false;
  }

  return reinterpret_cast<void*>(map->l_addr) == info.dli_fbase;
}

SymbolResolveResult ResolveSymbolForBionic(const char* name, bool has_window,
                                          void* real_gles_handle,
                                          const std::vector<void*>& stub_handles) {
  const bool is_gl_symbol = IsGlSymbol(name);
  const bool prefer_real_gles = has_window;
  SymbolResolveResult result;

  if (is_gl_symbol && prefer_real_gles) {
    result.address = mocktail::window::GetGLProcAddress(name);
    if (result.address != nullptr) {
      result.source = SymbolResolveSource::kWindow;
      return result;
    }
  }

  if (is_gl_symbol && prefer_real_gles && real_gles_handle != nullptr) {
    result.address = ::dlsym(real_gles_handle, name);
    if (result.address != nullptr) {
      result.source = SymbolResolveSource::kRealGles;
      return result;
    }
  }

  for (void* h : stub_handles) {
    if (void *candidate = ::dlsym(h, name); candidate != nullptr && StubOwnsSymbolAddress(h, candidate)) {
      result.address = candidate;
      result.source = SymbolResolveSource::kStub;
      return result;
    }
  }

  result.address = ::dlsym(RTLD_DEFAULT, name);
  if (result.address != nullptr) {
    result.source = SymbolResolveSource::kHost;
  }
  return result;
}

bool TraceAllEnabled() { return IsEnabled("MOCKTAIL_TRACE"); }

void SetEnvDefault(const char* name, const char* value) {
  if (name == nullptr || value == nullptr) {
    return;
  }
  const char* current = std::getenv(name);
  if (current == nullptr || current[0] == '\0') {
    setenv(name, value, 1);
  }
}

bool IsMocktailStubGlesPath(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return true;
  }
  std::string value(path);
  return value == "libGLESv2.so" ||
         value.find("/build/libGLESv2.so") != std::string::npos;
}

void* OpenRealGlesLibrary() {
  const char* env_library = std::getenv("MOCKTAIL_GLES_LIBRARY");
  const char* candidates[] = {
      env_library,
      "/usr/lib/libGLESv2.so.2",
      "/usr/lib64/libGLESv2.so.2",
      "libGLESv2.so.2",
      "/usr/lib/chromium/libGLESv2.so",
      "/usr/lib/chromium-browser/libGLESv2.so",
      "/usr/lib/electron42/libGLESv2.so",
      "/usr/lib/electron41/libGLESv2.so",
      "/usr/lib/electron40/libGLESv2.so",
      "/usr/lib/electron39/libGLESv2.so",
      "/usr/lib/cef/libGLESv2.so",
  };
  for (const char* candidate : candidates) {
    if (IsMocktailStubGlesPath(candidate)) {
      continue;
    }
    void* handle = ::dlopen(candidate, RTLD_LAZY | RTLD_LOCAL);
    if (handle != nullptr) {
      std::cout << "  [gles] Using real GLES from " << candidate << '\n';
      return handle;
    }
  }
  std::cerr << "  [gles] real libGLESv2 not found; using host GLES shim\n";
  return nullptr;
}

void ApplyRuntimeDefaults() {
  SetEnvDefault("MOCKTAIL_OFFLINE_SETTINGS", "1");
  // Startup worker ownership is joined. Returning while it still references
  // JNI, runtime, or stack state is never supported.
  SetEnvDefault("MOCKTAIL_ENGINE_DETACH", "0");
  // Roblox refuses to create its TaskScheduler until its FastFlag store is
  // loaded, and that store comes from ClientSettings. The offline-settings
  // defaults only disable flag fetching, so both of these must stay on for
  // the engine to get past "flags have been loaded".
  SetEnvDefault("MOCKTAIL_ALLOW_NO_COOKIE_LUA_APP", "1");
  // Current Roblox Android builds drive app startup through NativeGLInterface's
  // V2 app bridge path. The legacy NativeAppBridgeInterface entry point stays
  // available by opt-in, but running it in parallel can block V2 init.
  SetEnvDefault("MOCKTAIL_APP_BRIDGE_APP_START", "0");
  SetEnvDefault("MOCKTAIL_APP_BRIDGE_APP_START_THREAD", "0");
  SetEnvDefault("MOCKTAIL_START_LUA_APP_DM", "1");
  SetEnvDefault("MOCKTAIL_START_LUA_APP_DM_INLINE", "0");
  // The shipped APK drives these callbacks in a mostly synchronous ASMA flow.
  // Keep the background worker path opt-in so the default mirrors release
  // ordering and avoids racing heap-sensitive V2 startup state.
  SetEnvDefault("MOCKTAIL_START_LUA_APP_DM_THREAD", "0");
  SetEnvDefault("MOCKTAIL_START_LUA_APP_DM_DELAY_MS", "500");
  SetEnvDefault("MOCKTAIL_PLACE_ID", "0");
  SetEnvDefault("MOCKTAIL_CALL_REAL_APP_BRIDGE_INIT", "1");
  SetEnvDefault("MOCKTAIL_CALL_REAL_APP_BRIDGE_INIT_THREAD", "0");
  SetEnvDefault("MOCKTAIL_APP_BRIDGE_INIT_THREAD_TIMEOUT_MS", "1500");
  SetEnvDefault("MOCKTAIL_START_GAME_WITH_PARAM", "0");
  // APK ASMA calls nativeAppBridgeV2StartAppWithParams synchronously before
  // driving post-start surface/Lua callbacks. Keep the worker path opt-in so
  // those follow-up callbacks cannot race native StartApp construction.
  SetEnvDefault("MOCKTAIL_CALL_REAL_APP_BRIDGE_START_THREAD", "0");
  SetEnvDefault("MOCKTAIL_ASMA_START_TASK_SCHEDULER_FOREGROUND", "1");
  SetEnvDefault("MOCKTAIL_TASK_SCHEDULER_FOREGROUND_ON_MAIN_THREAD", "1");
  SetEnvDefault("MOCKTAIL_NATIVE_FRAGMENT_START", "1");
  SetEnvDefault("MOCKTAIL_PASS_CURRENT_DISPLAY_REFRESH_RATE", "1");
  SetEnvDefault("MOCKTAIL_PASS_SUPPORTED_REFRESH_RATES", "1");
  SetEnvDefault("MOCKTAIL_PASS_ACTIVITY_TO_GAME_SURFACE_PARAMS", "0");
  SetEnvDefault("MOCKTAIL_SYNC_START_APP_WITH_GAME", "1");
  // APK ASMA publishes queued ready events after the Lua app startup path has
  // run. Keep this inline by default; the detached worker can outlive teardown.
  SetEnvDefault("MOCKTAIL_SEND_APP_READY_THREAD", "0");
  SetEnvDefault("MOCKTAIL_UPDATE_SCREEN_ORIENTATION", "0");
  // Keep the ASMA/V2 NativeGL path as the default. GameActivity's native app
  // glue currently stalls V2 init on surface flags; leave it available opt-in.
  // GameActivity lifecycle callbacks can block on android_app_set_activity_state
  // in some Linux shims; keep them opt-in to avoid startup dead-ends.
  SetEnvDefault("MOCKTAIL_MAIN_THREAD_MESSAGE_PUMP", "0");
  SetEnvDefault("MOCKTAIL_IGNORE_WINDOW_CLOSE", "0");
  SetEnvDefault("MOCKTAIL_WIN_TITLE", "Roblox");
  SetEnvDefault("MOCKTAIL_GRAPHICS_BACKEND", "angle-vulkan");
}

int GetEnvInt(const char* name, int default_value) {
  const char* value = CachedGetenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  char* end = nullptr;
  long parsed = std::strtol(value, &end, 10);
  if (end == value) {
    return default_value;
  }
  return static_cast<int>(parsed);
}


jlong GetEnvLong(const char* name, jlong default_value) {
  const char* value = CachedGetenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  char* end = nullptr;
  long long parsed = std::strtoll(value, &end, 10);
  if (end == value) {
    return default_value;
  }
  return static_cast<jlong>(parsed);
}

uint64_t MonotonicMillis() {
  timespec ts{};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
         static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

uint64_t MonotonicNanos() {
  timespec ts{};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

JNIEnv* AttachMainThreadJniEnv() {
  if (g_vm_for_main_thread_pump == nullptr) {
    return nullptr;
  }
  JNIEnv* env = nullptr;
  JavaVM* java_vm = g_vm_for_main_thread_pump->GetJavaVM();
  if (java_vm != nullptr) {
    void* raw_env = nullptr;
    jint attach_result = java_vm->AttachCurrentThread(&raw_env, nullptr);
    if (attach_result == JNI_OK && raw_env != nullptr) {
      env = static_cast<JNIEnv*>(raw_env);
    } else if (IsEnabled("MOCKTAIL_TRACE")) {
      std::cerr << "  [main] AttachCurrentThread failed in pump: "
                << attach_result << '\n'
                << std::flush;
    }
  }
  if (env == nullptr) {
    env = g_vm_for_main_thread_pump->GetJNIEnv();
  }
  g_vm_for_main_thread_pump->RestoreFunctions();
  PublishCurrentJniEnv(env);
  return env;
}

bool InvokeTaskSchedulerForeground(
    JNIEnv* env, jclass native_gl_class,
    NativeSetTaskSchedulerBackgroundModeFn native_set_task_scheduler_background_mode,
    const char* log_scope) {
  if (env == nullptr || native_gl_class == nullptr ||
      native_set_task_scheduler_background_mode == nullptr) {
    std::cerr << "  [" << log_scope
              << "] NativeGLInterface.setTaskSchedulerBackgroundMode skipped: "
              << "missing JNI state\n"
              << std::flush;
    return false;
  }

  jstring reason = env->NewStringUTF("ASMA.start");
  std::cout << "  [" << log_scope
            << "] NativeGLInterface.setTaskSchedulerBackgroundMode(false, "
            << "ASMA.start)\n"
            << std::flush;
  if (sigsetjmp(g_update_surface_app_jmp_buf, 0) == 0) {
    g_update_surface_app_recovery_in_progress = kStage6RecoveryInline;
    native_set_task_scheduler_background_mode(env, native_gl_class, JNI_FALSE,
                                              reason);
    g_update_surface_app_recovery_in_progress = kStage6RecoveryInactive;
    std::cout << "  [" << log_scope
              << "] NativeGLInterface.setTaskSchedulerBackgroundMode returned\n"
              << std::flush;
    return true;
  }

  g_update_surface_app_recovery_in_progress = kStage6RecoveryInactive;
  std::cerr << "  [" << log_scope
            << "] setTaskSchedulerBackgroundMode recovered from crash\n"
            << std::flush;
  return false;
}

void RunPendingMainThreadTaskSchedulerForeground() {
  int expected = kMainThreadTaskSchedulerPending;
  if (!g_pending_main_thread_task_scheduler_state.compare_exchange_strong(
          expected, kMainThreadTaskSchedulerRunning,
          std::memory_order_acq_rel)) {
    return;
  }

  JNIEnv* env = AttachMainThreadJniEnv();
  const bool invoked = InvokeTaskSchedulerForeground(
      env, g_pending_main_thread_task_scheduler_class,
      g_pending_main_thread_task_scheduler_background_mode, "main");
  g_pending_main_thread_task_scheduler_recovered.store(invoked ? 0 : 1,
                                                       std::memory_order_release);
  g_pending_main_thread_task_scheduler_state.store(
      kMainThreadTaskSchedulerComplete, std::memory_order_release);
}

bool RunTaskSchedulerForegroundOnMainThread(
    NativeSetTaskSchedulerBackgroundModeFn native_set_task_scheduler_background_mode,
    jclass native_gl_class) {
  if (IsDisabled("MOCKTAIL_TASK_SCHEDULER_FOREGROUND_ON_MAIN_THREAD") ||
      IsEnabled("MOCKTAIL_ENGINE_INLINE") || !mocktail::window::IsInitialised()) {
    return false;
  }
  if (native_set_task_scheduler_background_mode == nullptr ||
      native_gl_class == nullptr) {
    return false;
  }

  g_pending_main_thread_task_scheduler_background_mode =
      native_set_task_scheduler_background_mode;
  g_pending_main_thread_task_scheduler_class = native_gl_class;
  g_pending_main_thread_task_scheduler_recovered.store(
      0, std::memory_order_release);
  g_pending_main_thread_task_scheduler_state.store(
      kMainThreadTaskSchedulerPending, std::memory_order_release);
  std::cout << "  [engine] NativeGLInterface.setTaskSchedulerBackgroundMode "
            << "scheduled on main thread\n"
            << std::flush;

  const int timeout_ms = GetEnvInt(
      "MOCKTAIL_TASK_SCHEDULER_FOREGROUND_MAIN_THREAD_TIMEOUT_MS", 3000);
  const uint64_t start_ms = MonotonicMillis();
  while (true) {
    const int state = g_pending_main_thread_task_scheduler_state.load(
        std::memory_order_acquire);
    if (state == kMainThreadTaskSchedulerComplete) {
      const bool recovered =
          g_pending_main_thread_task_scheduler_recovered.load(
              std::memory_order_acquire) != 0;
      g_pending_main_thread_task_scheduler_state.store(
          kMainThreadTaskSchedulerIdle, std::memory_order_release);
      if (recovered) {
        std::cerr << "  [engine] main-thread "
                  << "setTaskSchedulerBackgroundMode recovered from crash\n"
                  << std::flush;
      } else {
        std::cout << "  [engine] main-thread "
                  << "setTaskSchedulerBackgroundMode returned\n"
                  << std::flush;
      }
      return true;
    }

    if (timeout_ms >= 0) {
      const uint64_t now_ms = MonotonicMillis();
      if (now_ms >= start_ms &&
          now_ms - start_ms >= static_cast<uint64_t>(timeout_ms)) {
        g_pending_main_thread_task_scheduler_state.store(
            kMainThreadTaskSchedulerTimedOut, std::memory_order_release);
        std::cerr << "  [engine] main-thread "
                  << "setTaskSchedulerBackgroundMode timed out after "
                  << timeout_ms << " ms\n"
                  << std::flush;
        return true;
      }
    }
    usleep(1000);
  }
}

void PumpRobloxMainThreadMessagesOnce() {
  static const bool force_early =
      IsEnabled("MOCKTAIL_FORCE_EARLY_MAIN_THREAD_MESSAGE_PUMP");
  static const bool pump_disabled =
      IsDisabled("MOCKTAIL_MAIN_THREAD_MESSAGE_PUMP");
  static const bool trace_pump =
      IsEnabled("MOCKTAIL_TRACE");
  static const int pump_limit =
      GetEnvInt("MOCKTAIL_MAIN_THREAD_MESSAGE_PUMP_LIMIT", 0);
  if (g_main_thread_message_pump_ready.load() == 0 && !force_early) {
    static bool logged_not_ready = false;
    if (!logged_not_ready && trace_pump) {
      logged_not_ready = true;
      std::cerr << "  [main] nativeCallMessagesFromMainThread pump not ready\n"
                << std::flush;
    }
    return;
  }

  if (g_native_call_messages_from_main_thread == nullptr ||
      g_vm_for_main_thread_pump == nullptr ||
      g_native_gl_class_for_main_thread == nullptr || pump_disabled) {
    static bool logged_unavailable = false;
    if (!logged_unavailable && trace_pump) {
      logged_unavailable = true;
      std::cerr << "  [main] nativeCallMessagesFromMainThread pump unavailable"
                << " fn="
                << reinterpret_cast<const void*>(
                       g_native_call_messages_from_main_thread)
                << " vm=" << static_cast<void*>(g_vm_for_main_thread_pump)
                << " class="
                << reinterpret_cast<const void*>(
                       g_native_gl_class_for_main_thread)
                << " disabled=" << pump_disabled << '\n'
                << std::flush;
    }
    return;
  }

  JNIEnv* env = AttachMainThreadJniEnv();
  if (env == nullptr) {
    static bool logged_missing_env = false;
    if (!logged_missing_env && trace_pump) {
      logged_missing_env = true;
      std::cerr << "  [main] nativeCallMessagesFromMainThread pump has no JNIEnv\n"
                << std::flush;
    }
    return;
  }
  static int pump_count = 0;
  ++pump_count;
  if (pump_limit > 0 && pump_count > pump_limit) {
    return;
  }
  if (__builtin_expect(trace_pump, 0)) {
    if (pump_count <= 10 || pump_count % 100 == 0) {
      std::cerr << "  [main] nativeCallMessagesFromMainThread pump #"
                << pump_count << " env=" << static_cast<void*>(env) << '\n'
                << std::flush;
    }
  }
  g_native_call_messages_from_main_thread(
      env, g_native_gl_class_for_main_thread);
  if (__builtin_expect(trace_pump, 0)) {
    if (pump_count <= 10 || pump_count % 100 == 0) {
      std::cerr << "  [main] nativeCallMessagesFromMainThread returned #"
                << pump_count << '\n'
                << std::flush;
    }
  }
}

void PumpStartupOwnerThread(void* /*context*/) {
  RunPendingMainThreadTaskSchedulerForeground();
  PumpRobloxMainThreadMessagesOnce();
}

std::string GetEnvString(const char* name, const char* default_value) {
  const char* value = CachedGetenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value ? default_value : "";
  }
  return value;
}

bool HasEnvValue(const char* name) {
  const char* value = CachedGetenv(name);
  return value != nullptr && value[0] != '\0';
}

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

static_assert(sizeof(BionicAddrInfo) == 48,
              "unexpected bionic addrinfo size");

bool DnsTraceEnabled() {
  return IsEnabled("MOCKTAIL_TRACE");
}

addrinfo HostHintsFromBionic(const BionicAddrInfo* hints) {
  addrinfo host_hints{};
  if (!hints) {
    return host_hints;
  }
  host_hints.ai_flags = hints->ai_flags;
  host_hints.ai_family = hints->ai_family;
  host_hints.ai_socktype = hints->ai_socktype;
  host_hints.ai_protocol = hints->ai_protocol;
  return host_hints;
}

BionicAddrInfo* BionicAddrInfoFromHost(const addrinfo* host) {
  BionicAddrInfo* head = nullptr;
  BionicAddrInfo* tail = nullptr;
  for (const addrinfo* current = host; current; current = current->ai_next) {
    auto* node =
        static_cast<BionicAddrInfo*>(std::calloc(1, sizeof(BionicAddrInfo)));
    if (!node) {
      break;
    }
    node->ai_flags = current->ai_flags;
    node->ai_family = current->ai_family;
    node->ai_socktype = current->ai_socktype;
    node->ai_protocol = current->ai_protocol;
    node->ai_addrlen = current->ai_addrlen;
    if (current->ai_canonname) {
      node->ai_canonname = ::strdup(current->ai_canonname);
    }
    if (current->ai_addr && current->ai_addrlen > 0) {
      node->ai_addr = static_cast<sockaddr*>(std::malloc(current->ai_addrlen));
      if (node->ai_addr) {
        std::memcpy(node->ai_addr, current->ai_addr, current->ai_addrlen);
      }
    }
    if (!head) {
      head = node;
    } else {
      tail->ai_next = node;
    }
    tail = node;
  }
  return head;
}

extern "C" int mocktail_getaddrinfo(const char* node, const char* service,
                                    const BionicAddrInfo* hints,
                                    BionicAddrInfo** result) {
  if (!result) {
    return EAI_FAIL;
  }
  *result = nullptr;
  addrinfo host_hints = HostHintsFromBionic(hints);
  addrinfo* host_result = nullptr;
  int rc = ::getaddrinfo(node, service, hints ? &host_hints : nullptr,
                         &host_result);
  if (DnsTraceEnabled()) {
    std::cout << "  [dns] getaddrinfo node=" << (node ? node : "(null)")
              << " service=" << (service ? service : "(null)")
              << " family=" << (hints ? hints->ai_family : 0)
              << " socktype=" << (hints ? hints->ai_socktype : 0)
              << " rc=" << rc
              << " message=" << (rc == 0 ? "ok" : ::gai_strerror(rc))
              << '\n';
  }
  if (rc != 0) {
    return rc;
  }
  *result = BionicAddrInfoFromHost(host_result);
  ::freeaddrinfo(host_result);
  return *result ? 0 : EAI_MEMORY;
}

extern "C" void mocktail_freeaddrinfo(BionicAddrInfo* info) {
  while (info) {
    BionicAddrInfo* next = info->ai_next;
    std::free(info->ai_canonname);
    std::free(info->ai_addr);
    std::free(info);
    info = next;
  }
}

extern "C" hostent* mocktail_gethostbyname(const char* name) {
  hostent* result = ::gethostbyname(name);
  if (DnsTraceEnabled()) {
    std::cout << "  [dns] gethostbyname name=" << (name ? name : "(null)")
              << " result=" << reinterpret_cast<void*>(result) << '\n';
  }
  return result;
}

extern "C" int mocktail_mprotect(void* addr, size_t len, int prot) {
  if (addr == nullptr || len == 0) {
    errno = EINVAL;
    return -1;
  }
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    errno = EINVAL;
    return -1;
  }
  uintptr_t start = reinterpret_cast<uintptr_t>(addr);
  uintptr_t page_mask = static_cast<uintptr_t>(page_size) - 1;
  uintptr_t page_start = start & ~page_mask;
  uintptr_t end = start + len;
  if (end < start) {
    errno = EINVAL;
    return -1;
  }
  uintptr_t page_end = (end + page_mask) & ~page_mask;
  if (page_end < page_start) {
    errno = EINVAL;
    return -1;
  }
  return ::mprotect(reinterpret_cast<void*>(page_start),
                    static_cast<size_t>(page_end - page_start), prot);
}

void RegisterBionicDnsWrappers() {
  linker::RegisterSymbol("getaddrinfo",
                         reinterpret_cast<void*>(mocktail_getaddrinfo));
  linker::RegisterSymbol("freeaddrinfo",
                         reinterpret_cast<void*>(mocktail_freeaddrinfo));
  linker::RegisterSymbol("gethostbyname",
                         reinterpret_cast<void*>(mocktail_gethostbyname));
}

void RegisterBionicPathWrappers() {
  linker::RegisterSymbol("open", reinterpret_cast<void*>(mocktail_open));
  linker::RegisterSymbol("__open_2", reinterpret_cast<void*>(mocktail___open_2));
  linker::RegisterSymbol("fopen", reinterpret_cast<void*>(mocktail_fopen));
  linker::RegisterSymbol("access", reinterpret_cast<void*>(mocktail_access));
  linker::RegisterSymbol("stat", reinterpret_cast<void*>(mocktail_stat));
  linker::RegisterSymbol("lstat", reinterpret_cast<void*>(mocktail_lstat));
  linker::RegisterSymbol("statvfs", reinterpret_cast<void*>(mocktail_statvfs));
  linker::RegisterSymbol("statfs", reinterpret_cast<void*>(mocktail_statfs));
  linker::RegisterSymbol("mkdir", reinterpret_cast<void*>(mocktail_mkdir));
  linker::RegisterSymbol("opendir", reinterpret_cast<void*>(mocktail_opendir));
  linker::RegisterSymbol("rename", reinterpret_cast<void*>(mocktail_rename));
  linker::RegisterSymbol("unlink", reinterpret_cast<void*>(mocktail_unlink));
  linker::RegisterSymbol("rmdir", reinterpret_cast<void*>(mocktail_rmdir));
  linker::RegisterSymbol("realpath", reinterpret_cast<void*>(mocktail_realpath));
  linker::RegisterSymbol("readlink", reinterpret_cast<void*>(mocktail_readlink));
  linker::RegisterSymbol("__readlink_chk",
                         reinterpret_cast<void*>(mocktail___readlink_chk));
}

mocktail::runtime::RuntimePaths CurrentRuntimePaths() {
  const mocktail::runtime::ProcessEnvironment environment;
  return mocktail::runtime::RuntimePaths::FromEnvironment(environment);
}

std::string MocktailCacheRoot() {
  return CurrentRuntimePaths().cache_root().string();
}

std::string MocktailConfigRoot() {
  return CurrentRuntimePaths().config_root().string();
}

std::string DefaultAssetPath() {
  return CurrentRuntimePaths().DefaultAssetPath().string();
}

bool RegisterFreshGamePresentObserver(
    void* context, mocktail::runtime::FreshLaunchPresentObserver observer,
    void* observer_context) {
  if (context == nullptr) {
    return false;
  }
  return static_cast<mocktail::window::ScopedPresentObserver*>(context)
      ->Register(observer, observer_context);
}

void ClearFreshGamePresentObserver(void* context) {
  if (context != nullptr) {
    static_cast<mocktail::window::ScopedPresentObserver*>(context)->Reset();
  }
}

jobject CreateExperienceRawCallback(
    void* context, std::shared_ptr<void> callback_context,
    void (*run)(void*, JNIEnv*, jstring)) {
  if (context == nullptr || run == nullptr) {
    return nullptr;
  }
  return static_cast<jnivm::VM*>(context)->CreateMessageBusRawCallback(
      std::move(callback_context), jnivm::MessageBusRawCallbacks{run});
}

void ClearExperienceRawCallback(void* context, jobject callback) {
  if (context != nullptr) {
    static_cast<jnivm::VM*>(context)->ClearMessageBusRawCallback(callback);
  }
}

jobject CreateAsyncMessageBusRequestHandler(
    void* context, std::shared_ptr<void> callback_context,
    void (*run)(void*, JNIEnv*, jstring, jstring)) {
  return context ? static_cast<jnivm::VM*>(context)
                       ->CreateMessageBusAsyncRequestHandler(
                           std::move(callback_context),
                           jnivm::MessageBusAsyncRequestHandlerCallbacks{run})
                 : nullptr;
}

void ClearAsyncMessageBusRequestHandler(void* context, jobject handler) {
  if (context) {
    static_cast<jnivm::VM*>(context)->ClearMessageBusAsyncRequestHandler(
        handler);
  }
}

struct ExperienceLifecycleTarget {
  std::weak_ptr<mocktail::runtime::RobloxExperienceComposition> composition;
};

void NotifyLuaAppDidReturn(void* context) {
  if (context == nullptr) {
    return;
  }
  const std::shared_ptr<mocktail::runtime::RobloxExperienceComposition>
      composition =
          static_cast<ExperienceLifecycleTarget*>(context)->composition.lock();
  if (composition != nullptr) {
    composition->NotifyLuaAppDidReturn();
  }
}

mocktail::runtime::GameSessionUpdateResult ExperienceSurfaceCreated(
    void* context, uint64_t generation) {
  return static_cast<mocktail::runtime::RobloxExperienceComposition*>(context)
      ->SurfaceCreated(generation);
}

mocktail::runtime::GameSessionUpdateResult ExperienceSurfaceChanged(
    void* context, mocktail::runtime::GameSurface surface) {
  return static_cast<mocktail::runtime::RobloxExperienceComposition*>(context)
      ->SurfaceChanged(std::move(surface));
}

mocktail::runtime::GameSessionUpdateResult ExperienceSurfaceDestroyed(
    void* context, uint64_t generation) {
  return static_cast<mocktail::runtime::RobloxExperienceComposition*>(context)
      ->SurfaceDestroyed(generation);
}

std::string GetEnvStringDefaultPath(const char* name,
                                    const std::string& default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  return value;
}

bool EnsureDirectory(const std::string& path) {
  return mocktail::runtime::RuntimePaths::EnsureDirectory(path);
}

void EnsureAndroidDirectory(const std::string& android_path) {
  std::string host_path = libc_shim::TranslatePath(android_path);
  if (EnsureDirectory(host_path) && host_path != android_path &&
      IsEnabled("MOCKTAIL_TRACE")) {
    std::cerr << "  [engine] directory " << android_path << " -> "
              << host_path << '\n';
  }
}

size_t GetEngineStackSize() { return kDefaultEngineStackSize; }

void PrintStage(int stage, const char* description) {
  std::cout << "[stage " << stage << "] " << description << '\n'
            << std::flush;
}


bool IsUnsafeSoftTimeoutModule(void* rip) {
  Dl_info dlinfo;
  if (rip == nullptr || dladdr(rip, &dlinfo) == 0 || dlinfo.dli_fname == nullptr) {
    return true;
  }
  const char* module = dlinfo.dli_fname;
  return std::strstr(module, "libc.so") != nullptr ||
         std::strstr(module, "libpthread.so") != nullptr ||
         std::strstr(module, "ld-linux") != nullptr ||
         std::strstr(module, "libstdc++") != nullptr ||
         std::strstr(module, "libgcc_s") != nullptr;
}

void JniOnLoadTimeoutAlarm(int, siginfo_t* info, void* context) {
  static_cast<void>(info);
  auto* uc = static_cast<ucontext_t*>(context);
#if defined(__x86_64__)
  auto rip = uc ? static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]) : 0;
  auto rsp = uc ? static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RSP]) : 0;
  auto rbp = uc ? static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]) : 0;
  auto rax = uc ? static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RAX]) : 0;
  const char* regs_format =
      "  [timeout] RIP=0x%016llx RSP=0x%016llx RBP=0x%016llx "
      "RAX=0x%016llx\n";
#elif defined(__aarch64__)
  auto rip = uc ? static_cast<uintptr_t>(uc->uc_mcontext.pc) : 0;
  auto rsp = uc ? static_cast<uintptr_t>(uc->uc_mcontext.sp) : 0;
  auto rbp = uc ? static_cast<uintptr_t>(uc->uc_mcontext.regs[29]) : 0;
  auto rax = uc ? static_cast<uintptr_t>(uc->uc_mcontext.regs[0]) : 0;
  const char* regs_format =
      "  [timeout] PC=0x%016llx SP=0x%016llx FP=0x%016llx "
      "X0=0x%016llx\n";
#else
  uintptr_t rip = 0, rsp = 0, rbp = 0, rax = 0;
  const char* regs_format =
      "  [timeout] PC=0x%016llx SP=0x%016llx FP=0x%016llx "
      "X0=0x%016llx\n";
#endif
  char regs_msg[192];
  int len = std::snprintf(
      regs_msg, sizeof(regs_msg), regs_format,
      static_cast<unsigned long long>(rip), static_cast<unsigned long long>(rsp),
      static_cast<unsigned long long>(rbp), static_cast<unsigned long long>(rax));
  write(2, regs_msg, static_cast<size_t>(len));

  if (g_jni_onload_in_progress == 0 || g_jni_onload_timings_printed != 0) {
    return;
  }
  g_jni_onload_timings_printed = 1;

  const char prefix[] =
      "  [timeout] JNI_OnLoad still in progress; stack snapshot:\n";
  write(2, prefix, sizeof(prefix) - 1);
  PrintContextBacktrace(uc, "    ");
  PrintBacktraceNoSig("    ");
  if (g_jni_onload_soft_timeout != 0 && g_jni_onload_jmp_armed != 0 &&
      !IsUnsafeSoftTimeoutModule(reinterpret_cast<void*>(rip))) {
    g_jni_onload_jmp_armed = 0;
    g_jni_onload_in_progress = 0;
    siglongjmp(g_jni_onload_jmp_buf, 1);
    return;
  }
  if (g_jni_onload_soft_timeout != 0 && g_jni_onload_jmp_armed != 0) {
    const char skip_msg[] =
        "  [timeout] soft timeout skipped for unsafe module; waiting\n";
    write(2, skip_msg, sizeof(skip_msg) - 1);
    return;
  }
  _exit(124);
}


void PublishCurrentJniEnv(JNIEnv* env) {
  using SetCurrentJniEnvFn = void (*)(void*);
  auto* set_current_jni_env = reinterpret_cast<SetCurrentJniEnvFn>(
      ::dlsym(RTLD_DEFAULT, "mocktail_set_current_jni_env"));
  if (set_current_jni_env) {
    set_current_jni_env(env);
  }
}

bool EngineTraceEnabled() {
  return IsEnabled("MOCKTAIL_TRACE");
}

void PreloadPthreadSymbols() {
  using PreloadPthreadSymbolsFn = void (*)();
  auto* preload_pthread_symbols = reinterpret_cast<PreloadPthreadSymbolsFn>(
      ::dlsym(RTLD_DEFAULT, "mocktail_preload_pthread_symbols"));
  if (preload_pthread_symbols) {
    preload_pthread_symbols();
  }
}

void EngineLog(const char* message) {
  if (EngineTraceEnabled()) {
    std::cerr << "  [engine] " << message << '\n';
  }
}

void EngineLogPtr(const char* name, const void* ptr) {
  if (EngineTraceEnabled()) {
    std::cerr << "  [engine] " << name << "=0x"
              << std::hex << reinterpret_cast<uintptr_t>(ptr) << std::dec
              << '\n';
  }
}


void PrintBacktraceNoSig(const char* prefix) { (void)prefix; }

void PrintContextBacktrace(ucontext_t* ucontext, const char* prefix) {
  (void)ucontext;
  (void)prefix;
}

extern "C" int mocktail_recover_stack_chk_fail() {
  if (g_init_with_params_recovery_in_progress != 0) {
    g_init_with_params_recovery_in_progress = 0;
    siglongjmp(g_init_with_params_jmp_buf, 1);
  }
  if (g_start_app_with_params_recovery_in_progress != 0) {
    g_start_app_with_params_recovery_in_progress = 0;
    siglongjmp(g_start_app_with_params_jmp_buf, 1);
  }
  if (g_start_lua_app_dm_recovery_in_progress != 0) {
    g_start_lua_app_dm_recovery_in_progress = 0;
    siglongjmp(g_start_lua_app_dm_jmp_buf, 1);
  }
  if (g_game_global_init_recovery_in_progress != 0) {
    g_game_global_init_recovery_in_progress = 0;
    siglongjmp(g_game_global_init_jmp_buf, 1);
  }
  if (g_init_client_settings_recovery_in_progress != 0) {
    g_init_client_settings_recovery_in_progress = 0;
    siglongjmp(g_init_client_settings_jmp_buf, 1);
  }
  if (g_post_client_settings_recovery_in_progress != 0) {
    g_post_client_settings_recovery_in_progress = 0;
    siglongjmp(g_post_client_settings_jmp_buf, 1);
  }
  const char msg[] =
      "  [patch] stack check bridge had no active recovery target\n";
  write(2, msg, sizeof(msg) - 1);
  return 0;
}

static uintptr_t g_libroblox_base_static = 0;

extern "C" void mocktail_before_soinfo_constructors(const char* realpath,
                                                    uintptr_t base) {
  if (realpath == nullptr || std::strstr(realpath, "libroblox.so") == nullptr ||
      base == 0) {
    return;
  }

  g_libroblox_base = base;
  g_mocktail_abort_libroblox_base = base;
  g_libroblox_base_static = base;

  // No host allocator bridge and no guest thread initializer: the guest
  // allocates through the bionic shims and the host handles pthread startup.
  mocktail::compat::ConfigureBionicPthreadThreadInitializer(nullptr);
  libc_shim::ConfigureGuestAllocator(nullptr);

  // The whole .init_array runs natively; see
  // mocktail_should_skip_soinfo_constructors.
  std::cout << "  [compat] leaving libroblox constructors untouched\n"
            << std::flush;
}

extern "C" bool mocktail_should_skip_soinfo_constructors(const char* realpath) {
  if (realpath == nullptr ||
      std::strstr(realpath, "libroblox.so") == nullptr) {
    return false;
  }

  // libroblox.so's .init_array must run in full: its static state is needed by
  // the first JNI entry point, and skipping it faults. There is no per-build
  // constructor policy any more, so let the linker's own replay run them.
  std::cout << "  [compat] running libroblox static constructors\n"
            << std::flush;
  return false;
}


std::string JStringToString(JNIEnv* env, jstring value) {
  if (!env || !value) {
    return {};
  }
  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (!chars) {
    return {};
  }
  std::string result(chars);
  env->ReleaseStringUTFChars(value, chars);
  return result;
}

void MocktailSetAssetPath(JNIEnv* env, jstring asset_path) {
  std::string path = JStringToString(env, asset_path);
  if (path.empty()) {
    path = DefaultAssetPath();
  }
  setenv("MOCKTAIL_ASSET_ROOT", path.c_str(), 1);
  std::cout << "  [engine] asset root set to " << path << '\n' << std::flush;
}

void MocktailAppBridgeInit(JNIEnv* env, jstring app_params) {
  std::string params = JStringToString(env, app_params);
  if (params.empty()) {
    params = "{}";
  }
  setenv("MOCKTAIL_APP_BRIDGE_PARAMS", params.c_str(), 1);
  setenv("MOCKTAIL_APP_BRIDGE_INIT", "1", 1);
  std::cout << "  [engine] app bridge params staged\n" << std::flush;
}

void MocktailAppBridgeStart(JNIEnv*, jstring) {
  std::cout << "  [engine] app bridge marked started\n" << std::flush;
}

jstring NewStringFromEnvDefault(JNIEnv* env, const char* env_name,
                                const char* default_value) {
  const char* value = std::getenv(env_name);
  if (!value || value[0] == '\0') {
    value = default_value;
  }
  return env->NewStringUTF(value);
}

bool CookieHasAttribute(const std::string& cookie, const char* attribute) {
  std::string lower_cookie = cookie;
  std::string lower_attribute = attribute ? attribute : "";
  std::transform(lower_cookie.begin(), lower_cookie.end(),
                 lower_cookie.begin(), [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  std::transform(lower_attribute.begin(), lower_attribute.end(),
                 lower_attribute.begin(), [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return !lower_attribute.empty() &&
         lower_cookie.find(lower_attribute) != std::string::npos;
}

std::string CookieForJNICookieManager(std::string_view cookie_header,
                                      const std::string& domain) {
  if (cookie_header.empty()) {
    return {};
  }
  std::string cookie(cookie_header);
  if (!CookieHasAttribute(cookie, "domain=")) {
    if (!cookie.empty() && cookie.back() != ';') {
      cookie += "; ";
    } else if (!cookie.empty()) {
      cookie += ' ';
    }
    cookie += "Domain=";
    cookie += domain.empty() ? "roblox.com" : domain;
  }
  return cookie;
}

void ApplyAuthStartupDefaults(bool credential_available,
                              bool user_overrode_start_lua_app_dm,
                              bool user_overrode_start_lua_step,
                              bool user_overrode_start_app_step,
                              bool user_overrode_call_start_app) {
  if (credential_available) {
    std::cout << "  [auth] typed Roblox credential ready for native startup\n";
    return;
  }
  if (IsEnabled("MOCKTAIL_ALLOW_NO_COOKIE_LUA_APP")) {
    std::cout << "  [auth] no Roblox cookie found; continuing in no-cookie mode\n"
              << std::flush;
    return;
  }

  std::cerr << "  [auth] no Roblox cookie found; continuing in strict mode\n"
            << std::flush;

  if (!user_overrode_start_lua_app_dm) {
    setenv("MOCKTAIL_START_LUA_APP_DM", "0", 1);
  }
  if (!user_overrode_start_lua_step) {
    setenv("MOCKTAIL_STEP_START_LUA_APP_DM", "0", 1);
  }
  if (!user_overrode_start_app_step) {
    setenv("MOCKTAIL_STEP_START_APP_WITH_PARAMS", "0", 1);
  }
  if (!user_overrode_call_start_app) {
    setenv("MOCKTAIL_CALL_REAL_APP_BRIDGE_START", "0", 1);
  }
}

std::string ResolveClientSettingsJson() {
  mocktail::services::ClientSettingsOptions options;
  options.explicit_json =
      GetEnvString("MOCKTAIL_CLIENT_SETTINGS_JSON", "");
  options.explicit_file =
      GetEnvString("MOCKTAIL_CLIENT_SETTINGS_JSON_FILE", "");
  options.use_bundled = IsEnabled("MOCKTAIL_USE_BUNDLED_CLIENT_SETTINGS");
  options.offline_settings = IsEnabled("MOCKTAIL_OFFLINE_SETTINGS");
  options.fetch = true;
  options.auto_update = true;
  options.application =
      GetEnvString("MOCKTAIL_CLIENT_SETTINGS_APP", "GoogleAndroidApp");
  options.url = GetEnvString("MOCKTAIL_CLIENT_SETTINGS_URL", "");
  options.cache_file = GetEnvString(
      "MOCKTAIL_CLIENT_SETTINGS_CACHE_FILE",
      (MocktailCacheRoot() + "/clientsettings/" + options.application +
       ".json")
          .c_str());

  static mocktail::services::CurlHttpClient http_client;
  static mocktail::services::ClientSettingsService settings_service(
      http_client);
  const mocktail::services::ClientSettingsResult result =
      settings_service.Resolve(options);

  using mocktail::services::ClientSettingsSource;
  switch (result.source) {
    case ClientSettingsSource::kExplicitJson:
      break;
    case ClientSettingsSource::kExplicitFile:
      std::cout << "  [settings] using explicit client settings file "
                << options.explicit_file << " bytes=" << result.json.size()
                << '\n';
      break;
    case ClientSettingsSource::kBundledFile:
      std::cout << "  [settings] using bundled client settings "
                << options.bundled_file << " bytes=" << result.json.size()
                << '\n';
      break;
    case ClientSettingsSource::kSafeDefaults:
      std::cout << "  [settings] using safe inline client settings\n";
      break;
    case ClientSettingsSource::kDownloaded:
      std::cout << "  [settings] flags "
                << (result.cache_updated ? "updated" : "unchanged") << '\n';
      break;
    case ClientSettingsSource::kCache:
      if (!result.error.empty()) {
        std::cerr
            << "  [settings] CDN fetch failed; using cached flags if present: "
            << result.error << '\n';
      }
      std::cout << "  [settings] using cached flags\n";
      break;
    case ClientSettingsSource::kEmptyDefaults:
      if (!result.error.empty()) {
        std::cerr << "  [settings] CDN fetch failed: " << result.error << '\n';
      }
      std::cerr
          << "  [settings] no client settings available; using empty defaults\n";
      break;
  }
  std::cout << std::flush;
  std::cerr << std::flush;
  return result.json;
}

jstring NewClientSettingsString(JNIEnv* env) {
  std::string content = ResolveClientSettingsJson();
  return env->NewStringUTF(content.c_str());
}

void SetObjectField(JNIEnv* env, jobject object, const char* name,
                    const char* signature, jobject value) {
  if (!env || !object || !name || !signature) {
    return;
  }
  jclass object_class = env->GetObjectClass(object);
  jfieldID field_id = env->GetFieldID(object_class, name, signature);
  env->SetObjectField(object, field_id, value);
}

void SetStringField(JNIEnv* env, jobject object, const char* name,
                    const char* value) {
  SetObjectField(env, object, name, "Ljava/lang/String;",
                 env->NewStringUTF(value ? value : ""));
}

void SetJStringField(JNIEnv* env, jobject object, const char* name,
                    jstring value) {
  if (!env || !object || !name) {
    return;
  }
  jclass object_class = env->GetObjectClass(object);
  jfieldID field_id = env->GetFieldID(object_class, name, "Ljava/lang/String;");
  env->SetObjectField(object, field_id, value);
}

void SetIntField(JNIEnv* env, jobject object, const char* name, jint value) {
  if (!env || !object || !name) {
    return;
  }
  jclass object_class = env->GetObjectClass(object);
  jfieldID field_id = env->GetFieldID(object_class, name, "I");
  env->SetIntField(object, field_id, value);
}

void SetLongField(JNIEnv* env, jobject object, const char* name, jlong value) {
  if (!env || !object || !name) {
    return;
  }
  jclass object_class = env->GetObjectClass(object);
  jfieldID field_id = env->GetFieldID(object_class, name, "J");
  env->SetLongField(object, field_id, value);
}

void SetFloatField(JNIEnv* env, jobject object, const char* name,
                   jfloat value) {
  if (!env || !object || !name) {
    return;
  }
  jclass object_class = env->GetObjectClass(object);
  jfieldID field_id = env->GetFieldID(object_class, name, "F");
  env->SetFloatField(object, field_id, value);
}

void SetBooleanField(JNIEnv* env, jobject object, const char* name,
                     jboolean value) {
  if (!env || !object || !name) {
    return;
  }
  jclass object_class = env->GetObjectClass(object);
  jfieldID field_id = env->GetFieldID(object_class, name, "Z");
  env->SetBooleanField(object, field_id, value);
}

void SetRobloxServiceUrlFields(JNIEnv* env, jobject object) {
  if (!env || !object) {
    return;
  }
  constexpr const char* kBaseUrl = "https://www.roblox.com";
  constexpr const char* kApiUrl = "https://apis.roblox.com";
  constexpr const char* kClientSettingsUrl =
      "https://clientsettingscdn.roblox.com/v2/settings-compressed/"
      "application/AndroidApp.zst";
  constexpr const char* kClientSettingsBaseUrl =
      "https://clientsettingscdn.roblox.com";
  constexpr const char* kEcsv2ClientUrl =
      "https://ecsv2.roblox.com/client/pbe";
  constexpr const char* kEcsv2TimespentUrl =
      "https://ecsv2.roblox.com/timespent/pbe";
  constexpr const char* kTelemetryUrl =
      "https://apis.roblox.com/experience-signals-ingest/public/v1/"
      "events/single";

  SetStringField(env, object, "baseUrl", kBaseUrl);
  SetStringField(env, object, "baseURL", kBaseUrl);
  SetStringField(env, object, "wwwBaseUrl", kBaseUrl);
  SetStringField(env, object, "apiBaseUrl", kApiUrl);
  SetStringField(env, object, "apiGatewayUrl", kApiUrl);
  SetStringField(env, object, "clientSettingsUrl", kClientSettingsUrl);
  SetStringField(env, object, "settingsUrl", kClientSettingsUrl);
  SetStringField(env, object, "clientSettingsBaseUrl", kClientSettingsBaseUrl);
  SetStringField(env, object, "clientSettingsHost", kClientSettingsBaseUrl);
  SetStringField(env, object, "ecsv2Url", kEcsv2ClientUrl);
  SetStringField(env, object, "ecsUrl", kEcsv2ClientUrl);
  SetStringField(env, object, "eventStreamUrl", kEcsv2ClientUrl);
  SetStringField(env, object, "telemetryUrl", kTelemetryUrl);
  SetStringField(env, object, "telemetryEndpoint", kTelemetryUrl);
  SetStringField(env, object, "timeSpentUrl", kEcsv2TimespentUrl);
}

jobject NewObject(JNIEnv* env, const char* class_name) {
  if (!env || !class_name) {
    return nullptr;
  }
  jclass clazz = env->FindClass(class_name);
  return env->AllocObject(clazz);
}

void InstallNativeGlJavaImplementation(JNIEnv* env, jclass native_gl_java_class) {
  if (IsDisabled("MOCKTAIL_SET_NATIVE_GL_JAVA_IMPLEMENTATION")) {
    return;
  }
  if (env == nullptr || native_gl_java_class == nullptr) {
    std::cerr << "  [engine] NativeGLJavaInterface.setImplementation skipped: "
              << "missing JNI state\n"
              << std::flush;
    return;
  }

  jobject engine_java_callback =
      NewObject(env, "com/roblox/engine/jni/EngineJavaCallback2");
  if (engine_java_callback == nullptr) {
    std::cerr << "  [engine] NativeGLJavaInterface.setImplementation skipped: "
              << "EngineJavaCallback2 allocation failed\n"
              << std::flush;
    return;
  }

  jmethodID set_implementation = env->GetStaticMethodID(
      native_gl_java_class, "setImplementation",
      "(Lcom/roblox/engine/jni/EngineJavaCallback2;)V");
  if (set_implementation == nullptr) {
    std::cerr << "  [engine] NativeGLJavaInterface.setImplementation skipped: "
              << "method not found\n"
              << std::flush;
    return;
  }

  std::cout << "  [engine] NativeGLJavaInterface.setImplementation\n"
            << std::flush;
  env->CallStaticVoidMethod(native_gl_java_class, set_implementation,
                            engine_java_callback);
  std::cout << "  [engine] NativeGLJavaInterface.setImplementation returned\n"
            << std::flush;
}

jobject BuildPlatformParams(JNIEnv* env, jobject surface);

mocktail::runtime::DisplaySize HostDisplaySize() {
  return mocktail::runtime::ParseDisplaySize(
      std::getenv(mocktail::runtime::kDisplaySizeEnvironment));
}

mocktail::runtime::DisplaySize HostWindowPixelSize() {
  const mocktail::window::WindowViewportSnapshot viewport =
      mocktail::window::GetWindowViewportSnapshot();
  if (viewport.valid()) {
    return {viewport.pixel_width, viewport.pixel_height};
  }
  return mocktail::runtime::ParseDisplaySize(
      std::getenv(mocktail::runtime::kWindowSizeEnvironment));
}

jobject BuildDeviceParams(JNIEnv* env) {
  jobject params = NewObject(env, "com/roblox/engine/jni/model/DeviceParams");
  if (!params) {
    return nullptr;
  }

  const std::string device_name =
      GetEnvString("MOCKTAIL_DEVICE_NAME", "Pixel 7");
  const std::string manufacturer =
      GetEnvString("MOCKTAIL_DEVICE_MANUFACTURER", "Google");
  const std::string device_sku = GetEnvString(
      "MOCKTAIL_DEVICE_SKU",
      std::string(mocktail::compat::kGuestCpuName).c_str());
  const std::string soc_model =
      GetEnvString("MOCKTAIL_DEVICE_SOC_MODEL",
                   std::string(mocktail::compat::kGuestCpuName).c_str());
  SetStringField(env, params, "osVersion", "33");
  SetStringField(env, params, "deviceName", device_name.c_str());
  const std::string app_version =
      GetEnvString("MOCKTAIL_ROBLOX_VERSION", "unknown");
  SetStringField(env, params, "appVersion", app_version.c_str());
  SetStringField(env, params, "country", "US");
  SetStringField(env, params, "manufacturer", manufacturer.c_str());
  const mocktail::runtime::DisplaySize display_size =
      mocktail::runtime::ParseDisplaySize(
          std::getenv(mocktail::runtime::kDisplaySizeEnvironment));
  const std::string display_resolution = std::to_string(display_size.width) +
                                         "x" +
                                         std::to_string(display_size.height);
  SetStringField(env, params, "displayResolution", display_resolution.c_str());
  SetStringField(env, params, "networkType", "wifi");
  SetStringField(env, params, "deviceSku", device_sku.c_str());
  SetStringField(env, params, "socModel", soc_model.c_str());
  SetStringField(env, params, "appBuildVariant", "googleProdRelease");
  SetStringField(env, params, "testDeviceName", device_name.c_str());

  std::uint64_t host_memory_bytes = mocktail::runtime::DetectHostMemoryBytes();
  if (IsEnabled("MOCKTAIL_DISABLE_LOW_RAM_DEVICE")) {
    host_memory_bytes =
        std::max<std::uint64_t>(host_memory_bytes, 4096ULL * 1024ULL * 1024ULL);
  }
  const mocktail::runtime::DeviceMemoryProfile memory =
      mocktail::runtime::BuildDeviceMemoryProfile(host_memory_bytes);
  SetIntField(env, params, "deviceTotalMemoryMB", memory.total_memory_mb);
  SetIntField(env, params, "displayPhysicalWidthPixels", display_size.width);
  SetIntField(env, params, "displayPhysicalHeightPixels",
              display_size.height);
  SetIntField(env, params, "memoryClass", memory.memory_class_mb);
  SetIntField(env, params, "largeMemoryClass", memory.large_memory_class_mb);
  SetLongField(env, params, "lowMemoryKillerBackgroundAppThreshold",
               memory.low_memory_killer_background_threshold);
  SetLongField(env, params, "lowMemoryKillerForegroundAppThreshold",
               memory.low_memory_killer_foreground_threshold);
  SetBooleanField(env, params, "cpu64Bit", JNI_TRUE);
  SetBooleanField(env, params, "isChrome", JNI_FALSE);
  SetBooleanField(env, params, "isLowRamDevice",
                  memory.low_ram_device ? JNI_TRUE : JNI_FALSE);
  return params;
}

jobject BuildAppBridgeInitParams(JNIEnv* env, jstring client_settings,
                                 jstring fast_flags, jstring app_params,
                                 jstring asset_path) {
  jobject params =
      NewObject(env, "com/roblox/engine/jni/autovalue/InitParams");
  if (!params) {
    return nullptr;
  }

  jobject activity = NewObject(env, "com/roblox/client/RobloxActivity");
  jobject context = NewObject(env, "android/content/Context");
  jobject asset_manager = NewObject(env, "android/content/res/AssetManager");
  jobject resources = NewObject(env, "android/content/res/Resources");
  jobject class_loader = NewObject(env, "java/lang/ClassLoader");
  jobject package_manager = NewObject(env, "android/content/pm/PackageManager");
  jobject window = NewObject(env, "android/view/Window");
  jobject display = NewObject(env, "android/view/Display");
  jobject surface = NewObject(env, "android/view/Surface");
  jobject platform_params = BuildPlatformParams(env, surface);
  jobject device_params = BuildDeviceParams(env);
  jobject surface_holder = NewObject(env, "android/view/SurfaceHolder");
  jobject view = NewObject(env, "android/view/View");

  SetObjectField(env, params, "platformParams",
                 "Lcom/roblox/engine/jni/model/PlatformParams;",
                 platform_params);
  jobject start_game_device_params =
      IsEnabled("MOCKTAIL_START_GAME_DEVICE_PARAMS_NON_NULL") ? device_params
                                                             : nullptr;
  SetObjectField(env, params, "deviceParams",
                 "Lcom/roblox/engine/jni/model/DeviceParams;",
                 start_game_device_params);
  SetStringField(env, params, "baseURL", "https://www.roblox.com");
  const std::string user_agent = GetEnvString(
      "MOCKTAIL_USER_AGENT", "Roblox/unknown (Linux; Android 33)");
  SetStringField(env, params, "userAgent", user_agent.c_str());
  SetBooleanField(env, params, "isTablet", JNI_FALSE);
  SetBooleanField(env, params, "isPotato", JNI_FALSE);
  SetBooleanField(env, params, "isVrDevice", JNI_FALSE);
  SetStringField(env, params, "buildVariant", "googleProdRelease");
  SetObjectField(env, params, "vrContext", "Landroid/app/Activity;",
                 activity);

  SetObjectField(env, params, "activity", "Lcom/roblox/client/RobloxActivity;",
                 activity);
  SetObjectField(env, params, "robloxActivity",
                 "Lcom/roblox/client/RobloxActivity;", activity);
  SetObjectField(env, params, "mainActivity",
                 "Lcom/roblox/client/RobloxActivity;", activity);
  SetObjectField(env, params, "context", "Landroid/content/Context;", context);
  SetObjectField(env, params, "applicationContext",
                 "Landroid/content/Context;", context);
  SetObjectField(env, params, "assetManager",
                 "Landroid/content/res/AssetManager;", asset_manager);
  SetObjectField(env, params, "assets",
                 "Landroid/content/res/AssetManager;", asset_manager);
  SetObjectField(env, params, "resources",
                 "Landroid/content/res/Resources;", resources);
  SetObjectField(env, params, "classLoader", "Ljava/lang/ClassLoader;",
                 class_loader);
  SetObjectField(env, params, "packageManager",
                 "Landroid/content/pm/PackageManager;", package_manager);
  SetObjectField(env, params, "window", "Landroid/view/Window;", window);
  SetObjectField(env, params, "display", "Landroid/view/Display;", display);
  SetObjectField(env, params, "surface", "Landroid/view/Surface;", surface);
  SetObjectField(env, params, "surfaceHolder", "Landroid/view/SurfaceHolder;",
                 surface_holder);
  SetObjectField(env, params, "view", "Landroid/view/View;", view);
  SetObjectField(env, params, "decorView", "Landroid/view/View;", view);
  SetObjectField(env, params, "rootView", "Landroid/view/View;", view);

  SetObjectField(env, params, "clientSettingsJson", "Ljava/lang/String;",
                 client_settings);
  SetObjectField(env, params, "clientSettings", "Ljava/lang/String;",
                 client_settings);
  SetObjectField(env, params, "fastFlagsJson", "Ljava/lang/String;",
                 fast_flags);
  SetObjectField(env, params, "fastFlags", "Ljava/lang/String;", fast_flags);
  SetObjectField(env, params, "appParamsJson", "Ljava/lang/String;",
                 app_params);
  SetObjectField(env, params, "appParams", "Ljava/lang/String;", app_params);
  SetObjectField(env, params, "launchParams", "Ljava/lang/String;",
                 app_params);
  SetObjectField(env, params, "assetPath", "Ljava/lang/String;", asset_path);

  SetStringField(env, params, "packageName", "com.roblox.client");
  SetStringField(env, params, "platformName", "Android");
  const std::string init_app_version =
      GetEnvString("MOCKTAIL_ROBLOX_VERSION", "unknown");
  SetStringField(env, params, "appVersion", init_app_version.c_str());
  const std::string init_device_name =
      GetEnvString("MOCKTAIL_DEVICE_NAME", "Pixel 7");
  SetStringField(env, params, "deviceName", init_device_name.c_str());
  SetStringField(env, params, "locale", "en_us");
  SetRobloxServiceUrlFields(env, params);

  const mocktail::runtime::DisplaySize init_display = HostDisplaySize();
  SetIntField(env, params, "screenWidth", init_display.width);
  SetIntField(env, params, "screenHeight", init_display.height);
  SetIntField(env, params, "densityDpi", 160);
  SetIntField(env, params, "sdkVersion", 33);
  SetBooleanField(env, params, "headless", JNI_FALSE);
  SetBooleanField(env, params, "isHeadless", JNI_FALSE);
  SetBooleanField(env, params, "isFirstInstall", JNI_FALSE);
  SetBooleanField(env, params, "isLowMemoryDevice", JNI_FALSE);

  return params;
}

jobject BuildApplicationExitInfoList(JNIEnv* env) {
  jobject list = NewObject(env, "java/util/ArrayList");
  if (!list) {
    list = NewObject(env, "java/util/List");
  }
  return list;
}

jobject BuildPlatformParams(JNIEnv* env, jobject surface) {
  jobject params =
      NewObject(env, "com/roblox/engine/jni/model/PlatformParams");
  if (!params) {
    return nullptr;
  }

  SetObjectField(env, params, "surface", "Landroid/view/Surface;", surface);
  SetStringField(env, params, "platform", "Android");
  const std::string platform_device_name =
      GetEnvString("MOCKTAIL_DEVICE_NAME", "Pixel 7");
  SetStringField(env, params, "deviceName", platform_device_name.c_str());
  SetStringField(env, params, "locale", "en_us");
  SetStringField(env, params, "assetFolderPath", DefaultAssetPath().c_str());
  const mocktail::runtime::DisplaySize platform_window = HostWindowPixelSize();
  const mocktail::runtime::DisplaySize platform_display = HostDisplaySize();
  SetIntField(env, params, "width", platform_window.width);
  SetIntField(env, params, "height", platform_window.height);
  SetIntField(env, params, "screenWidth", platform_display.width);
  SetIntField(env, params, "screenHeight", platform_display.height);
  SetIntField(env, params, "densityDpi", 160);
  SetIntField(env, params, "viewportWidthMm",
              mocktail::runtime::PixelsToMillimetersAt160Dpi(
                  platform_window.width));
  SetIntField(env, params, "viewportHeightMm",
              mocktail::runtime::PixelsToMillimetersAt160Dpi(
                  platform_window.height));
  const mocktail::window::WindowViewportSnapshot viewport =
      mocktail::window::GetWindowViewportSnapshot();
  SetFloatField(env, params, "dpiScale", viewport.dpi_scale);
  SetBooleanField(
      env, params, "isKeyboardDevice",
      IsEnabled("MOCKTAIL_KEYBOARD_ENABLED_INTERNAL") ? JNI_TRUE : JNI_FALSE);
  SetBooleanField(
      env, params, "isMouseDevice",
      IsEnabled("MOCKTAIL_MOUSE_ENABLED_INTERNAL") ? JNI_TRUE : JNI_FALSE);
  SetBooleanField(
      env, params, "isTouchDevice",
      IsEnabled("MOCKTAIL_TOUCH_ENABLED_INTERNAL") ? JNI_TRUE : JNI_FALSE);
  SetBooleanField(env, params, "isLuaHomePageEnabled", JNI_TRUE);
  SetBooleanField(env, params, "isLuaGamesPageEnabled", JNI_TRUE);
  SetBooleanField(env, params, "isLuaChatEnabled", JNI_TRUE);
  SetBooleanField(env, params, "isTablet", JNI_FALSE);
  SetBooleanField(env, params, "headless", JNI_FALSE);
  SetBooleanField(env, params, "isHeadless", JNI_FALSE);
  return params;
}

jobject BuildStartGameParams(JNIEnv* env, jobject platform_params,
                            jobject device_params, jobject surface,
                            jobject vr_context, const char* launch_data_env,
                            const jnivm::RobloxAuthIdentity& identity) {
  std::cout << "  [engine] BuildStartGameParams begin\n" << std::flush;
  if (!env) {
    std::cerr << "  [engine] BuildStartGameParams: env is null\n" << std::flush;
    return nullptr;
  }
  jclass start_game_params_class =
      env->FindClass("com/roblox/engine/jni/autovalue/StartGameParams");
  if (!start_game_params_class) {
    std::cerr << "  [engine] BuildStartGameParams: StartGameParams class not found\n"
              << std::flush;
    return nullptr;
  }
  const jlong place_id = GetEnvLong("MOCKTAIL_PLACE_ID", 0);
  const jlong join_target_user_id =
      GetEnvLong("MOCKTAIL_GAME_JOIN_USER_ID", 0);
  const jlong conversation_id =
      GetEnvLong("MOCKTAIL_GAME_CONVERSATION_ID", 0);
  const jlong referred_by_player_id =
      GetEnvLong("MOCKTAIL_REFERRED_BY_PLAYER_ID", 0);
  const jint join_request_type =
      GetEnvInt("MOCKTAIL_GAME_JOIN_REQUEST_TYPE", -1);
  const std::string access_code_value =
      GetEnvString("MOCKTAIL_GAME_ACCESS_CODE", "");
  const std::string link_code_value =
      GetEnvString("MOCKTAIL_GAME_LINK_CODE", "");
  const std::string reserved_server_access_code_value =
      GetEnvString("MOCKTAIL_GAME_RESERVED_SERVER_ACCESS_CODE", "");
  const std::string call_id_value = GetEnvString("MOCKTAIL_GAME_CALL_ID", "");
  const std::string event_id_value = GetEnvString("MOCKTAIL_GAME_EVENT_ID", "");
  const std::string join_attempt_id_value =
      GetEnvString("MOCKTAIL_GAME_JOIN_ATTEMPT_ID", "");
  const std::string join_attempt_origin_value =
      GetEnvString("MOCKTAIL_GAME_JOIN_ATTEMPT_ORIGIN", "");
  const std::string iso_context_value =
      GetEnvString("MOCKTAIL_GAME_ISO_CONTEXT", "");
  const std::string& username_value = identity.username;
  const std::string launch_data_value =
      GetEnvString(launch_data_env != nullptr ? launch_data_env
                                              : "MOCKTAIL_GAME_PARAMS_JSON",
                   "");
  const std::string game_id_value =
      GetEnvString("MOCKTAIL_GAME_ID", "");
  const std::string referral_page_value =
      GetEnvString("MOCKTAIL_REFERRAL_PAGE", "");
  const std::string game_join_context_value =
      GetEnvString("MOCKTAIL_GAME_JOIN_CONTEXT", "");
  if (EngineTraceEnabled()) {
    std::cout << "  [engine] StartGame params: place_id=" << place_id
              << " join_target_user_id=" << join_target_user_id
              << " game_id=" << game_id_value
              << " join_request_type=" << join_request_type << '\n'
              << std::flush;
  }

  const jstring access_code = env->NewStringUTF(access_code_value.c_str());
  const jstring link_code = env->NewStringUTF(link_code_value.c_str());
  const jstring reserved_server_access_code =
      env->NewStringUTF(reserved_server_access_code_value.c_str());
  const jstring call_id = env->NewStringUTF(call_id_value.c_str());
  const jstring event_id = env->NewStringUTF(event_id_value.c_str());
  const jstring join_attempt_id = env->NewStringUTF(join_attempt_id_value.c_str());
  const jstring join_attempt_origin =
      env->NewStringUTF(join_attempt_origin_value.c_str());
  const jstring iso_context = env->NewStringUTF(iso_context_value.c_str());
  jstring username = env->NewStringUTF(username_value.c_str());
  jstring launch_data = env->NewStringUTF(launch_data_value.c_str());
  jstring game_id = env->NewStringUTF(game_id_value.c_str());
  jstring referral_page = env->NewStringUTF(referral_page_value.c_str());
  jstring game_join_context = env->NewStringUTF(game_join_context_value.c_str());

  jobject params = NewObject(env, "com/roblox/engine/jni/autovalue/StartGameParams");
  if (!params) {
    return nullptr;
  }

  SetObjectField(env, params, "surface", "Landroid/view/Surface;", surface);
  SetObjectField(env, params, "platformParams",
                 "Lcom/roblox/engine/jni/model/PlatformParams;",
                 platform_params);
  SetObjectField(env, params, "deviceParams",
                 "Lcom/roblox/engine/jni/model/DeviceParams;",
                 nullptr);
  SetLongField(env, params, "placeId", place_id);
  SetLongField(env, params, "userId", join_target_user_id);
  SetJStringField(env, params, "accessCode", access_code);
  SetJStringField(env, params, "callId", call_id);
  SetJStringField(env, params, "linkCode", link_code);
  SetJStringField(env, params, "reservedServerAccessCode",
                  reserved_server_access_code);
  SetLongField(env, params, "conversationId", conversation_id);
  SetIntField(env, params, "joinRequestType", join_request_type);
  SetJStringField(env, params, "gameId", game_id);
  SetBooleanField(env, params, "isUnder13", JNI_FALSE);
  SetJStringField(env, params, "username", username);
  SetJStringField(env, params, "referralPage", referral_page);
  SetJStringField(env, params, "launchData", launch_data);
  SetJStringField(env, params, "gameJoinContext", game_join_context);
  SetJStringField(env, params, "eventId", event_id);
  SetJStringField(env, params, "joinAttemptId", join_attempt_id);
  SetJStringField(env, params, "joinAttemptOrigin", join_attempt_origin);
  SetJStringField(env, params, "isoContext", iso_context);
  SetLongField(env, params, "referredByPlayerId", referred_by_player_id);
  SetObjectField(env, params, "vrContext", "Landroid/app/Activity;", vr_context);

  return params;
}

jobject BuildMockSurface(JNIEnv* env) {
  jobject surface = NewObject(env, "android/view/Surface");
  if (!surface) {
    return nullptr;
  }
  const mocktail::runtime::DisplaySize surface_size = HostWindowPixelSize();
  SetIntField(env, surface, "width", surface_size.width);
  SetIntField(env, surface, "height", surface_size.height);
  SetBooleanField(env, surface, "valid", JNI_TRUE);
  SetBooleanField(env, surface, "isValid", JNI_TRUE);
  return surface;
}

jobject BuildConfiguration(JNIEnv* env) {
  return jnivm::CreateAndroidConfiguration(env);
}

// Every path, URL and identity value ConfigureNativeSettings hands to the
// engine. Each one comes from the environment with an Android-shaped
// default, so they are resolved together up front and then passed around as
// a single value instead of thirty separate locals.
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

// Resolves those values, and says once, up front, whether a Roblox
// credential was found -- so the caller can tell a signed-in cold start from
// an anonymous one before it starts calling into the engine.
NativeSettingsValues ResolveNativeSettingsValues(
    const EngineStartupContext* context) {
  std::string data_dir = GetEnvStringDefaultPath(
      "MOCKTAIL_ANDROID_DATA_DIR",
      "/data/user/0/com.roblox.client");
  std::string files_dir = GetEnvStringDefaultPath(
      "MOCKTAIL_ANDROID_FILES_DIR",
      "/data/user/0/com.roblox.client/files");
  std::string settings_cache_dir = GetEnvStringDefaultPath(
      "MOCKTAIL_ANDROID_SETTINGS_CACHE_DIR",
      "/data/user/0/com.roblox.client");
  std::string cache_dir = GetEnvStringDefaultPath(
      "MOCKTAIL_ANDROID_CACHE_DIR",
      "/data/user/0/com.roblox.client/cache");
  std::string external_base = GetEnvStringDefaultPath(
      "MOCKTAIL_ANDROID_EXTERNAL_BASE_DIR",
      "/sdcard/Android/data/com.roblox.client");
  std::string external_dir = GetEnvStringDefaultPath(
      "MOCKTAIL_ANDROID_EXTERNAL_DIR",
      "/sdcard/Android/data/com.roblox.client/files");
  std::string preferences_file = GetEnvString("MOCKTAIL_ANDROID_PREFERENCES_FILE",
                                              "prefs");
  std::string default_policy_file = GetEnvString(
      "MOCKTAIL_DEFAULT_APP_POLICY_FILE",
      "content/guac/defaultConfigs/GuacDefaultPolicy-GlobalDist.json");
  std::string base_url =
      GetEnvString("MOCKTAIL_BASE_URL", "https://www.roblox.com/");
  std::string api_url =
      GetEnvString("MOCKTAIL_API_URL", "https://api.roblox.com/");
  std::string roblox_channel =
      GetEnvString("MOCKTAIL_ROBLOX_CHANNEL", "production");
  std::string channel_platform_name =
      GetEnvString("MOCKTAIL_CHANNEL_PLATFORM_NAME", "GoogleAndroidApp");
  std::string roblox_version =
      GetEnvString("MOCKTAIL_ROBLOX_VERSION", "unknown");
  std::string exception_reason_filename =
      GetEnvString("MOCKTAIL_EXCEPTION_REASON_FILENAME",
                   "exception_reason.txt");
  std::string http_proxy_host =
      GetEnvString("MOCKTAIL_HTTP_PROXY_HOST", "");
  jlong http_proxy_port =
      static_cast<jlong>(GetEnvInt("MOCKTAIL_HTTP_PROXY_PORT", 0));
  std::string cookie_base_url =
      GetEnvString("MOCKTAIL_COOKIE_BASE_URL", "https://www.roblox.com/");
  std::string cookie_domain =
      GetEnvString("MOCKTAIL_COOKIE_DOMAIN", "roblox.com");
  const mocktail::runtime::SecureRobloxCredential* credential =
      context->roblox_credential;
  const std::string_view roblox_cookies =
      credential != nullptr ? credential->view() : std::string_view();
  std::string cookie_manager_cookies =
      CookieForJNICookieManager(roblox_cookies, cookie_domain);
  if (!roblox_cookies.empty()) {
    std::cout << "  [engine] typed credential prepared bytes="
              << roblox_cookies.size() << '\n'
              << std::flush;
  } else if (IsEnabled("MOCKTAIL_ALLOW_NO_COOKIE_LUA_APP")) {
    std::cout << "  [engine] no Roblox cookie found; proceeding without login\n"
              << std::flush;
  } else {
    std::cout << "  [engine] no Roblox credential found; proceeding without login\n"
              << std::flush;
  }
  std::string android_id =
      GetEnvString("MOCKTAIL_ANDROID_ID", "0000000000000000");
  std::string advertising_id =
      GetEnvString("MOCKTAIL_ADVERTISING_ID", "");
  const jnivm::RobloxAuthIdentity& account_identity =
      context->account_identity;
  std::string account_user_id = std::to_string(account_identity.user_id);

  NativeSettingsValues values;
  values.data_dir = data_dir;
  values.files_dir = files_dir;
  values.settings_cache_dir = settings_cache_dir;
  values.cache_dir = cache_dir;
  values.external_base = external_base;
  values.external_dir = external_dir;
  values.preferences_file = preferences_file;
  values.default_policy_file = default_policy_file;
  values.base_url = base_url;
  values.api_url = api_url;
  values.roblox_channel = roblox_channel;
  values.channel_platform_name = channel_platform_name;
  values.roblox_version = roblox_version;
  values.exception_reason_filename = exception_reason_filename;
  values.http_proxy_host = http_proxy_host;
  values.http_proxy_port = http_proxy_port;
  values.cookie_base_url = cookie_base_url;
  values.cookie_domain = cookie_domain;
  values.credential = credential;
  values.roblox_cookies = roblox_cookies;
  values.cookie_manager_cookies = cookie_manager_cookies;
  values.android_id = android_id;
  values.advertising_id = advertising_id;
  values.account_user_id = account_user_id;
  return values;
}

// Lay down every directory the engine expects to already exist. It creates
// some of them itself but not all, and a missing parent makes its own
// boost::filesystem::create_directory fail rather than create, so this has
// to happen before the first native call.
void EnsureNativeSettingsDirectories(const NativeSettingsValues& values) {
  EnsureAndroidDirectory(values.data_dir);
  EnsureAndroidDirectory(values.files_dir);
  EnsureAndroidDirectory(values.settings_cache_dir);
  EnsureAndroidDirectory(values.cache_dir);
  EnsureAndroidDirectory(values.external_base);
  EnsureAndroidDirectory(values.external_dir);
  EnsureAndroidDirectory(values.data_dir + "/rbx-storage");
  EnsureAndroidDirectory(values.data_dir + "/appData");
  EnsureAndroidDirectory(values.data_dir + "/appData/LocalStorage");
  EnsureAndroidDirectory(values.data_dir + "/appData/rbx-storage");
  EnsureAndroidDirectory(values.files_dir + "/rbx-storage");
  EnsureAndroidDirectory(values.files_dir + "/appData");
  EnsureAndroidDirectory(values.files_dir + "/appData/LocalStorage");
  EnsureAndroidDirectory(values.files_dir + "/appData/OTAPatchBackups");
  EnsureAndroidDirectory(values.files_dir + "/appData/rbx-storage");
  EnsureAndroidDirectory(values.cache_dir + "/ContentProvider_2");
  EnsureAndroidDirectory(values.cache_dir + "/rbx-storage");
  EnsureAndroidDirectory(values.cache_dir + "/sounds");
}

void ConfigureNativeSettings(JNIEnv* env, jclass settings_class,
                             const EngineStartupContext* context) {
  if (!env || !settings_class || !context) {
    return;
  }

  // Deliberately not const: the cookie copy is wiped from memory by
  // platform::SecureClear before this function returns.
  NativeSettingsValues values = ResolveNativeSettingsValues(context);

  EnsureNativeSettingsDirectories(values);

  jstring data_dir_string = env->NewStringUTF(values.data_dir.c_str());
  jstring files_dir_string = env->NewStringUTF(values.files_dir.c_str());
  jstring settings_cache_dir_string =
      env->NewStringUTF(values.settings_cache_dir.c_str());
  jstring external_base_string = env->NewStringUTF(values.external_base.c_str());
  jstring external_dir_string = env->NewStringUTF(values.external_dir.c_str());
  jstring preferences_file_string = env->NewStringUTF(values.preferences_file.c_str());
  jstring base_url_string = env->NewStringUTF(values.base_url.c_str());
  jstring api_url_string = env->NewStringUTF(values.api_url.c_str());
  jstring roblox_channel_string = env->NewStringUTF(values.roblox_channel.c_str());
  jstring channel_platform_name_string =
      env->NewStringUTF(values.channel_platform_name.c_str());
  jstring roblox_version_string = env->NewStringUTF(values.roblox_version.c_str());
  jstring exception_reason_filename_string =
      env->NewStringUTF(values.exception_reason_filename.c_str());
  jstring cookie_base_url_string = env->NewStringUTF(values.cookie_base_url.c_str());
  jstring roblox_cookies_string = env->NewStringUTF(
      values.credential != nullptr ? values.credential->c_str() : "");
  auto run_native_setting = [](const char* name, auto call) -> bool {
    if (sigsetjmp(g_native_settings_jmp_buf, 1) == 0) {
      g_native_settings_recovery_name = name;
      g_native_settings_recovery_in_progress = 1;
      call();
      g_native_settings_recovery_in_progress = 0;
      g_native_settings_recovery_name = nullptr;
      return true;
    }
    g_native_settings_recovery_in_progress = 0;
    std::cerr << "  [engine] NativeSettings " << name
              << " recovered from crash\n"
              << std::flush;
    g_native_settings_recovery_name = nullptr;
    return false;
  };

  if (context->native_set_exception_reason_filename) {
    std::cout << "  [engine] NativeSettings exceptionReasonFilename="
              << values.exception_reason_filename << '\n'
              << std::flush;
    run_native_setting("exceptionReasonFilename", [&]() {
      context->native_set_exception_reason_filename(
          env, settings_class, exception_reason_filename_string);
    });
  }
  if (context->native_set_base_url) {
    std::cout << "  [engine] NativeSettings baseUrl=" << values.base_url
              << " apiUrl=" << values.api_url << '\n' << std::flush;
    run_native_setting("baseUrl", [&]() {
      context->native_set_base_url(env, settings_class, base_url_string,
                                   api_url_string);
    });
  }
  if (context->native_set_roblox_channel) {
    std::cout << "  [engine] NativeSettings robloxChannel=" << values.roblox_channel
              << '\n'
              << std::flush;
    run_native_setting("robloxChannel", [&]() {
      context->native_set_roblox_channel(env, settings_class,
                                         roblox_channel_string);
    });
  }
  if (context->native_override_channel_platform_name) {
    std::cout << "  [engine] NativeSettings channelPlatformName="
              << values.channel_platform_name << '\n' << std::flush;
    run_native_setting("channelPlatformName", [&]() {
      context->native_override_channel_platform_name(
          env, settings_class, channel_platform_name_string);
    });
  }
  if (context->native_set_roblox_version) {
    std::cout << "  [engine] NativeSettings robloxVersion=" << values.roblox_version
              << '\n' << std::flush;
    run_native_setting("robloxVersion", [&]() {
      context->native_set_roblox_version(env, settings_class,
                                         roblox_version_string);
    });
  }
  if (context->native_set_device_info) {
    std::cout << "  [engine] NativeSettings deviceInfo\n" << std::flush;
    jobject device_params = BuildDeviceParams(env);
    run_native_setting("deviceInfo", [&]() {
      context->native_set_device_info(env, settings_class, device_params);
    });
  }

  run_native_setting("baseDataDirectories", [&]() {
    context->native_set_base_data_directories(env, settings_class,
                                              data_dir_string,
                                              external_base_string);
  });
  run_native_setting("cacheDirectory", [&]() {
    context->native_set_cache_directory(env, settings_class,
                                        settings_cache_dir_string);
  });
  run_native_setting("filesDirectory", [&]() {
    context->native_set_files_directory(env, settings_class,
                                        files_dir_string);
  });
  run_native_setting("externalDirectory", [&]() {
    context->native_set_external_directory(env, settings_class,
                                           external_dir_string);
  });
  run_native_setting("preferencesFile", [&]() {
    context->native_set_preferences_file(env, settings_class,
                                         preferences_file_string);
  });
  if (context->native_init_fast_log) {
    std::cout << "  [engine] NativeSettings initFastLog\n" << std::flush;
    run_native_setting("initFastLog", [&]() {
      context->native_init_fast_log(env, settings_class);
    });
  }

  if (!values.cookie_manager_cookies.empty() &&
      context->native_cookie_manager_set_cookie == nullptr) {
    std::cout << "  [engine] WARNING: JNICookieManager.setCookie unavailable\n"
              << std::flush;
  }

  if (context->native_set_multiple_cookies && !values.roblox_cookies.empty()) {
    std::cout << "  [engine] NativeSettings multipleCookies base="
              << values.cookie_base_url << " bytes=" << values.roblox_cookies.size() << '\n'
              << std::flush;
    run_native_setting("multipleCookies", [&]() {
      context->native_set_multiple_cookies(env, settings_class,
                                           cookie_base_url_string,
                                           roblox_cookies_string);
    });
    std::cout << "  [engine] NativeSettings multipleCookies returned\n"
              << std::flush;
  }
  mocktail::platform::SecureClear(&values.cookie_manager_cookies);
}

void ConfigureLocalStorage(JNIEnv* env, const EngineStartupContext* context) {
  if (!env || !context) {
    return;
  }
  std::string data_dir = GetEnvStringDefaultPath(
      "MOCKTAIL_ANDROID_DATA_DIR",
      "/data/user/0/com.roblox.client");
  std::string files_dir = GetEnvStringDefaultPath(
      "MOCKTAIL_ANDROID_FILES_DIR",
      "/data/user/0/com.roblox.client/files");
  std::string cache_dir = GetEnvStringDefaultPath(
      "MOCKTAIL_ANDROID_CACHE_DIR",
      "/data/user/0/com.roblox.client/cache");
  EnsureAndroidDirectory(files_dir + "/appData");
  EnsureAndroidDirectory(files_dir + "/appData/LocalStorage");
  EnsureAndroidDirectory(files_dir + "/appData/OTAPatchBackups");
  EnsureAndroidDirectory(files_dir + "/appData/rbx-storage");
  EnsureAndroidDirectory(data_dir + "/rbx-storage");
  EnsureAndroidDirectory(data_dir + "/shared_prefs");
  EnsureAndroidDirectory(cache_dir);
  EnsureAndroidDirectory(cache_dir + "/ContentProvider_2");
  EnsureAndroidDirectory(cache_dir + "/rbx-storage");
  EnsureAndroidDirectory(cache_dir + "/sounds");

  jobject asset_manager = nullptr;
  auto get_asset_manager = [&]() -> jobject {
    if (asset_manager == nullptr) {
      asset_manager = NewObject(env, "android/content/res/AssetManager");
    }
    return asset_manager;
  };

  if (context->native_init_asset_manager) {
    jclass asset_manager_setup_class =
        env->FindClass("com/roblox/client/JNIAAssetManagerSetup");
    std::cout << "  [engine] JNIAAssetManagerSetup.initNative\n"
              << std::flush;
    context->native_init_asset_manager(env, asset_manager_setup_class,
                                       get_asset_manager());
    std::cout << "  [engine] JNIAAssetManagerSetup.initNative returned\n"
              << std::flush;
  }

  if (!context->native_init_storage_manager) {
    return;
  }

  jobject local_storage_manager =
      NewObject(env, "com/roblox/client/LocalStorageManager");
  jstring files_dir_string = env->NewStringUTF(files_dir.c_str());
  jstring cache_dir_string = env->NewStringUTF(cache_dir.c_str());
  std::cout << "  [engine] LocalStorageManager.initStorageManagerNativeV3 files="
            << files_dir << " cache=" << cache_dir << '\n'
            << std::flush;
  context->native_init_storage_manager(env, local_storage_manager,
                                       get_asset_manager(), files_dir_string,
                                       cache_dir_string);
  std::cout << "  [engine] LocalStorageManager.initStorageManagerNativeV3 returned\n"
            << std::flush;
}


// Resolve Android adapters from the runtime bundle even when a launcher
// relocates the executable. Host libraries with the same unversioned SONAME
// do not implement the Android ABI (notably VK_KHR_android_surface).
static std::string RuntimeBionicAdapterPath(const char* name) {
  const char* override_dir = std::getenv("MOCKTAIL_RUNTIME_LIBRARY_DIR");
  std::string directory;
  if (override_dir != nullptr && override_dir[0] != '\0') {
    directory = override_dir;
  } else {
    char executable_path[4097] = {};
    const ssize_t length =
        ::readlink("/proc/self/exe", executable_path,
                   sizeof(executable_path) - 1U);
    if (length <= 0) return {};
    executable_path[length] = '\0';
    std::string path(executable_path);
    const std::string::size_type separator = path.find_last_of('/');
    if (separator == std::string::npos) return {};
    directory = separator == 0 ? "/" : path.substr(0, separator);
  }
  if (directory.empty()) return {};
  return directory + "/" + name;
}


// The JNIEnv for the startup thread, re-acquired if a previous engine call
// left it cleared. Written back through the reference because re-attaching is
// observable to every later step.
JNIEnv* EnsureStartupEnv(EngineStartupContext* context, JNIEnv*& env) {
  if (!env) {
    env = context->vm->GetJNIEnv();
  }
  return env;
}


// Tells the engine what the display can do: first its current refresh rate, then
// optionally the full list of supported rates. Both are optional in their own
// right.
void ReportDisplayRefreshRates(EngineStartupContext* context, JNIEnv*& env,
                            jclass native_gl_class) {
const mocktail::platform::DisplayRefreshCapabilities display_refresh =
    mocktail::window::GetDisplayRefreshCapabilities();
if (context->run_display_refresh_rate && display_refresh.valid() &&
    context->native_pass_current_display_refresh_rate) {
  env = EnsureStartupEnv(context, env);
  std::cout << "  [engine] NativeGLInterface.nativePassCurrentDisplayRefreshRate "
            << display_refresh.current_hz << '\n'
            << std::flush;
  if (sigsetjmp(g_display_refresh_rate_jmp_buf, 1) == 0) {
    g_display_refresh_rate_recovery_in_progress = kStage6RecoveryInline;
    context->native_pass_current_display_refresh_rate(env, native_gl_class,
                                                      display_refresh.current_hz);
    g_display_refresh_rate_recovery_in_progress = kStage6RecoveryInactive;
    std::cout
        << "  [engine] NativeGLInterface.nativePassCurrentDisplayRefreshRate returned\n"
        << std::flush;
  } else {
    g_display_refresh_rate_recovery_in_progress = kStage6RecoveryInactive;
    std::cerr
        << "  [engine] NativeGLInterface.nativePassCurrentDisplayRefreshRate recovered\n"
        << std::flush;
  }
}

if (context->run_display_refresh_rate && display_refresh.valid() &&
    context->native_pass_supported_refresh_rates) {
  env = EnsureStartupEnv(context, env);
  std::cout << "  [engine] NativeGLInterface.nativePassSupportedRefreshRates"
            << " count=" << display_refresh.supported_hz.size() << '\n'
            << std::flush;
  if (sigsetjmp(g_display_refresh_rate_jmp_buf, 1) == 0) {
    g_display_refresh_rate_recovery_in_progress = kStage6RecoveryInline;
    const jsize count =
        static_cast<jsize>(display_refresh.supported_hz.size());
    jfloatArray refresh_rates = env->NewFloatArray(count);
    if (refresh_rates != nullptr && count > 0) {
      env->SetFloatArrayRegion(refresh_rates, 0, count,
                               display_refresh.supported_hz.data());
      context->native_pass_supported_refresh_rates(env, native_gl_class,
                                                   refresh_rates);
    }
    g_display_refresh_rate_recovery_in_progress = kStage6RecoveryInactive;
    std::cout
        << "  [engine] NativeGLInterface.nativePassSupportedRefreshRates returned\n"
        << std::flush;
  } else {
    g_display_refresh_rate_recovery_in_progress = kStage6RecoveryInactive;
    std::cerr
        << "  [engine] NativeGLInterface.nativePassSupportedRefreshRates recovered\n"
        << std::flush;
  }
}
}

// The last two notifications the engine waits on: that the app finished coming
// up, and that the joined experience finished loading. Both are guarded so a
// faulting call still lets startup continue to the next step.
void NotifyAppReadyAndGameLoaded(EngineStartupContext* context, JNIEnv*& env,
                                jclass native_gl_class) {
if (context->native_send_app_ready) {
  env = EnsureStartupEnv(context, env);
  volatile sig_atomic_t send_app_ready_recovered = 0;
  std::cout << "  [engine] nativeAppBridgeV2SendAppEventOnAppReady\n"
            << std::flush;
  if (sigsetjmp(g_send_app_ready_jmp_buf, 1) == 0) {
    g_send_app_ready_recovery_in_progress = kStage6RecoveryInline;
    jstring empty_ready_arg = env->NewStringUTF("");
    jstring home_feature = env->NewStringUTF("Home");
    context->native_send_app_ready(env, native_gl_class, empty_ready_arg,
                                   empty_ready_arg, empty_ready_arg,
                                   home_feature);
    g_send_app_ready_recovery_in_progress = kStage6RecoveryInactive;
  } else {
    send_app_ready_recovered = 1;
    g_send_app_ready_recovery_in_progress = kStage6RecoveryInactive;
    std::cerr
        << "  [engine] nativeAppBridgeV2SendAppEventOnAppReady recovered\n"
        << std::flush;
  }
  if (send_app_ready_recovered == 0) {
    std::cout
        << "  [engine] nativeAppBridgeV2SendAppEventOnAppReady returned\n"
        << std::flush;
  }
}

}

void* EngineStartupThread(void* arg) {
  auto* context = static_cast<EngineStartupContext*>(arg);
  if (context == nullptr) {
    std::cerr << "  [engine] invalid startup context\n" << std::flush;
    return nullptr;
  }
  std::cout << "  [engine] EngineStartupThread entered, context=" << context
            << "\n" << std::flush;
  std::cout << "  [engine] context flags: prepare=" << context->run_prepare_jni
            << " setAsset=" << context->run_set_asset_path
            << " initWithParams=" << context->run_init_with_params << '\n'
            << std::flush;

  EngineLog("thread entered");
  EngineLogPtr("context", context);
  EngineLogPtr("JavaVM", context->java_vm);
  std::cout << "  [engine] thread java_vm=" << context->java_vm
            << " shared_vm=" << context->vm << '\n' << std::flush;
  bool attached_to_thread = false;
  JNIEnv* env = nullptr;
  if (context->java_vm) {
    std::cout << "  [engine] entering AttachCurrentThread\n" << std::flush;
    EngineLog("AttachCurrentThread");
    void* raw_env = nullptr;
    jint attach_result =
        context->java_vm->AttachCurrentThread(&raw_env, nullptr);
    std::cout << "  [engine] AttachCurrentThread result=" << attach_result
              << " raw_env=" << raw_env << '\n' << std::flush;
    if (attach_result == JNI_OK) {
      env = static_cast<JNIEnv*>(raw_env);
      g_stage6_jni_env = reinterpret_cast<uintptr_t>(env);
      attached_to_thread = true;
    }
    EngineLog("AttachCurrentThread returned");
    EngineLogPtr("attached JNIEnv", env);
  }
  if (!env) {
    EngineLog("fallback GetJNIEnv");
  }
  if (!env) {
    env = context->vm->GetJNIEnv();
    g_stage6_jni_env = reinterpret_cast<uintptr_t>(env);
  }
  if (!env) {
    std::cerr << "  [engine] failed to acquire JNIEnv\n";
    return nullptr;
  }
  EngineLogPtr("reset JNIEnv", env);
  EngineLog("PublishCurrentJniEnv");
  PublishCurrentJniEnv(env);

  // Keep EGL unbound so the worker that runs real Roblox surface calls can
  // become the first owner of the context. Binding here makes later worker
  // MakeCurrent fail with EGL_BAD_ACCESS on Mesa/Wayland.
  // JNI_OnLoad replaces env->functions; Stage 6 needs the pseudo-VM table.
  if (env && context->vm) {
    context->vm->RestoreFunctions();
    env = context->vm->GetJNIEnv();
    std::cerr << "  [engine] env->functions restored: " << (void*)env->functions << "\n" << std::flush;
  }
  EngineLogPtr("JNIEnv", env);
  EngineLogPtr("JNIEnv.functions", env ? env->functions : nullptr);

  if (!context->run_prepare_jni) {
    EngineLog("JNI prep disabled");
    if (attached_to_thread) {
      EngineLog("DetachCurrentThread");
      jint detach_result = context->java_vm->DetachCurrentThread();
      if (detach_result != JNI_OK) {
        std::cerr << "  [engine] DetachCurrentThread failed: " << detach_result
                  << '\n' << std::flush;
      }
      attached_to_thread = false;
    }
    return nullptr;
  }

  EngineLog("FindClass NativeGLInterface");
  jclass native_gl_class =
      env->FindClass("com/roblox/engine/jni/NativeGLInterface");
  jclass native_input_class =
      env->FindClass("com/roblox/engine/jni/NativeInputInterface");
  if (!native_input_class) {
    native_input_class = native_gl_class;
  }
  jclass native_settings_class =
      env->FindClass("com/roblox/engine/jni/NativeSettingsInterface");
  jclass native_app_bridge_class =
      env->FindClass("com/roblox/engine/jni/NativeAppBridgeInterface");
  jclass native_gl_java_class =
      env->FindClass("com/roblox/engine/jni/NativeGLJavaInterface");
  if (!native_app_bridge_class) {
    native_app_bridge_class = native_gl_class;
  }
  jclass startup_activity_class =
      env->FindClass("com/roblox/client/startup/MainGameActivity");
  if (!startup_activity_class) {
    startup_activity_class = native_gl_class;
  }
  EngineLog("FindClass returned");
  EngineLog("NewStringUTF args");
  jstring empty_string = env->NewStringUTF("");
  jstring client_settings = NewClientSettingsString(env);
  jstring client_settings_overrides =
      NewStringFromEnvDefault(env, "MOCKTAIL_CLIENT_SETTINGS_OVERRIDES_JSON",
                              "{}");
  jstring client_settings_signature =
      NewStringFromEnvDefault(env, "MOCKTAIL_CLIENT_SETTINGS_SIGNATURE", "");
  jstring client_settings_group =
      NewStringFromEnvDefault(env, "MOCKTAIL_CLIENT_SETTINGS_GROUP",
                              "GoogleAndroidApp");
  jstring fast_flags =
      NewStringFromEnvDefault(env, "MOCKTAIL_FAST_FLAGS_JSON", "{}");
  const char* launch_mode = std::getenv("MOCKTAIL_LAUNCH_MODE");
  const char* default_launch_mode = "normal";
  std::string default_app_params =
      std::string("{\"launchMode\":\"") + (launch_mode != nullptr &&
                                                  launch_mode[0] != '\0'
                                              ? launch_mode
                                              : default_launch_mode) +
      "\",\"placeId\":0}";
  jstring app_params = NewStringFromEnvDefault(
      env, "MOCKTAIL_APP_PARAMS_JSON", default_app_params.c_str());
  EngineLogPtr("JNIEnv.functions", env ? env->functions : nullptr);
  jstring asset_path =
      env->NewStringUTF(GetEnvStringDefaultPath("MOCKTAIL_ASSET_PATH",
                                                DefaultAssetPath())
                            .c_str());
  std::string asset_path_value = JStringToString(env, asset_path);
  if (asset_path_value.empty()) {
    asset_path_value = DefaultAssetPath();
    asset_path = env->NewStringUTF(asset_path_value.c_str());
  }
  if (IsEnabled("MOCKTAIL_TRACE")) {
    std::cout << "  [engine] asset path prepared: " << asset_path_value
              << '\n'
              << std::flush;
  }
  jobject surface = BuildMockSurface(env);
  jobject game_activity = NewObject(env, "com/roblox/client/startup/MainGameActivity");
  jobject game_surface_activity = nullptr;
  jobject app_bridge_notification_listener =
      NewObject(env,
                "com/roblox/engine/jni/OnAppBridgeNotificationListener");
  jobject game_activity_asset_manager =
      NewObject(env, "android/content/res/AssetManager");
  jobject game_activity_config = BuildConfiguration(env);
  jobject platform_params = BuildPlatformParams(env, surface);
  jobject device_params = BuildDeviceParams(env);
  const jnivm::RobloxAuthIdentity& account_identity =
      context->account_identity;
  jobject start_game_params =
      BuildStartGameParams(env, platform_params, device_params, surface,
                           game_activity, "MOCKTAIL_GAME_PARAMS_JSON",
                           account_identity);
  InstallNativeGlJavaImplementation(env, native_gl_java_class);
  EngineLog("NewStringUTF returned");

  jlong game_activity_handle = 0;
  bool game_activity_init_attempted = false;
  auto run_game_activity_initialize = [&]() -> jlong {
    if (!context->run_game_activity_init ||
        context->native_game_activity_init == nullptr) {
      return 0;
    }
    if (game_activity_init_attempted) {
      return game_activity_handle;
    }
    game_activity_init_attempted = true;
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] GameActivity.initializeNativeCode\n"
              << std::flush;
    jstring internal_data_dir =
        env->NewStringUTF("/data/user/0/com.roblox.client/files");
    jstring obb_dir =
        env->NewStringUTF("/sdcard/Android/obb/com.roblox.client");
    jstring external_data_dir =
        env->NewStringUTF("/sdcard/Android/data/com.roblox.client/files");
    if (sigsetjmp(g_game_activity_init_jmp_buf, 1) == 0) {
      g_game_activity_init_recovery_in_progress = 1;
      g_saved_game_activity = game_activity;
      game_activity_handle = context->native_game_activity_init(
          env, game_activity, internal_data_dir, obb_dir, external_data_dir,
          game_activity_asset_manager, nullptr, game_activity_config);
      g_game_activity_init_recovery_in_progress = 0;
    } else {
      g_game_activity_init_recovery_in_progress = 0;
      game_activity_handle = 0;
      std::cerr << "  [engine] GameActivity.initializeNativeCode recovered\n"
                << std::flush;
    }
    g_game_activity_native_handle =
        static_cast<uintptr_t>(game_activity_handle);
    std::cout << "  [engine] GameActivity.initializeNativeCode returned "
              << game_activity_handle << '\n'
              << std::flush;
    if (game_activity_handle != 0 &&
        IsEnabled("MOCKTAIL_DUMP_GAME_ACTIVITY_HANDLE")) {
      auto** game_activity_slots = reinterpret_cast<void**>(
          static_cast<uintptr_t>(game_activity_handle));
      std::cout << "  [engine] GameActivity handle slots:";
      for (int i = 0; i < 40; ++i) {
        std::cout << " [" << i << "]=" << game_activity_slots[i];
      }
      std::cout << '\n' << std::flush;
    }
    return game_activity_handle;
  };

  if (context->run_game_activity_init) {
    run_game_activity_initialize();
  }

  if (context->run_set_asset_path) {
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] nativeSetAssetPath\n" << std::flush;
    if (context->call_real_set_asset_path) {
      if (sigsetjmp(g_set_asset_path_jmp_buf, 1) == 0) {
        g_set_asset_path_recovery_in_progress = 1;
        if (EngineTraceEnabled()) {
          std::cerr << "  [engine] nativeSetAssetPath env=" << (void*)env
                    << " class=" << (void*)startup_activity_class
                    << " path=" << asset_path_value << '\n'
                    << std::flush;
        }
        context->native_set_asset_path(env, startup_activity_class, asset_path);
        g_set_asset_path_recovery_in_progress = 0;
      } else {
        std::cerr << "  [engine] nativeSetAssetPath recovered\n" << std::flush;
        MocktailSetAssetPath(env, asset_path);
      }
    } else {
      MocktailSetAssetPath(env, asset_path);
    }
    std::cout << "  [engine] nativeSetAssetPath returned\n" << std::flush;
  }

  if (context->run_native_settings) {
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] NativeSettings directories\n" << std::flush;
    ConfigureNativeSettings(env, native_settings_class, context);
    ConfigureLocalStorage(env, context);
    if (context->native_base_url_protocol_init != nullptr) {
      jclass base_url_protocol_class =
          env->FindClass("com/roblox/universalapp/linking/JNIBaseUrlProtocol");
      std::cout << "  [engine] JNIBaseUrlProtocol.init\n" << std::flush;
      context->native_base_url_protocol_init(env, base_url_protocol_class,
                                             game_activity);
      std::cout << "  [engine] JNIBaseUrlProtocol.init returned\n"
                << std::flush;
    }
    if (context->native_web_login_protocol_init != nullptr) {
      // Binds CookieProtocol so the engine can write .ROBLOSECURITY itself.
      // This is the whole native sign-in path: no WebView is involved.
      jclass web_login_protocol_class =
          env->FindClass("com/roblox/universalapp/linking/JNIWebLoginProtocol");
      std::cout << "  [engine] JNIWebLoginProtocol.init\n" << std::flush;
      context->native_web_login_protocol_init(env, web_login_protocol_class,
                                              game_activity);
      std::cout << "  [engine] JNIWebLoginProtocol.init returned\n"
                << std::flush;
      // On Android the launch Intent reaches WebLoginProtocol through the
      // Java linking layer. There is none here, so hand the raw roblox://
      // argument straight to the engine, which decides for itself whether it
      // carries a web-login ticket.
      if (context->native_web_login_cold_start != nullptr &&
          context->raw_launch_uri != nullptr &&
          !context->raw_launch_uri->empty()) {
        jstring launch_uri = env->NewStringUTF(context->raw_launch_uri->c_str());
        const jboolean consumed = launch_uri != nullptr
                                      ? context->native_web_login_cold_start(
                                            env, web_login_protocol_class,
                                            launch_uri)
                                      : JNI_FALSE;
        if (launch_uri != nullptr) {
          env->DeleteLocalRef(launch_uri);
        }
        std::cout << "  [engine] WebLoginProtocol cold start "
                  << (consumed == JNI_TRUE ? "consumed" : "not a login link")
                  << "\n"
                  << std::flush;
      }
    }
    std::cout << "  [engine] NativeSettings directories returned\n"
              << std::flush;
  }

  if (context->run_global_init) {
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] nativeGameGlobalInit\n" << std::flush;
    if (sigsetjmp(g_game_global_init_jmp_buf, 1) == 0) {
      g_game_global_init_recovery_in_progress = 1;
      context->native_global_init(env, native_gl_class);
      g_game_global_init_recovery_in_progress = 0;
    } else {
      g_game_global_init_recovery_in_progress = 0;
      const char msg[] = "  [engine] nativeGameGlobalInit recovered\n";
      write(2, msg, sizeof(msg) - 1);
    }
    const char msg[] = "  [engine] nativeGameGlobalInit returned\n";
    write(1, msg, sizeof(msg) - 1);
  }

  if (context->native_update_adapter_init &&
      !IsDisabled("MOCKTAIL_UPDATE_ADAPTER_INIT")) {
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] nativeUpdateAdapterInit\n" << std::flush;
    context->native_update_adapter_init(env, native_gl_class);
    std::cout << "  [engine] nativeUpdateAdapterInit returned\n" << std::flush;
  }

  if (context->run_init_client_settings) {
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] nativeInitClientSettings\n" << std::flush;
    if (sigsetjmp(g_init_client_settings_jmp_buf, 1) == 0) {
      g_init_client_settings_recovery_in_progress = 1;
      const char* variant = std::getenv("MOCKTAIL_INIT_CLIENT_SETTINGS_VARIANT");
      if (variant == nullptr || variant[0] == '\0') {
        variant = "classic";
      }
      jint settings_result = 0;
      const jlong client_settings_timestamp_seconds =
          static_cast<jlong>(time(nullptr));
      if (std::strcmp(variant, "compressed") == 0 &&
          context->native_init_client_settings_cached_compressed != nullptr) {
        jbyteArray empty_compressed_settings = env->NewByteArray(0);
        settings_result =
            context->native_init_client_settings_cached_compressed(
                env, native_gl_class, empty_compressed_settings,
                client_settings_overrides, client_settings_group, empty_string,
                client_settings_timestamp_seconds, JNI_FALSE);
      } else if (std::strcmp(variant, "cached") == 0 &&
                 context->native_init_client_settings_cached != nullptr) {
        settings_result = context->native_init_client_settings_cached(
            env, native_gl_class, client_settings, client_settings_overrides,
            client_settings_group, empty_string,
            client_settings_timestamp_seconds);
      } else if (std::strcmp(variant, "signed") == 0 &&
                 context->native_init_client_settings_signed != nullptr) {
        settings_result = context->native_init_client_settings_signed(
            env, native_gl_class, client_settings, client_settings_signature,
            client_settings_overrides, client_settings_group);
      } else if (context->native_init_client_settings != nullptr) {
        variant = "classic";
        settings_result = context->native_init_client_settings(
            env, native_gl_class, client_settings, client_settings_overrides,
            client_settings_group);
      } else {
        std::cerr << "  [engine] nativeInitClientSettings has no usable "
                  << "variant\n"
                  << std::flush;
      }
      g_init_client_settings_recovery_in_progress = 0;
      std::cout << "  [engine] nativeInitClientSettings returned "
                << settings_result << " via " << variant << '\n'
                << std::flush;
      if (context->run_native_settings) {
        std::cout << "  [engine] reapplying NativeSettings after "
                  << "nativeInitClientSettings\n"
                  << std::flush;
        ConfigureNativeSettings(env, native_settings_class, context);
      }
    } else {
      g_init_client_settings_recovery_in_progress = 0;
      std::cerr << "  [engine] nativeInitClientSettings recovered\n" << std::flush;
    }
  }

  if (native_gl_java_class && app_bridge_notification_listener) {
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] NativeGLJavaInterface."
              << "setAppBridgeNotificationListener\n"
              << std::flush;
    jmethodID set_listener = env->GetStaticMethodID(
        native_gl_java_class, "setAppBridgeNotificationListener",
        "(Lcom/roblox/engine/jni/OnAppBridgeNotificationListener;)V");
    env->CallStaticVoidMethod(native_gl_java_class, set_listener,
                              app_bridge_notification_listener);
    std::cout << "  [engine] NativeGLJavaInterface."
              << "setAppBridgeNotificationListener returned\n"
              << std::flush;
  }

  if (context->run_init_with_params) {
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] nativeAppBridgeV2InitWithParams\n" << std::flush;
    if (context->call_real_init_with_params) {
      jobject init_params =
          BuildAppBridgeInitParams(env, client_settings, fast_flags, app_params,
                                   asset_path);
      if (sigsetjmp(g_init_with_params_jmp_buf, 1) == 0) {
        g_init_with_params_recovery_in_progress = 1;
        context->native_init_with_params(env, native_gl_class, init_params);
        g_init_with_params_recovery_in_progress = 0;
      } else {
std::cerr << "  [engine] nativeAppBridgeV2InitWithParams recovered\n"
                  << std::flush;
        MocktailAppBridgeInit(env, app_params);
      }
    } else {
      MocktailAppBridgeInit(env, app_params);
    }
    std::cout << "  [engine] nativeAppBridgeV2InitWithParams returned\n"
              << std::flush;
    g_main_thread_message_pump_ready.store(1);
    if (IsEnabled("MOCKTAIL_TRACE")) {
      std::cerr << "  [engine] nativeCallMessagesFromMainThread pump ready\n"
                << std::flush;
    }
  }

  if (context->run_game_activity_init && context->native_game_activity_init) {
    env = EnsureStartupEnv(context, env);
    run_game_activity_initialize();
  }

  if (context->run_app_lifecycle_active &&
      context->native_app_lifecycle_set_active) {
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] JNIAppLifecycleNativeAdapter.setActive\n"
              << std::flush;
    context->native_app_lifecycle_set_active(env, native_gl_class);
    std::cout << "  [engine] JNIAppLifecycleNativeAdapter.setActive returned\n"
              << std::flush;
  }

  if (context->run_native_fragment_start &&
      context->native_on_fragment_start) {
    env = EnsureStartupEnv(context, env);
    std::cout << "  [engine] NativeGLInterface.nativeOnFragmentStart\n"
              << std::flush;
    if (sigsetjmp(g_native_fragment_start_jmp_buf, 1) == 0) {
      g_native_fragment_start_recovery_in_progress = kStage6RecoveryInline;
      context->native_on_fragment_start(env, native_gl_class);
      g_native_fragment_start_recovery_in_progress = kStage6RecoveryInactive;
      std::cout << "  [engine] NativeGLInterface.nativeOnFragmentStart returned\n"
                << std::flush;
    } else {
      g_native_fragment_start_recovery_in_progress = kStage6RecoveryInactive;
      std::cerr << "  [engine] NativeGLInterface.nativeOnFragmentStart recovered\n"
                << std::flush;
    }
  }

  ReportDisplayRefreshRates(context, env, native_gl_class);




  if (context->native_set_task_scheduler_background_mode) {
    env = EnsureStartupEnv(context, env);
    if (!RunTaskSchedulerForegroundOnMainThread(
            context->native_set_task_scheduler_background_mode,
            native_gl_class)) {
      InvokeTaskSchedulerForeground(
          env, native_gl_class,
          context->native_set_task_scheduler_background_mode, "engine");
    }
  }

  if (context->run_start_app_with_params) {
	    env = EnsureStartupEnv(context, env);
	    std::cout << "  [engine] nativeAppBridgeV2StartAppWithParams\n"
	              << std::flush;
    MocktailAppBridgeStart(env, app_params);
    std::cout << "  [engine] nativeAppBridgeV2StartAppWithParams returned\n"
              << std::flush;
  }


  if (context->game_session_runtime != nullptr) {
    env = EnsureStartupEnv(context, env);
    mocktail::runtime::GameSessionPrincipal principal;
    principal.kind = context->account_identity.user_id > 0
                         ? mocktail::runtime::GameSessionPrincipalKind::
                               kAuthenticated
                         : mocktail::runtime::GameSessionPrincipalKind::
                               kLocalGuest;
    principal.generation = 1;
    principal.principal_id = context->account_identity.user_id > 0
                                 ? std::to_string(
                                       context->account_identity.user_id)
                                 : std::string();
    principal.base_url = "https://www.roblox.com/";
    std::string launch_parameters =
        GetEnvString("MOCKTAIL_GAME_PARAMS_JSON", "{}");
    if (launch_parameters.empty()) {
      launch_parameters = "{}";
    }
    mocktail::runtime::GameJoinRequest request{
        1, GetEnvLong("MOCKTAIL_PLACE_ID", 0), std::move(launch_parameters)};
    const mocktail::window::WindowSurfaceSnapshot window_surface =
        mocktail::window::GetWindowSurfaceSnapshot();
    if (!window_surface.available) {
      std::cerr << "  [game-session] initial typed window surface is "
                   "unavailable\n";
      return nullptr;
    }
    mocktail::runtime::GameSurface game_surface{
        window_surface.generation, window_surface.native_window,
        window_surface.width, window_surface.height, window_surface.dpi_scale};
    mocktail::runtime::RobloxGameSessionBinding binding{
        {native_gl_class, surface, platform_params, game_surface_activity,
         start_game_params},
        principal,
        request,
        game_surface};
    const mocktail::Status status =
        context->game_session_runtime->InitializeAndStart(
            binding, std::move(principal), std::move(request), game_surface);
    if (!status.ok()) {
      std::cerr << "  [game-session] typed lifecycle startup failed: "
                << status.message() << '\n'
                << std::flush;
      return nullptr;
    }
    std::cout << "  [game-session] typed lifecycle startup completed: "
              << mocktail::runtime::GameSessionStateName(
                     context->game_session_runtime->Snapshot().state)
              << '\n'
              << std::flush;
  }

  NotifyAppReadyAndGameLoaded(context, env, native_gl_class);

  int keepalive_ms = GetEnvInt("MOCKTAIL_KEEPALIVE_MS", 0);
  if (keepalive_ms > 0) {
    std::cout << "  [engine] keepalive: " << keepalive_ms << " ms\n"
              << std::flush;
    usleep(static_cast<useconds_t>(keepalive_ms) * 1000);
    std::cout << "  [engine] keepalive returned\n" << std::flush;
  }

  if (IsEnabled("MOCKTAIL_KEEPALIVE")) {
    std::cout << "  [engine] keepalive: forever\n" << std::flush;
    while (true) {
      sleep(1);
    }
  }

  if (attached_to_thread) {
    EngineLog("DetachCurrentThread");
    jint detach_result = context->java_vm->DetachCurrentThread();
    if (detach_result != JNI_OK) {
      std::cerr << "  [engine] DetachCurrentThread failed: " << detach_result
                << '\n' << std::flush;
    }
    attached_to_thread = false;
  }

  return nullptr;
}

// The names libroblox.so does not import dynamically, so they cannot come from
// RegisterUndefinedElfImports and this table is the only thing that registers
// them. That is the compatibility surface Mocktail exists to provide: guest
// layouts such as pthread and FILE that have to come from the Bionic shims
// rather than from glibc, plus the Android exports the synthetic libraries
// publish. Run resolves each name against the stubs, the host GL library and
// the window, then registers whichever answer it got.
//
// Every other name the guest needs is resolved from its own .dynsym, which is
// why this list is short and cannot drift from the binary.
constexpr const char* kSymbolsToRegister[] = {
    // Symbols exported by the Android compatibility stubs.
    "AAsset_getLength64",
    "AAsset_getRemainingLength",
    "AAsset_getRemainingLength64",
    "AAsset_openFileDescriptor64",
    "AAsset_read",
    "AAsset_seek",
    "AAsset_seek64",
    "alCreateEngine",
    "eglBindAPI",
    "eglQueryString",
    "vkCreateAndroidSurfaceKHR",
    "vkCreateInstance",
    "vkDestroySurfaceKHR",
    "vkEnumerateInstanceExtensionProperties",
    "vkEnumerateInstanceLayerProperties",
    "vkGetDeviceProcAddr",
    "vkGetInstanceProcAddr",
    "statfs",
    "__poll_chk",
    // stdio stream pointers — Bionic clients access these as FILE* globals
    "slCreateEngine",
    "SL_IID_ANDROIDCONFIGURATION",
    "SL_IID_ANDROIDSIMPLEBUFFERQUEUE",
    "SL_IID_BUFFERQUEUE",
    "SL_IID_ENGINE",
    "SL_IID_PLAY",
    "SL_IID_RECORD",
    "SL_IID_VOLUME",
    "mocktail_recover_stack_chk_fail",

    // pthread functions whose host ABI is already compatible.
    "pthread_kill",

    // Standard C library functions resolved from host libc.
    "malloc",
    "calloc",
    "realloc",
    "free",
    "setenv",
    "putenv",
    "sprintf",
    "vprintf",
    "vsprintf",
    "fgetc",
    "rewind",
    "lseek64",
    "pwrite64",
    "chdir",
    "dup",
    "dup2",
    "send",
    "recv",
    "htons",
    "htonl",
    "ntohs",
    "ntohl",
    "clock_getres",
    "usleep",
    "sleep",
    "munlock",
    "kill",
    "getgid",
    "getegid",
    "isatty",
    "isalpha",
    "isdigit",
    "isupper",
    "islower",
    "toupper",
    "rand_r",
    "abs",
    "labs",
    "llabs",
    "ceil",
    "floor",
    "fabs",
    "sqrt",
    "perror",
    "pthread_barrier_init",
    "pthread_barrier_destroy",
    "pthread_barrier_wait",
    "pthread_spin_init",
    "pthread_spin_destroy",
    "pthread_spin_lock",
    "pthread_spin_trylock",
    "pthread_spin_unlock",
};


// The Vulkan entry points the adapter can stand in for. Only addresses the
// adapter itself owns are registered, so anything that resolves into the
// real driver still reaches the driver.
constexpr const char* kVulkanAdapterExports[] = {
    "vkCreateAndroidSurfaceKHR",
    "vkCreateDevice",
    "vkCreateInstance",
    "vkCreateSwapchainKHR",
    "vkDestroyDevice",
    "vkDestroyInstance",
    "vkDestroySurfaceKHR",
    "vkDestroySwapchainKHR",
    "vkEnumerateInstanceExtensionProperties",
    "vkEnumerateInstanceLayerProperties",
    "vkGetDeviceProcAddr",
    "vkGetDeviceQueue",
    "vkGetDeviceQueue2",
    "vkGetInstanceProcAddr",
    "vkQueuePresentKHR",
};

}  // namespace legacy_internal

// The startup code below is one step of one orchestrator and reads these names
// hundreds of times; qualifying each one would bury the logic.
using namespace legacy_internal;

// Every engine entry point the startup sequence can call, resolved once up
// front. Keeping them in one struct means the startup context is wired by
// name rather than by counting aggregate arguments, and there is a single
// place to see which parts of the engine surface this client uses.
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

// The first seven come from the caller's capability resolution; the rest are
// looked up directly so a missing optional symbol degrades to a null entry
// point instead of failing the whole startup.
RobloxEngineSymbols ResolveRobloxEngineSymbols(
    void* roblox_handle,
    const mocktail::runtime::RobloxStartupSymbols& startup_symbols,
    jnivm::VM* jni_vm) {
  RobloxEngineSymbols symbols;
  symbols.global_init =
      startup_symbols.game_global_init;
  symbols.update_adapter_init =
      startup_symbols.update_adapter_init;
  symbols.init_with_params =
      startup_symbols.app_bridge_v2_init_with_params;
  symbols.start_lua_app_dm =
      startup_symbols.app_bridge_start_lua_app_dm;
  symbols.start_app_with_params =
      startup_symbols.app_bridge_v2_start_app_with_params;
  symbols.update_surface_app =
      startup_symbols.app_bridge_v2_update_surface_app_with_platform_params;
  symbols.call_messages_from_main_thread =
      startup_symbols.call_messages_from_main_thread;

  symbols.init_client_settings =
      reinterpret_cast<NativeInitClientSettingsFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeGLInterface_"
          "nativeInitClientSettings"));
  symbols.init_client_settings_signed =
      reinterpret_cast<NativeInitClientSettingsSignedFn>(
          linker::ResolveSymbol(
              roblox_handle,
              "Java_com_roblox_engine_jni_NativeGLInterface_"
              "nativeInitClientSettingsSigned"));
  symbols.init_client_settings_cached =
      reinterpret_cast<NativeInitClientSettingsCachedFn>(
          linker::ResolveSymbol(
              roblox_handle,
              "Java_com_roblox_engine_jni_NativeGLInterface_"
              "nativeInitClientSettingsCached"));
  symbols.init_client_settings_cached_compressed =
      reinterpret_cast<NativeInitClientSettingsCachedCompressedFn>(
          linker::ResolveSymbol(
              roblox_handle,
              "Java_com_roblox_engine_jni_NativeGLInterface_"
              "nativeInitClientSettingsCachedCompressed"));
  symbols.post_client_settings =
      reinterpret_cast<NativePostClientSettingsFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeGLInterface_"
          "nativePostClientSettingsLoadedInitialization3"));
  symbols.initialize_native_flags =
      reinterpret_cast<NativeInitializeNativeFlagsFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_client_flags_FlagJniInterface_"
          "nativeInitializeNativeFlags"));
  symbols.app_bridge_app_start =
      reinterpret_cast<NativeAppBridgeAppStartFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeAppBridgeInterface_"
          "nativeAppBridgeAppStart__Ljava_lang_String_2Ljava_lang_String_2Z"
          "Ljava_lang_String_2Ljava_lang_String_2Ljava_lang_String_2"));
  symbols.set_is_first_install =
      reinterpret_cast<NativeSetIsFirstInstallFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeAppBridgeInterface_"
          "setIsFirstInstall"));
  symbols.set_base_url =
      reinterpret_cast<NativeSetBaseUrlFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetBaseUrl"));
  symbols.set_device_info =
      reinterpret_cast<NativeObjectInitFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetDeviceInfo"));
  symbols.base_url_protocol_init =
      reinterpret_cast<NativeObjectInitFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_universalapp_linking_JNIBaseUrlProtocol_init"));
  symbols.web_login_protocol_init =
      reinterpret_cast<NativeObjectInitFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_universalapp_linking_JNIWebLoginProtocol_init"));
  symbols.web_login_cold_start =
      reinterpret_cast<NativeColdStartUriFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_universalapp_linking_"
          "JNIWebLoginProtocol_maybeHandleColdStartProtocolLaunch"));
  symbols.set_roblox_channel =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetRobloxChannel"));
  symbols.override_channel_platform_name =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeOverrideChannelPlatformName"));
  symbols.set_roblox_version =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetRobloxVersion"));
  symbols.set_exception_reason_filename =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetExceptionReasonFilename"));
  symbols.set_base_data_directories =
      reinterpret_cast<NativeSetTwoStringParamsFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetBaseDataDirectories"));
  symbols.set_cache_directory =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetCacheDirectory"));
  symbols.set_files_directory =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetFilesDirectory"));
  symbols.set_external_directory =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetExternalDirectory"));
  symbols.set_preferences_file =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetPreferencesFile"));
  symbols.set_default_app_policy_file =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetDefaultAppPolicyFile"));
  symbols.set_http_client_proxy =
      reinterpret_cast<NativeSetHttpClientProxyFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetHttpClientProxy"));
  symbols.init_fast_log =
      reinterpret_cast<NativeNoArgFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeInitFastLog"));
  symbols.set_multiple_cookies =
      reinterpret_cast<NativeSetTwoStringParamsFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetMultipleCookies"));
  symbols.get_cookies_for_domain =
      reinterpret_cast<jnivm::RobloxCookieGetter>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeGetCookiesForDomain"));
  jni_vm->SetRobloxCookieGetter(symbols.get_cookies_for_domain);
  symbols.cookie_manager_set_cookie =
      reinterpret_cast<NativeSetTwoStringParamsFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_universalapp_cookie_JNICookieManager_setCookie"));
  symbols.set_platform_headers_with_idfa =
      reinterpret_cast<NativeSetThreeStringParamsFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetPlatformHeadersWithIdfa"));
  symbols.set_user_id =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeSettingsInterface_"
          "nativeSetUserId"));
  symbols.init_asset_manager =
      reinterpret_cast<NativeObjectInitFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_client_JNIAAssetManagerSetup_initNative"));
  symbols.init_storage_manager =
      reinterpret_cast<NativeInitStorageManagerFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_client_LocalStorageManager_"
          "initStorageManagerNativeV3"));
  symbols.local_storage_set_platform_impl =
      reinterpret_cast<NativeSetPlatformImplFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_protocols_localstorageplatforminterface_"
          "generated_ILocalStorageHandlerCore_setPlatformImpl"));
  symbols.set_init_params =
      reinterpret_cast<NativeAppBridgeSetInitParamsFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_client_startup_MainGameActivity_"
          "nativeAppBridgeSetInitParams"));
  symbols.retry_init =
      reinterpret_cast<NativeNoArgFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_client_startup_MainGameActivity_"
          "nativeRetryInit"));
  symbols.update_screen_orientation =
      reinterpret_cast<NativeUpdateScreenOrientationFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeInputInterface_"
          "nativeUpdateScreenOrientation"));
  symbols.update_app_ui_sizes =
      reinterpret_cast<NativeUpdateAppUiSizesFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeGLInterface_updateAppUISizes"));
  symbols.on_fragment_start =
      reinterpret_cast<NativeNoArgFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeGLInterface_"
          "nativeOnFragmentStart"));
  symbols.pass_current_display_refresh_rate =
      reinterpret_cast<NativePassCurrentDisplayRefreshRateFn>(
          linker::ResolveSymbol(
              roblox_handle,
              "Java_com_roblox_engine_jni_NativeGLInterface_"
              "nativePassCurrentDisplayRefreshRate"));
  symbols.pass_supported_refresh_rates =
      reinterpret_cast<NativePassSupportedRefreshRatesFn>(
          linker::ResolveSymbol(
              roblox_handle,
              "Java_com_roblox_engine_jni_NativeGLInterface_"
              "nativePassSupportedRefreshRates"));
  symbols.set_task_scheduler_background_mode =
      reinterpret_cast<NativeSetTaskSchedulerBackgroundModeFn>(
          linker::ResolveSymbol(
              roblox_handle,
              "Java_com_roblox_engine_jni_NativeGLInterface_"
              "setTaskSchedulerBackgroundMode"));
  symbols.send_app_ready =
      reinterpret_cast<NativeSendAppReadyFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeGLInterface_"
          "nativeAppBridgeV2SendAppEventOnAppReady"));
  symbols.send_game_loaded =
      reinterpret_cast<NativeSendGameLoadedFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_engine_jni_NativeGLInterface_"
          "nativeAppBridgeV2SendAppEventOnGameLoaded"));
  symbols.set_asset_path =
      reinterpret_cast<NativeSetStringParamFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_client_startup_MainGameActivity_"
          "nativeSetAssetPath"));
  auto resolve_activity_lifecycle =
      [&](const char* method_name) -> NativeActivityLifecycleStringFn {
    std::string symbol =
        "Java_com_roblox_universalapp_activitylifecyclecallbacks_"
        "JNIActivityLifecycleCallbacks_";
    symbol += method_name;
    return reinterpret_cast<NativeActivityLifecycleStringFn>(
        linker::ResolveSymbol(roblox_handle, symbol.c_str()));
  };
  symbols.activity_lifecycle = {
      resolve_activity_lifecycle("nativeOnPreCreated"),
      resolve_activity_lifecycle("nativeOnCreated"),
      resolve_activity_lifecycle("nativeOnPostCreated"),
      resolve_activity_lifecycle("nativeOnPreStarted"),
      resolve_activity_lifecycle("nativeOnStarted"),
      resolve_activity_lifecycle("nativeOnPostStarted"),
      resolve_activity_lifecycle("nativeOnPreResumed"),
      resolve_activity_lifecycle("nativeOnResumed"),
      resolve_activity_lifecycle("nativeOnPostResumed"),
  };
  symbols.game_activity_init =
      reinterpret_cast<NativeGameActivityInitFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_google_androidgamesdk_GameActivity_initializeNativeCode"));
  symbols.app_lifecycle_set_active =
      reinterpret_cast<NativeNoArgFn>(linker::ResolveSymbol(
          roblox_handle,
          "Java_com_roblox_universalapp_applifecyclenativeadapter_"
          "JNIAppLifecycleNativeAdapter_setActive"));
  return symbols;
}

// Which engine startup steps this run will actually call. Real startup calls
// run by default; only signal recovery falls back to the host
// implementations, and the "call real" flags stay opt-in where the engine
// call can block before rendering starts.
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

StartupDecisions ResolveStartupDecisions(
    bool has_window, const RobloxEngineSymbols& symbols) {
  StartupDecisions decisions;
  // Every step used to be tunable through MOCKTAIL_STEP_* and friends, with
  // ApplyRuntimeDefaults filling the rest. The values below are what those
  // gates evaluated to with nothing set; the flags are gone, so the
  // defaults are the behavior. Beware CachedGetenv: reads before
  // ApplyRuntimeDefaults freeze, so MOCKTAIL_START_LUA_APP_DM and
  // MOCKTAIL_CALL_REAL_APP_BRIDGE_START evaluate false here even though
  // defaults exist for them later.
  decisions.run_set_asset_path = true;
  decisions.call_real_set_asset_path = true;
  decisions.run_global_init = true;
  decisions.run_init_client_settings = true;
  decisions.run_post_client_settings = false;
  decisions.run_app_bridge_app_start = false;
  decisions.run_init_with_params = true;
  decisions.call_real_init_with_params = true;
  decisions.run_update_screen_orientation = false;
  // MOCKTAIL_STEP_GAME_ACTIVITY_INIT defaults to "0", which disables this
  // even in windowed runs. That default is load-bearing, not an override.
  decisions.run_game_activity_init = false;
  decisions.run_set_init_params = false;
  decisions.run_app_lifecycle_active = false;
  decisions.run_native_fragment_start =
      has_window && symbols.on_fragment_start != nullptr;
  decisions.run_display_refresh_rate =
      has_window && (symbols.pass_current_display_refresh_rate != nullptr ||
                     symbols.pass_supported_refresh_rates != nullptr);
  decisions.run_start_app_with_params = true;
  decisions.call_real_start_app_with_params = false;
  decisions.run_start_lua_app_dm = false;
  decisions.run_native_settings = true;
  decisions.run_prepare_jni = true;
  return decisions;
}

// Pumps SDL until the window closes. Each tick drains typed window-surface
// events, runs the engine's deferred main-thread work, and delivers
// platform events and dynamic launch requests. Clears
// *game_surface_events_completed when a typed event or a launch request
// fails, so the caller aborts the run instead of spinning on a broken
// bridge.
void RunMainLoop(
    bool* game_surface_events_completed,
    mocktail::runtime::RobloxGameSessionRuntime* game_session_runtime,
    mocktail::runtime::RobloxExperienceComposition* experience_composition) {
  std::unique_ptr<mocktail::window::WindowGameSurfaceBridge> surface_bridge;
  if (game_session_runtime != nullptr) {
    surface_bridge =
        std::make_unique<mocktail::window::WindowGameSurfaceBridge>(
            mocktail::window::MakeWindowGameSurfaceEventSource(),
            mocktail::window::MakeWindowGameSurfaceConsumer(
                game_session_runtime),
            mocktail::window::MakeWindowResizeReadinessCommitObserver());
  } else if (experience_composition != nullptr &&
             experience_composition->subscribed()) {
    mocktail::window::WindowGameSurfaceConsumer experience_consumer{
        experience_composition, &ExperienceSurfaceCreated,
        &ExperienceSurfaceChanged, &ExperienceSurfaceDestroyed};
    surface_bridge =
        std::make_unique<mocktail::window::WindowGameSurfaceBridge>(
            mocktail::window::MakeWindowGameSurfaceEventSource(),
            experience_consumer,
            mocktail::window::MakeWindowResizeReadinessCommitObserver());
  }
  const auto drain_game_surface_events = [&]() {
    if (surface_bridge == nullptr) {
      return true;
    }
    const mocktail::window::WindowGameSurfaceDrainResult result =
        surface_bridge->Drain();
    if (!result.ok()) {
      std::cerr << "[FATAL] Typed GAME surface bridge rejected an event: "
                << result.status.message() << '\n';
      return false;
    }
    if (result.drained_events != 0) {
      const mocktail::window::WindowGameSurfaceBridgeSnapshot snapshot =
          surface_bridge->Snapshot();
      std::cout << "  [game-session] typed window surface events committed="
                << result.drained_events
                << " last_generation=" << snapshot.last_generation << '\n'
                << std::flush;
    }
    return true;
  };
  int main_loop_ticks = 0;
  const bool fps_trace = IsEnabled("MOCKTAIL_TRACE");
  const bool trace_main_loop = IsEnabled("MOCKTAIL_TRACE");
  uint64_t fps_window_start_ns = fps_trace ? MonotonicNanos() : 0;
  uint64_t fps_samples = 0;
  uint64_t fps_tick_ns = 0;
  uint64_t fps_sdl_ns = 0;
  uint64_t fps_platform_ns = 0;
  uint64_t fps_engine_ns = 0;
  uint64_t fps_launch_ns = 0;
  uint64_t fps_pace_sleep_ns = 0;
  while (true) {
    const uint64_t fps_tick_start_ns = fps_trace ? MonotonicNanos() : 0;
    const bool keep_window_open = mocktail::window::PumpEvents();
    *game_surface_events_completed = drain_game_surface_events();
    const uint64_t fps_after_sdl_ns = fps_trace ? MonotonicNanos() : 0;
    if (!game_surface_events_completed || !keep_window_open) {
      break;
    }
    ++main_loop_ticks;
    if (trace_main_loop &&
        (main_loop_ticks <= 10 || main_loop_ticks % 100 == 0)) {
      std::cerr << "  [main] SDL event loop tick #" << main_loop_ticks
                << '\n'
                << std::flush;
    }
    RunPendingMainThreadTaskSchedulerForeground();
    if (g_pending_main_thread_start_lua_app_dm &&
        !g_pending_main_thread_start_lua_started &&
        g_vm_for_main_thread_pump &&
        g_native_gl_class_for_main_thread &&
        MonotonicMillis() >= g_pending_main_thread_start_lua_due_ms) {
      g_pending_main_thread_start_lua_started = true;
      JNIEnv* env = AttachMainThreadJniEnv();
      if (env == nullptr) {
        continue;
      }
      std::cout << "  [main] delayed nativeAppBridgeStartLuaAppDM\n"
                << std::flush;
      g_pending_main_thread_start_lua_app_dm(
          env, g_native_gl_class_for_main_thread);
      std::cout << "  [main] delayed nativeAppBridgeStartLuaAppDM returned\n"
                << std::flush;
    }
    if (experience_composition != nullptr) {
      const mocktail::Status platform_event_status =
          experience_composition->DrainPlatformEvents();
      if (!platform_event_status.ok()) {
        std::cerr << "[FATAL] Roblox platform event delivery failed: "
                  << platform_event_status.message() << '\n';
        *game_surface_events_completed = false;
        break;
      }
    }
    const uint64_t fps_after_platform_ns =
        fps_trace ? MonotonicNanos() : 0;
    PumpRobloxMainThreadMessagesOnce();
    const uint64_t fps_after_engine_ns = fps_trace ? MonotonicNanos() : 0;
    if (experience_composition != nullptr &&
        experience_composition->subscribed()) {
      const mocktail::Status launch_status =
          experience_composition->DrainLaunchRequests();
      if (!launch_status.ok()) {
        std::cerr << "[FATAL] Dynamic experience launch failed: "
                  << launch_status.message() << '\n';
        *game_surface_events_completed = false;
        break;
      }
    }
    const uint64_t fps_after_launch_ns = fps_trace ? MonotonicNanos() : 0;
    const uint64_t fps_pace_ns = mocktail::window::PaceInputPump();
    if (fps_trace) {
      const uint64_t fps_tick_end_ns = MonotonicNanos();
      ++fps_samples;
      fps_tick_ns += fps_tick_end_ns - fps_tick_start_ns;
      fps_sdl_ns += fps_after_sdl_ns - fps_tick_start_ns;
      fps_platform_ns += fps_after_platform_ns - fps_after_sdl_ns;
      fps_engine_ns += fps_after_engine_ns - fps_after_platform_ns;
      fps_launch_ns += fps_after_launch_ns - fps_after_engine_ns;
      fps_pace_sleep_ns += fps_pace_ns;
      if (fps_tick_end_ns - fps_window_start_ns >= 1000000000ULL &&
          fps_samples != 0) {
        std::fprintf(stderr,
                     "  [fps] main n=%llu tick=%llu us sdl=%llu us "
                     "drain=%llu us engine=%llu us launch=%llu us "
                     "pace_sleep=%llu us\n",
                     static_cast<unsigned long long>(fps_samples),
                     static_cast<unsigned long long>(fps_tick_ns /
                                                     fps_samples / 1000ULL),
                     static_cast<unsigned long long>(fps_sdl_ns /
                                                     fps_samples / 1000ULL),
                     static_cast<unsigned long long>(fps_platform_ns /
                                                     fps_samples / 1000ULL),
                     static_cast<unsigned long long>(fps_engine_ns /
                                                     fps_samples / 1000ULL),
                     static_cast<unsigned long long>(fps_launch_ns /
                                                     fps_samples / 1000ULL),
                     static_cast<unsigned long long>(fps_pace_sleep_ns /
                                                     fps_samples / 1000ULL));
        fps_window_start_ns = fps_tick_end_ns;
        fps_samples = 0;
        fps_tick_ns = 0;
        fps_sdl_ns = 0;
        fps_platform_ns = 0;
        fps_engine_ns = 0;
        fps_launch_ns = 0;
        fps_pace_sleep_ns = 0;
      }
    }
  }
}

// Dumps every resolved engine entry point as an address. Run once after
// resolution so a startup failure can be attributed to a specific symbol.
struct EngineSymbolDump {
  const char* label;
  const void* address;
};

void ReportResolvedEngineSymbols(const RobloxEngineSymbols& symbols) {
  // The concrete signature differs per symbol and does not matter here.
  const auto as = [](auto entry) {
    return reinterpret_cast<const void*>(entry);
  };
  const EngineSymbolDump dump[] = {
      {"native_init_with_params", as(symbols.init_with_params)},
      {"native_init_client_settings", as(symbols.init_client_settings)},
      {"native_init_client_settings_signed",
       as(symbols.init_client_settings_signed)},
      {"native_init_client_settings_cached",
       as(symbols.init_client_settings_cached)},
      {"native_init_client_settings_cached_compressed",
       as(symbols.init_client_settings_cached_compressed)},
      {"native_initialize_native_flags", as(symbols.initialize_native_flags)},
      {"native_app_bridge_app_start", as(symbols.app_bridge_app_start)},
      {"native_set_is_first_install", as(symbols.set_is_first_install)},
      {"native_update_adapter_init", as(symbols.update_adapter_init)},
      {"native_update_app_ui_sizes", as(symbols.update_app_ui_sizes)},
      {"native_update_surface_app", as(symbols.update_surface_app)},
      {"native_start_app_with_params", as(symbols.start_app_with_params)},
      {"native_send_app_ready", as(symbols.send_app_ready)},
      {"native_send_game_loaded", as(symbols.send_game_loaded)},
      {"native_start_lua_app_dm", as(symbols.start_lua_app_dm)},
      {"native_call_messages_from_main_thread",
       as(symbols.call_messages_from_main_thread)},
      {"native_base_url_protocol_init", as(symbols.base_url_protocol_init)},
      {"native_set_device_info", as(symbols.set_device_info)},
      {"native_override_channel_platform_name",
       as(symbols.override_channel_platform_name)},
      {"native_set_roblox_version", as(symbols.set_roblox_version)},
      {"native_set_http_client_proxy", as(symbols.set_http_client_proxy)},
      {"native_init_fast_log", as(symbols.init_fast_log)},
      {"native_set_multiple_cookies", as(symbols.set_multiple_cookies)},
      {"native_cookie_manager_set_cookie",
       as(symbols.cookie_manager_set_cookie)},
      {"native_set_platform_headers_with_idfa",
       as(symbols.set_platform_headers_with_idfa)},
      {"native_local_storage_set_platform_impl",
       as(symbols.local_storage_set_platform_impl)},
      {"native_retry_init", as(symbols.retry_init)},
      {"native_set_asset_path", as(symbols.set_asset_path)},
      {"activity_on_pre_created",
       as(symbols.activity_lifecycle.on_pre_created)},
      {"activity_on_created", as(symbols.activity_lifecycle.on_created)},
      {"activity_on_post_created",
       as(symbols.activity_lifecycle.on_post_created)},
      {"activity_on_pre_started",
       as(symbols.activity_lifecycle.on_pre_started)},
      {"activity_on_started", as(symbols.activity_lifecycle.on_started)},
      {"activity_on_post_started",
       as(symbols.activity_lifecycle.on_post_started)},
      {"activity_on_pre_resumed",
       as(symbols.activity_lifecycle.on_pre_resumed)},
      {"activity_on_resumed", as(symbols.activity_lifecycle.on_resumed)},
      {"activity_on_post_resumed",
       as(symbols.activity_lifecycle.on_post_resumed)},
      {"native_game_activity_init", as(symbols.game_activity_init)},
      {"native_app_lifecycle_set_active", as(symbols.app_lifecycle_set_active)},
      {"native_on_fragment_start", as(symbols.on_fragment_start)},
      {"native_pass_supported_refresh_rates",
       as(symbols.pass_supported_refresh_rates)},
      {"native_pass_current_display_refresh_rate",
       as(symbols.pass_current_display_refresh_rate)},
  };
  for (const EngineSymbolDump& entry : dump) {
    std::cout << "    " << entry.label << '=' << entry.address << '\n';
  }
}

// Creates the host window, exports the display and viewport sizes the
// engine reads through the environment, and resolves the theme. Returns
// whether a usable window exists.
bool InitializeHostWindow(
    const mocktail::runtime::RuntimeConfig& runtime_config,
    const mocktail::legacy::RuntimeDependencies& dependencies,
    bool user_overrode_main_thread_message_pump,
    bool user_overrode_call_start_app) {
  // Create SDL/EGL before loading Android stubs so SDL binds the host graphics
  // backend. The window stays hidden until the first real frame.
  bool window_initialised = false;
  {
    const int win_w = runtime_config.window().width;
    const int win_h = runtime_config.window().height;
    const char* win_title = runtime_config.window().title.c_str();
    std::cout << "  [window] Creating " << win_w << "x" << win_h
              << " window...\n"
              << std::flush;
    window_initialised = mocktail::window::Init(win_w, win_h, win_title);
    if (!window_initialised) {
      if (IsEnabled("MOCKTAIL_OFFLINE_SETTINGS")) {
        std::cerr << "  [window] FATAL: Roblox requires a working SDL video "
                  << "device. Check DISPLAY/WAYLAND_DISPLAY "
                  << "and SDL3.\n"
                  << std::flush;
        return EXIT_FAILURE;
      }
      std::cerr
          << "  [window] WARNING: window init failed; continuing without a "
             "window\n"
          << std::flush;
    } else {
      std::cout << "  [window] Window ready; waiting for Roblox frames\n"
                << std::flush;
    }
  }
  if (window_initialised) {
    auto* sdl_window =
        static_cast<SDL_Window*>(mocktail::window::GetBackendWindow());
    const SDL_DisplayID display =
        sdl_window != nullptr ? SDL_GetDisplayForWindow(sdl_window) : 0;
    const SDL_DisplayMode* mode =
        display != 0 ? SDL_GetCurrentDisplayMode(display) : nullptr;
    if (mode != nullptr && mode->w > 0 && mode->h > 0) {
      // SDL mode dimensions need pixel_density to become physical pixels.
      const float density = mode->pixel_density > 0.0f ? mode->pixel_density
                                                       : 1.0f;
      const std::string display_size =
          std::to_string(std::lround(mode->w * density)) + "x" +
          std::to_string(std::lround(mode->h * density));
      setenv(mocktail::runtime::kDisplaySizeEnvironment, display_size.c_str(),
             1);
      std::cout << "  [window] host display " << display_size << " pixels\n"
                << std::flush;
    }
  }
  if (window_initialised) {
    const mocktail::window::WindowViewportSnapshot viewport =
        mocktail::window::GetWindowViewportSnapshot();
    if (viewport.valid()) {
      const std::string window_size = std::to_string(viewport.pixel_width) +
                                      "x" +
                                      std::to_string(viewport.pixel_height);
      setenv(mocktail::runtime::kWindowSizeEnvironment, window_size.c_str(),
             1);
    }
  }
  const SDL_SystemTheme system_theme =
      window_initialised ? SDL_GetSystemTheme() : SDL_SYSTEM_THEME_UNKNOWN;
  const bool system_dark_theme = system_theme == SDL_SYSTEM_THEME_DARK;
  const char* app_storage_file =
      std::getenv("MOCKTAIL_APP_STORAGE_FILE_INTERNAL");
  bool dark_theme = runtime_config.theme_mode() != "light";
  bool roblox_theme_found = false;
  if (runtime_config.theme_mode() == "system") {
    dark_theme = system_dark_theme;
  } else if (runtime_config.theme_mode() == "roblox" &&
             app_storage_file != nullptr) {
    const mocktail::runtime::RobloxThemeCacheResult saved_theme =
        mocktail::runtime::ReadRobloxThemeCache(
            app_storage_file, dependencies.account_identity().user_id);
    if (!saved_theme) {
      std::cerr << "[FATAL] Cannot read Roblox theme cache: "
                << saved_theme.error << '\n';
      return EXIT_FAILURE;
    }
    if (saved_theme.dark_theme.has_value()) {
      dark_theme = *saved_theme.dark_theme;
      roblox_theme_found = true;
    }
  }
  setenv("MOCKTAIL_RESOLVED_THEME_INTERNAL", dark_theme ? "Dark" : "Light", 1);
  if (runtime_config.theme_mode() == "roblox") {
    std::cout << "  [theme] "
              << (roblox_theme_found ? "Roblox saved theme="
                                     : "Roblox theme missing; default=")
              << (dark_theme ? "dark" : "light") << '\n'
              << std::flush;
  } else if (app_storage_file != nullptr) {
    std::string theme_error;
    if (!mocktail::runtime::ApplyRobloxThemeCacheOverride(
            app_storage_file, dependencies.account_identity().user_id,
            dark_theme, &theme_error)) {
      std::cerr << "[FATAL] Roblox theme cache override failed: " << theme_error
                << '\n';
      return EXIT_FAILURE;
    }
    std::cout << "  [theme] Roblox local theme="
              << (dark_theme ? "dark" : "light") << '\n'
              << std::flush;
  }
  const bool has_window = window_initialised;
  if (!has_window) {
    std::cout << "  [window] GUI-bound startup steps disabled (no SDL window)\n"
              << std::flush;
  } else {
    setenv("MOCKTAIL_MAIN_THREAD_MESSAGE_PUMP", "1", 1);
    std::cout << "  [window] windowed startup: auto-enabled main-thread "
              << "message pump\n"
              << std::flush;
  }

  return has_window;
}

// The readiness evidence Run accumulates while it runs, so the final verdict can be
// stated without re-deriving anything. Each flag is cleared by the step that failed;
// the ones with no failure path start out set.
struct RunCompletionState {
  bool real_frame_presented = false;
  bool lifecycle_shutdown_completed = false;
  bool game_surface_events_completed = true;
  bool input_shutdown_completed = true;
  bool resize_readiness_completed = true;
};

// Stops resize readiness and shuts audio down ahead of the SDL teardown, recording
// whether both completed. Audio that refuses to release the device fails the run
// rather than tearing SDL down underneath it.
void FinishWindowShutdown(mocktail::legacy::RuntimeDependencies& dependencies,
                          RunCompletionState* completion) {
  const mocktail::Status resize_stop_status =
      completion->lifecycle_shutdown_completed
          ? mocktail::window::StopResizeReadiness()
          : mocktail::window::ResizeReadinessCompletionStatus();
  const mocktail::Status resize_completion_status =
      mocktail::window::ResizeReadinessCompletionStatus();
  completion->resize_readiness_completed =
      resize_stop_status.ok() && resize_completion_status.ok();
  if (!completion->resize_readiness_completed) {
    const mocktail::Status& failure =
        resize_stop_status.ok() ? resize_completion_status
                                : resize_stop_status;
    std::cerr << "[FATAL] Real resize readiness failed: "
              << failure.message() << '\n';
  }
  const mocktail::Status audio_shutdown_status =
      dependencies.ShutdownBeforePlatform();
  if (!audio_shutdown_status.ok()) {
    std::cerr << "  [main] audio shutdown blocked SDL teardown: "
              << audio_shutdown_status.message() << '\n';
    completion->lifecycle_shutdown_completed = false;
  } else {
    mocktail::window::Shutdown();
  }
}

// Reports whether the run reached what it claims to have reached: a real presented
// frame, a completed Roblox lifecycle shutdown, and no rejected surface, input or
// resize step. Reports the first failure and returns false.
bool ValidateRunCompletion(const RunCompletionState& completion) {
  if (!completion.real_frame_presented) {
    std::cerr << "[FATAL] Windowed runtime exited without a real presented "
                 "frame.\n";
    return false;
  }
  if (!completion.lifecycle_shutdown_completed) {
    std::cerr << "[FATAL] Windowed runtime exited without completing the "
                 "Roblox lifecycle shutdown.\n";
    return false;
  }
  if (!completion.game_surface_events_completed) {
    std::cerr << "[FATAL] Windowed runtime rejected a typed GAME surface "
                 "lifecycle event.\n";
    return false;
  }
  if (!completion.input_shutdown_completed) {
    std::cerr << "[FATAL] Windowed runtime exited without completing typed input shutdown.\n";
    return false;
  }
  if (!completion.resize_readiness_completed) {
    std::cerr << "[FATAL] Windowed runtime exited without completing the real "
                 "resize/rebind readiness sequence.\n";
    return false;
  }
  return true;
}

// Assembles the context the engine startup thread runs against: the engine
// handles, the launch inputs, and each startup-step decision paired with the
// entry point it drives. Field order has to match EngineStartupContext.
EngineStartupContext BuildEngineStartupContext(
    jnivm::VM* vm,
    JavaVM* java_vm,
    const mocktail::runtime::CommandLineOptions& options,
    const mocktail::legacy::RuntimeDependencies& dependencies,
    mocktail::runtime::RobloxGameSessionRuntime* game_session_runtime,
    const StartupDecisions& decisions,
    const RobloxEngineSymbols& symbols) {
  return EngineStartupContext{
    vm,
    java_vm,
    &options.engine_launch_uri,
    dependencies.account_identity(),
    &dependencies.roblox_credential(),
    game_session_runtime,
    decisions.run_prepare_jni,
    decisions.run_set_asset_path,
    decisions.call_real_set_asset_path,
    decisions.run_global_init,
    decisions.run_init_client_settings,
    decisions.run_post_client_settings,
    decisions.run_app_bridge_app_start,
    decisions.run_native_settings,
    decisions.run_set_init_params,
    decisions.run_init_with_params,
    decisions.call_real_init_with_params,
    decisions.run_update_screen_orientation,
    decisions.run_start_app_with_params,
    decisions.call_real_start_app_with_params,
    decisions.run_game_activity_init,
    decisions.run_app_lifecycle_active,
    decisions.run_native_fragment_start,
    decisions.run_display_refresh_rate,
    decisions.run_start_lua_app_dm,
    symbols.global_init,
    symbols.init_client_settings,
    symbols.init_client_settings_signed,
    symbols.init_client_settings_cached,
    symbols.init_client_settings_cached_compressed,
    symbols.post_client_settings,
    symbols.initialize_native_flags,
    symbols.app_bridge_app_start,
    symbols.set_is_first_install,
    symbols.set_base_url,
    symbols.set_device_info,
    symbols.base_url_protocol_init,
    symbols.web_login_protocol_init,
    symbols.web_login_cold_start,
    symbols.set_roblox_channel,
    symbols.override_channel_platform_name,
    symbols.set_roblox_version,
    symbols.set_exception_reason_filename,
    symbols.set_base_data_directories,
    symbols.set_cache_directory,
    symbols.set_files_directory,
    symbols.set_external_directory,
    symbols.set_preferences_file,
    symbols.set_default_app_policy_file,
    symbols.set_http_client_proxy,
    symbols.init_fast_log,
    symbols.set_multiple_cookies,
    symbols.cookie_manager_set_cookie,
    symbols.set_platform_headers_with_idfa,
    symbols.set_user_id,
    symbols.init_asset_manager,
    symbols.init_storage_manager,
    symbols.local_storage_set_platform_impl,
    symbols.set_init_params,
    symbols.retry_init,
    symbols.init_with_params,
    symbols.update_adapter_init,
    symbols.update_screen_orientation,
    symbols.set_task_scheduler_background_mode,
    symbols.start_app_with_params,
    symbols.send_app_ready,
    symbols.set_asset_path,
    symbols.game_activity_init,
    symbols.app_lifecycle_set_active,
    symbols.on_fragment_start,
    symbols.pass_supported_refresh_rates,
    symbols.pass_current_display_refresh_rate,
    symbols.start_lua_app_dm};
}

// Runs the engine startup sequence, returning only once it has completed. The
// default path puts it on a pthread so the main thread keeps pumping;
// MOCKTAIL_ENGINE_INLINE calls it directly. MOCKTAIL_ENGINE_DETACH is refused
// because a worker whose runtime ownership cannot be proven cannot be unwound.
int RunEngineStartup(EngineStartupContext* startup_context) {
  if (IsEnabled("MOCKTAIL_ENGINE_DETACH")) {
    std::cerr << "[FATAL] Detached startup workers are unsupported because "
                 "their runtime ownership cannot be proven\n";
    return EXIT_FAILURE;
  }
  if (IsEnabled("MOCKTAIL_ENGINE_INLINE")) {
    std::cout << "  [engine] starting inline startup path\n" << std::flush;
    EngineStartupThread(startup_context);
  } else {
    std::cout << "  [engine] starting pthread startup path\n" << std::flush;
    std::cout << "  [engine] creating startup thread\n" << std::flush;
    mocktail::runtime::OwnedPthread startup_thread;
    const int create_result = startup_thread.Start(
        &EngineStartupThread, startup_context, GetEngineStackSize());
    std::cout << "  [engine] startup thread create result=" << create_result
              << '\n' << std::flush;
    if (create_result != 0) {
      std::cerr << "\n[FATAL] Could not create engine startup thread: "
                << create_result << '\n';
      return EXIT_FAILURE;
    }

    const int startup_timeout_ms = 0;
    if (startup_timeout_ms <= 0) {
      std::cout << "  [engine] joining startup thread\n" << std::flush;
    } else {
      std::cout << "  [engine] joining startup thread with timeout "
                << startup_timeout_ms << "ms\n"
                << std::flush;
    }

    const mocktail::runtime::OwnedPthreadWaitResult wait_result =
        startup_thread.WaitFor(startup_timeout_ms > 0 ? startup_timeout_ms
                                                      : -1,
                               10, &PumpStartupOwnerThread);
    if (!wait_result.joined()) {
      if (wait_result.status ==
          mocktail::runtime::OwnedPthreadWaitStatus::kTimedOut) {
        std::cerr << "  [engine] startup thread timed out after "
                  << startup_timeout_ms
                  << "ms, cancelling and requiring a physical join\n"
                  << std::flush;
        for (int dump_index = 0; dump_index < 3; ++dump_index) {
          startup_thread.Signal(SIGUSR1);
          usleep(100 * 1000);
        }
      } else {
        std::cerr << "  [engine] startup thread join failed: "
                  << wait_result.platform_error << '\n'
                  << std::flush;
      }

      const int cancel_join_grace_ms = 5000;
      const mocktail::runtime::OwnedPthreadCancelResult cancel_result =
          startup_thread.CancelAndJoinFor(
              cancel_join_grace_ms, 10, &PumpStartupOwnerThread);
      if (cancel_result.cancel_error != 0) {
        std::cerr << "  [engine] startup thread cancel failed: "
                  << cancel_result.cancel_error << '\n'
                  << std::flush;
      }
      std::cerr << "[FATAL] Startup worker did not complete normally; "
                << cancel_join_grace_ms << "ms cancel/join grace result: "
                << mocktail::runtime::OwnedPthreadWaitStatusName(
                       cancel_result.wait.status)
                << " error=" << cancel_result.wait.platform_error
                << ". Native rollback is unproven even after a physical "
                   "join; terminating without RAII unwind\n"
                << std::flush;
      std::_Exit(EXIT_FAILURE);
    }
  }
  return EXIT_SUCCESS;
}

// The two bridges the direct GAME path installs. Grouped so the install
// step and the shutdown step that tears them down agree on one name, and
// so neither half can be dropped without the other.
struct GamePlatformProtocols {
  std::unique_ptr<mocktail::runtime::RobloxPermissionsBridge> permissions;
  std::unique_ptr<mocktail::runtime::RobloxCallProtocolBridge> calls;
};

// Brings up the PermissionsProtocol and CallProtocol the direct GAME path
// needs. That path runs the CoreScripts which query app protocols, so it
// needs these even though ExperienceProtocol's dynamic launch path builds
// its own.
bool InstallGamePlatformProtocols(JavaVM* raw_vm, jnivm::VM* jni_vm,
                                  void* roblox_handle,
                                  bool microphone_enabled,
                                  GamePlatformProtocols* protocols) {
  mocktail::runtime::JniEnvironmentProvider environment{
      raw_vm, jni_vm, &RestoreGameSessionJniEnvironment};
  JNIEnv* env = nullptr;
  auto status = environment.Acquire(&env);
  if (!status.ok()) {
    std::cerr << "[FATAL] GAME protocol JNI environment unavailable\n";
    return false;
  }
  const auto symbols = mocktail::runtime::ResolveRobloxPlatformProtocolSymbols(
      roblox_handle,
      reinterpret_cast<mocktail::runtime::DeleteMessageBusConnectionFn>(
          linker::ResolveSymbol(roblox_handle,
                                "Java_com_roblox_universalapp_messagebus_"
                                "Connection_deleteSharedPtr")));
  jclass bus_class =
      env->FindClass("com/roblox/universalapp/messagebus/MessageBus");
  jmethodID singleton =
      bus_class ? env->GetStaticMethodID(
                      bus_class, "f",
                      "()Lcom/roblox/universalapp/messagebus/MessageBus;")
                : nullptr;
  jobject bus = singleton
                    ? env->CallStaticObjectMethod(bus_class, singleton)
                    : nullptr;
  if (bus_class)
    env->DeleteLocalRef(bus_class);
  if (!bus || env->ExceptionCheck()) {
    if (env->ExceptionCheck())
      env->ExceptionClear();
    if (bus)
      env->DeleteLocalRef(bus);
    std::cerr << "[FATAL] GAME platform MessageBus is unavailable\n";
    return false;
  }
  const mocktail::runtime::RobloxMessageBusObjects objects{
      bus,
      jni_vm,
      &CreateExperienceRawCallback,
      &ClearExperienceRawCallback,
      &CreateAsyncMessageBusRequestHandler,
      &ClearAsyncMessageBusRequestHandler};
  protocols->permissions =
      std::make_unique<mocktail::runtime::RobloxPermissionsBridge>(
          environment, symbols.permissions, objects,
          microphone_enabled);
  protocols->calls =
      std::make_unique<mocktail::runtime::RobloxCallProtocolBridge>(
          environment, symbols.permissions, objects);
  status = protocols->permissions->Initialize();
  if (status.ok())
    status = protocols->calls->Initialize();
  env->DeleteLocalRef(bus);
  if (!status.ok()) {
    std::cerr << "[FATAL] GAME platform protocols did not initialize: "
              << status.message() << '\n';
    return false;
  }
  std::cout << "  [platform] direct GAME PermissionsProtocol and "
               "CallProtocol ready\n"
            << std::flush;
  return true;
}

// Builds the composition the dynamic launch path uses, which gets its
// permissions and call protocols from the experience message bus rather
// than from the engine-wide singletons.
bool InstallExperiencePlatformProtocols(
    JavaVM* raw_vm,
    jnivm::VM* jni_vm,
    void* roblox_handle,
    const mocktail::runtime::RobloxGameSessionSymbols& experience_game_symbols,
    mocktail::window::ScopedPresentObserver* game_present_observer,
    const mocktail::runtime::InputCapabilityConfig& input_capabilities,
    bool microphone_enabled,
    std::shared_ptr<mocktail::runtime::RobloxExperienceComposition>*
        experience_composition) {
  mocktail::runtime::RobloxExperienceMessageBusSymbols message_bus_symbols;
  message_bus_symbols.get_launch_id =
      reinterpret_cast<mocktail::runtime::GetExperienceLaunchIdFn>(
          linker::ResolveSymbol(roblox_handle,
                                "Java_com_roblox_universalapp_experience_"
                                "JNIExperienceProtocol_getLaunchId"));
  message_bus_symbols.subscribe_raw =
      reinterpret_cast<mocktail::runtime::SubscribeExperienceLaunchRawFn>(
          linker::ResolveSymbol(
              roblox_handle,
              "Java_com_roblox_universalapp_messagebus_MessageBus_"
              "doSubscribeRaw"));
  message_bus_symbols.delete_connection =
      reinterpret_cast<mocktail::runtime::DeleteMessageBusConnectionFn>(
          linker::ResolveSymbol(
              roblox_handle,
              "Java_com_roblox_universalapp_messagebus_Connection_"
              "deleteSharedPtr"));
  const mocktail::runtime::RobloxPlatformProtocolSymbols platform_protocol_symbols =
      mocktail::runtime::ResolveRobloxPlatformProtocolSymbols(
          roblox_handle, message_bus_symbols.delete_connection);
  mocktail::runtime::RobloxExperienceJniFactory jni_factory{
      jni_vm,
      &CreateExperienceRawCallback,
      &ClearExperienceRawCallback,
      &mocktail::runtime::SetJnivmPlatformWebCallbacks,
      &mocktail::runtime::ClearJnivmPlatformWebCallbacks,
      &CreateAsyncMessageBusRequestHandler,
      &ClearAsyncMessageBusRequestHandler};
  mocktail::runtime::RobloxFreshLaunchPresentBoundary present_boundary{
      game_present_observer, &RegisterFreshGamePresentObserver,
      &ClearFreshGamePresentObserver};
  mocktail::runtime::RobloxGameSurfaceJniConfig surface_config;
  surface_config.asset_folder_path = DefaultAssetPath();
  surface_config.dpi_scale =
      mocktail::window::GetWindowViewportSnapshot().dpi_scale;
  surface_config.is_touch_device = input_capabilities.touch_enabled;
  surface_config.is_mouse_device = input_capabilities.mouse_enabled;
  surface_config.is_keyboard_device = input_capabilities.keyboard_enabled;
  mocktail::runtime::JniEnvironmentProvider environment{
      raw_vm, jni_vm, &RestoreGameSessionJniEnvironment};
  *experience_composition =
      std::make_shared<mocktail::runtime::RobloxExperienceComposition>(
          environment, message_bus_symbols,
          platform_protocol_symbols.system_theme,
          platform_protocol_symbols.permissions,
          experience_game_symbols, jni_factory, present_boundary,
          std::move(surface_config),
          mocktail::runtime::RobloxExperienceSurfaceProvider{},
          mocktail::runtime::RobloxExperiencePresenceObserver{},
          microphone_enabled);
  const mocktail::Status platform_protocol_status =
      (*experience_composition)->InitializePlatformProtocols();
  if (!platform_protocol_status.ok()) {
    std::cerr << "[FATAL] Roblox platform protocols did not initialize: "
              << platform_protocol_status.message() << '\n';
    return false;
  }
  std::cout
      << "  [platform] PermissionsProtocol and CallProtocol initialized "
         "before native bootstrap\n"
      << std::flush;
  return true;
}

// Takes down the host-side bridges in the order the window gives them up:
// text input first, then the window input runtime that may already have
// torn the first one down, then the platform protocol bridges.
void FinishWindowBridges(
    mocktail::runtime::RobloxTextInputJniBridge* text_input_bridge,
    mocktail::runtime::RobloxWindowInputRuntime* window_input_runtime,
    GamePlatformProtocols* game_protocols,
    RunCompletionState* completion) {
  if (text_input_bridge != nullptr) {
    completion->input_shutdown_completed = text_input_bridge->Shutdown().ok();
  }
  if (window_input_runtime != nullptr) {
    completion->input_shutdown_completed =
        window_input_runtime->Shutdown().ok() &&
        completion->input_shutdown_completed;
  }
  if (game_protocols->calls != nullptr) {
    (void)game_protocols->calls->Shutdown();
    game_protocols->calls.reset();
  }
  if (game_protocols->permissions != nullptr) {
    (void)game_protocols->permissions->Shutdown();
    game_protocols->permissions.reset();
  }
}

// Tears down the experience composition and reports whether the place was
// still up when it went. If it was, the app-lifecycle shutdown below has
// nothing left to drive.
bool FinishExperienceComposition(
    jnivm::VM* jni_vm,
    std::shared_ptr<mocktail::runtime::RobloxExperienceComposition>
        experience_composition,
    std::shared_ptr<ExperienceLifecycleTarget>* experience_lifecycle_target,
    RunCompletionState* completion) {
  bool experience_destroyed_app = false;
  if (experience_composition != nullptr) {
    jni_vm->ClearRobloxExperienceLifecycleCallbacks();
    experience_lifecycle_target->reset();
    const mocktail::runtime::GameSessionSnapshot snapshot =
        experience_composition->Snapshot();
    experience_destroyed_app =
        snapshot.game_running || snapshot.game_paused ||
        snapshot.game_present_pending;
    const mocktail::Status experience_shutdown =
        experience_composition->Shutdown();
    if (experience_destroyed_app) {
      completion->lifecycle_shutdown_completed = experience_shutdown.ok();
    }
    if (!experience_shutdown.ok()) {
      std::cerr << "  [main] ExperienceProtocol shutdown failed: "
                << experience_shutdown.message() << '\n';
    }
  }
  return experience_destroyed_app;
}

// The last thing between the window closing and the process exiting. Either
// the game session runtime reports the state the engine left its lifecycle
// in, or the symbols are resolved here and the legacy app lifecycle is
// driven directly.
void FinishRobloxAppLifecycle(
    void* roblox_handle,
    mocktail::runtime::RobloxGameSessionRuntime* game_session_runtime,
    mocktail::window::ScopedPresentObserver* game_present_observer,
    bool experience_destroyed_app,
    RunCompletionState* completion) {
  if (game_session_runtime != nullptr) {
    const mocktail::runtime::GameSessionUpdateResult shutdown_result =
        game_session_runtime->Shutdown();
    completion->lifecycle_shutdown_completed =
        shutdown_result.ok() &&
        shutdown_result.state == mocktail::runtime::GameSessionState::kStopped;
    std::cout << "  [main] Roblox lifecycle shutdown: "
              << mocktail::runtime::GameSessionStateName(shutdown_result.state)
              << " (" << shutdown_result.message << ")\n"
              << std::flush;
    game_present_observer->Reset();
  } else if (!experience_destroyed_app) {
    const mocktail::runtime::RobloxSymbolLookup lifecycle_lookup(
        &ResolveRobloxCapabilitySymbol, &roblox_handle);
    const mocktail::runtime::RobloxAppLifecycleResolution
        lifecycle_resolution =
            mocktail::runtime::ResolveRobloxAppLifecycleSymbols(
                lifecycle_lookup);
    if (lifecycle_resolution.ok()) {
      JNIEnv* shutdown_env = AttachMainThreadJniEnv();
      mocktail::runtime::RobloxAppLifecycle lifecycle(
          *lifecycle_resolution.symbols());
      const mocktail::runtime::RobloxAppShutdownResult shutdown_result =
          lifecycle.Shutdown(shutdown_env,
                             g_native_gl_class_for_main_thread);
      completion->lifecycle_shutdown_completed = shutdown_result.ok();
      std::cout << "  [main] Roblox lifecycle shutdown: "
                << mocktail::runtime::RobloxAppShutdownStatusName(
                       shutdown_result.status)
                << " (" << shutdown_result.message << ")\n"
                << std::flush;
    } else {
      std::cerr << "  [main] Roblox lifecycle resolution failed: "
                << mocktail::runtime::RobloxAppLifecycleResolutionStatusName(
                       lifecycle_resolution.status())
                << '\n'
                << std::flush;
    }
  }
}

// Registers every Android/Java class the engine resolves by name through
// FindClass. JNI_OnLoad looks these descriptors up, so the table has to be
// in place before the engine library is loaded. The context and activity
// classes are held in locals purely so their registrations outlive this
// function.
void RegisterAndroidSdkJniClasses(const std::shared_ptr<jnivm::VM>& jni_vm) {
  auto context_class = jni_vm->RegisterClass("android/content/Context");
  auto activity_class =
      jni_vm->RegisterClass("com/roblox/client/RobloxActivity");
  auto settings_class = jni_vm->RegisterClass("rbx/JNIRobloxSettings");

  settings_class->RegisterMethod(
      "nativeInitClientSettings", "()V",
      [](JNIEnv* /*env*/, jobject /*obj*/) {
        std::cout << "  [JNI callback] nativeInitClientSettings invoked\n";
      });

  auto native_gl_interface_class =
      jni_vm->RegisterClass("com/roblox/engine/jni/NativeGLInterface");
  g_native_gl_class_for_main_thread =
      reinterpret_cast<jclass>(native_gl_interface_class.get());
  g_vm_for_main_thread_pump = jni_vm.get();
  jni_vm->RegisterClass("com/roblox/engine/jni/NativeInputInterface");
  jni_vm->RegisterClass("com/roblox/engine/jni/NativeSettingsInterface");
  jni_vm->RegisterClass("com/roblox/engine/jni/NativeAppBridgeInterface");
  jni_vm->RegisterClass("com/roblox/engine/jni/NativeGLJavaInterface");
  jni_vm->RegisterClass("com/roblox/engine/jni/EngineJavaCallback2");
  jni_vm->RegisterClass("com/roblox/engine/jni/OnAppBridgeNotificationListener");
  jni_vm->RegisterClass("com/roblox/client/flags/FlagJniInterface");
  jni_vm->RegisterClass("com/roblox/client/flags/NativeFlagsInitResult");
  jni_vm->RegisterClass("com/roblox/engine/jni/model/DeviceStaticParams");
  jni_vm->RegisterClass("com/roblox/engine/jni/model/DeviceParams");
  jni_vm->RegisterClass("com/roblox/engine/jni/model/PlatformParams");
  jni_vm->RegisterClass("com/roblox/engine/jni/model/NativeTextBoxInfo");
  jni_vm->RegisterClass("com/roblox/engine/jni/autovalue/InitParams");
  jni_vm->RegisterClass("com/roblox/engine/jni/autovalue/StartAppParams");
  jni_vm->RegisterClass("com/roblox/client/startup/MainGameActivity");
  jni_vm->RegisterClass("com/roblox/client/startup/NativeHelper");
  jni_vm->RegisterClass(
      "com/roblox/universalapp/activitylifecyclecallbacks/"
      "JNIActivityLifecycleCallbacks");
  jni_vm->RegisterClass("com/roblox/universalapp/messagebus/MessageBus");
  jni_vm->RegisterClass("com/roblox/universalapp/messagebus/Connection");
  jni_vm->RegisterClass(
      "com/roblox/universalapp/systemtheme/SystemThemeProtocol");
  jni_vm->RegisterClass("com/roblox/universalapp/cookie/CookieProtocol");
  jni_vm->RegisterClass(
      "com/roblox/universalapp/linking/JNIWebLoginProtocol");
  jni_vm->RegisterClass("com/roblox/universalapp/cookie/JNICookieManager");
  jni_vm->RegisterClass("com/roblox/universalapp/cookie/JNICookieProtocol");
  jni_vm->RegisterClass(
      "com/roblox/universalapp/cookie/JNICookieProtocol$OnSetCookieHandler");
  jni_vm->RegisterClass("com/roblox/client/JNIAAssetManagerSetup");
  jni_vm->RegisterClass("org/fmod/FMOD");
  jni_vm->RegisterClass("android/content/res/AssetManager");
  jni_vm->RegisterClass("android/media/AudioManager");
  jni_vm->RegisterClass(
      "com/roblox/protocols/systemdialog/PlatformSystemDialogHandler");
  jni_vm->RegisterClass(
      "com/roblox/protocols/systemdialogplatforminterface/generated/"
      "SystemDialogRequest");
  jni_vm->RegisterClass(
      "com/roblox/protocols/systemdialogplatforminterface/generated/"
      "ISystemDialogCallback");
  jni_vm->RegisterClass("com/google/androidgamesdk/GameActivity");
  jni_vm->RegisterClass("com/google/androidgamesdk/gametextinput/InputConnection");
  jni_vm->RegisterClass("com/google/androidgamesdk/gametextinput/State");
  jni_vm->RegisterClass("com/roblox/client/LocalStorageManager");
  jni_vm->RegisterClass(
      "com/roblox/protocols/localstorageplatforminterface/generated/"
      "ILocalStorageHandlerCore");
  jni_vm->RegisterClass(
      "com/roblox/protocols/localstorageplatforminterface/generated/"
      "ILocalStorageHandlerCore$CppProxy");
  jni_vm->RegisterClass(
      "com/roblox/protocols/localstorageplatforminterface/generated/"
      "IPlatformLocalStorageHandler");
  jni_vm->RegisterClass(
      "com/roblox/protocols/localstorageplatforminterface/generated/"
      "IPlatformLocalStorageHandler$CppProxy");
  jni_vm->RegisterClass("java/util/HashSet");
  jni_vm->RegisterClass("java/util/Iterator");
  jni_vm->RegisterClass("android/os/LocaleList");
  jni_vm->RegisterClass("java/util/Locale");
  jni_vm->RegisterClass("androidx/core/graphics/Insets");
  jni_vm->RegisterClass("androidx/core/view/WindowInsetsCompat$Type");
  jni_vm->RegisterClass("android/view/MotionEvent");
  jni_vm->RegisterClass("android/view/KeyEvent");
  jni_vm->RegisterClass("android/view/Surface");
  jni_vm->RegisterClass("java/lang/String");
  jni_vm->RegisterClass("java/lang/Object");

  std::cout << "  Registered " << jni_vm->GetClassCount()
            << " JNI class(es)\n";
}

// Registers the Bionic networking, pthread and process-level entry points the
// engine imports from libc and libdl. These are straight pass-through
// wrappers, so they go in directly rather than through the bulk symbol
// table the GL and Vulkan symbols go through.
void RegisterBionicNetworkAndPthreadSymbols() {
  RegisterBionicDnsWrappers();
  RegisterBionicPathWrappers();
  linker::RegisterSymbol("prctl",
                         reinterpret_cast<void*>(mocktail_bionic_prctl));
  linker::RegisterSymbol(
      "setsockopt", reinterpret_cast<void*>(mocktail_bionic_setsockopt));
  linker::RegisterSymbol("sendmsg",
                         reinterpret_cast<void*>(mocktail_bionic_sendmsg));
  linker::RegisterSymbol("mprotect", reinterpret_cast<void*>(mocktail_mprotect));
  linker::RegisterSymbol("pthread_condattr_init",
                         reinterpret_cast<void*>(mocktail_pthread_condattr_init));
  linker::RegisterSymbol("pthread_condattr_destroy",
                         reinterpret_cast<void*>(mocktail_pthread_condattr_destroy));
  linker::RegisterSymbol("pthread_condattr_setclock",
                         reinterpret_cast<void*>(mocktail_pthread_condattr_setclock));
  linker::RegisterSymbol("pthread_cond_init",
                         reinterpret_cast<void*>(mocktail_pthread_cond_init));
  linker::RegisterSymbol("pthread_cond_destroy",
                         reinterpret_cast<void*>(mocktail_pthread_cond_destroy));
  linker::RegisterSymbol("pthread_cond_signal",
                         reinterpret_cast<void*>(mocktail_pthread_cond_signal));
  linker::RegisterSymbol("pthread_cond_broadcast",
                         reinterpret_cast<void*>(mocktail_pthread_cond_broadcast));
  linker::RegisterSymbol("pthread_cond_wait",
                         reinterpret_cast<void*>(mocktail_pthread_cond_wait));
  linker::RegisterSymbol("pthread_cond_timedwait",
                         reinterpret_cast<void*>(mocktail_pthread_cond_timedwait));
  linker::RegisterSymbol("pthread_mutexattr_init",
                         reinterpret_cast<void*>(mocktail_pthread_mutexattr_init));
  linker::RegisterSymbol("pthread_mutexattr_destroy",
                         reinterpret_cast<void*>(mocktail_pthread_mutexattr_destroy));
  linker::RegisterSymbol("pthread_mutexattr_settype",
                         reinterpret_cast<void*>(mocktail_pthread_mutexattr_settype));
  linker::RegisterSymbol("pthread_mutex_init",
                         reinterpret_cast<void*>(mocktail_pthread_mutex_init));
  linker::RegisterSymbol("pthread_mutex_destroy",
                         reinterpret_cast<void*>(mocktail_pthread_mutex_destroy));
  linker::RegisterSymbol("pthread_mutex_lock",
                         reinterpret_cast<void*>(mocktail_pthread_mutex_lock));
  linker::RegisterSymbol("pthread_mutex_trylock",
                         reinterpret_cast<void*>(mocktail_pthread_mutex_trylock));
  linker::RegisterSymbol("pthread_mutex_unlock",
                         reinterpret_cast<void*>(mocktail_pthread_mutex_unlock));
  linker::RegisterSymbol("pthread_once",
                         reinterpret_cast<void*>(mocktail_pthread_once));
  linker::RegisterSymbol("pthread_spin_init",
                         reinterpret_cast<void*>(mocktail_pthread_spin_init));
  linker::RegisterSymbol("pthread_spin_destroy",
                         reinterpret_cast<void*>(mocktail_pthread_spin_destroy));
  linker::RegisterSymbol("pthread_spin_lock",
                         reinterpret_cast<void*>(mocktail_pthread_spin_lock));
  linker::RegisterSymbol("pthread_spin_trylock",
                         reinterpret_cast<void*>(mocktail_pthread_spin_trylock));
  linker::RegisterSymbol("pthread_spin_unlock",
                         reinterpret_cast<void*>(mocktail_pthread_spin_unlock));
  linker::RegisterSymbol(
      "pthread_attr_init",
      reinterpret_cast<void*>(mocktail_pthread_attr_init));
  linker::RegisterSymbol(
      "pthread_attr_destroy",
      reinterpret_cast<void*>(mocktail_pthread_attr_destroy));
  linker::RegisterSymbol(
      "pthread_attr_setstacksize",
      reinterpret_cast<void*>(mocktail_pthread_attr_setstacksize));
  linker::RegisterSymbol(
      "pthread_attr_setdetachstate",
      reinterpret_cast<void*>(mocktail_pthread_attr_setdetachstate));
  linker::RegisterSymbol(
      "pthread_attr_setschedparam",
      reinterpret_cast<void*>(mocktail_pthread_attr_setschedparam));
  linker::RegisterSymbol(
      "pthread_getattr_np",
      reinterpret_cast<void*>(mocktail_pthread_getattr_np));
  linker::RegisterSymbol(
      "pthread_attr_getstack",
      reinterpret_cast<void*>(mocktail_pthread_attr_getstack));
  linker::RegisterSymbol("pthread_barrier_init",
                         reinterpret_cast<void*>(mocktail_pthread_barrier_init));
  linker::RegisterSymbol(
      "pthread_barrier_destroy",
      reinterpret_cast<void*>(mocktail_pthread_barrier_destroy));
  linker::RegisterSymbol("pthread_barrier_wait",
                         reinterpret_cast<void*>(mocktail_pthread_barrier_wait));
  linker::RegisterSymbol(
      "pthread_create",
      reinterpret_cast<void*>(mocktail_bionic_pthread_create));
  linker::RegisterSymbol(
      "pthread_setschedparam",
      reinterpret_cast<void*>(mocktail_bionic_pthread_setschedparam));
  linker::RegisterSymbol(
      "sched_setscheduler",
      reinterpret_cast<void*>(mocktail_bionic_sched_setscheduler));
  linker::RegisterSymbol("abort", reinterpret_cast<void*>(mocktail_abort));
  linker::RegisterSymbol("__stack_chk_fail", reinterpret_cast<void*>(mocktail_recover_stack_chk_fail));
}

// True when at least one symbol Stage 6 needs is still null. Checked as a
// single predicate so the required-symbol list reads as one list instead of
// burying the real failure handling under 28 lines of pointer comparisons.
bool StartupSymbolsMissing(const RobloxEngineSymbols& symbols,
                           void* message_bus_publish_response_raw) {
  return ((symbols.init_client_settings == nullptr &&
         symbols.init_client_settings_signed == nullptr &&
         symbols.init_client_settings_cached == nullptr &&
         symbols.init_client_settings_cached_compressed == nullptr) ||
        symbols.post_client_settings == nullptr ||
        symbols.set_base_url == nullptr || symbols.set_device_info == nullptr ||
        symbols.set_roblox_channel == nullptr ||
        symbols.set_exception_reason_filename == nullptr ||
        symbols.set_base_data_directories == nullptr ||
        symbols.set_cache_directory == nullptr ||
        symbols.set_files_directory == nullptr ||
        symbols.set_external_directory == nullptr ||
        symbols.set_preferences_file == nullptr ||
        symbols.set_default_app_policy_file == nullptr ||
        symbols.set_http_client_proxy == nullptr ||
        symbols.init_fast_log == nullptr ||
        symbols.set_multiple_cookies == nullptr ||
        symbols.set_platform_headers_with_idfa == nullptr ||
        symbols.init_storage_manager == nullptr ||
        symbols.set_init_params == nullptr || symbols.set_asset_path == nullptr ||
        symbols.activity_lifecycle.on_created == nullptr ||
        symbols.activity_lifecycle.on_started == nullptr ||
        symbols.activity_lifecycle.on_resumed == nullptr ||
        message_bus_publish_response_raw == nullptr);
}

// Brings up the typed production-input runtime and the text-input bridge over
// it. Returns false only after reporting which stage refused to start.
bool InitializeTypedInput(
    JavaVM* raw_vm, const std::shared_ptr<jnivm::VM>& jni_vm,
    const mocktail::runtime::RobloxInputSymbols& input_symbols,
    std::shared_ptr<mocktail::runtime::RobloxWindowInputRuntime>&
        window_input_runtime,
    std::unique_ptr<mocktail::runtime::RobloxTextInputJniBridge>&
        text_input_bridge) {
  if (!mocktail::window::IsInitialised()) {
    return true;
  }
  mocktail::runtime::JniEnvironmentProvider input_environment{
      raw_vm, jni_vm.get(), &RestoreGameSessionJniEnvironment};
  window_input_runtime =
      std::make_shared<mocktail::runtime::RobloxWindowInputRuntime>(
          input_environment, input_symbols);
  const mocktail::Status input_status = window_input_runtime->Initialize();
  if (!input_status.ok()) {
    std::cerr << "[FATAL] Typed production input did not initialize: "
              << input_status.message() << '\n';
    return EXIT_FAILURE;
  }
  const mocktail::Status text_input_status =
      mocktail::runtime::RobloxTextInputJniBridge::Create(
          jni_vm.get(), window_input_runtime, &text_input_bridge);
  if (!text_input_status.ok()) {
    std::cerr << "[FATAL] Typed Roblox text input did not initialize: "
              << text_input_status.message() << '\n';
    return EXIT_FAILURE;
  }
  return true;
}

// What the stub preload produced. The handle list feeds symbol resolution;
// the name list and the Vulkan adapter handle are both needed again later,
// when the synthetic SONAMEs get loaded.
struct BionicStubPreload {
  std::vector<const char*> names;
  std::vector<void*> handles;
  void* vulkan_adapter = nullptr;
};

// One pass over kSymbolsToRegister, summarised so the caller can report
// where each GL symbol came from.
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

// One GL name's contribution to the audit. Shared by both resolution passes so
// the totals mean the same thing whichever pass supplied the address.
void TallyGlSymbol(const char* name, SymbolResolveSource source, void* address,
                   GlSymbolStats* stats) {
  if (!IsGlSymbol(name)) {
    return;
  }
  ++stats->gl_symbol_count;
  if (address == nullptr) {
    ++stats->gl_unresolved;
  }
  switch (source) {
    case SymbolResolveSource::kWindow:
      ++stats->gl_from_window;
      break;
    case SymbolResolveSource::kRealGles:
      ++stats->gl_from_real_gles;
      break;
    case SymbolResolveSource::kStub:
      ++stats->gl_from_stub;
      break;
    case SymbolResolveSource::kHost:
      ++stats->gl_from_host;
      break;
    case SymbolResolveSource::kMissing:
    default:
      break;
  }
}

// Opens every Bionic library the guest can import from and keeps the
// handles. EGL and Vulkan prefer our own adapter over whatever the host
// happens to have, since the guest resolves those two by SONAME.
BionicStubPreload PreloadBionicStubs(
    const mocktail::graphics::BionicEglBridge& bionic_egl_bridge) {
  std::vector<const char*> stub_names = {
      "libc.so", "libdl.so", "libm.so", "libz.so",
      "libandroid.so", "liblog.so", "libmediandk.so",
      "libOpenSLES.so", "libOpenMAXAL.so",
      "libEGL.so", "libGLESv2.so",
  };
  if (IsEnabled("MOCKTAIL_PRELOAD_VULKAN_SHIM")) {
    stub_names.push_back("libvulkan.so");
  }

  std::vector<void*> stub_handles;
  void* bionic_vulkan_adapter_handle = nullptr;
  for (const char* name : stub_names) {
    void* h = nullptr;
    bool exact_adapter = false;
    if (std::strcmp(name, "libEGL.so") == 0 &&
        bionic_egl_bridge.IsLoaded()) {
      h = bionic_egl_bridge.handle();
      exact_adapter = true;
    } else {
      const std::string adapter = RuntimeBionicAdapterPath(name);
      if (!adapter.empty()) {
        h = ::dlopen(adapter.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        exact_adapter = h != nullptr;
      }
    }
    if (h == nullptr) {
      h = ::dlopen(name, RTLD_LAZY | RTLD_GLOBAL | RTLD_NOLOAD);
      if (!h) h = ::dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
    }
    if (h) {
      std::cout << "  [stubs] Preloaded " << name;
      if (exact_adapter) {
        std::cout << " via exact host adapter";
      }
      std::cout << '\n';
      stub_handles.push_back(h);
      if (std::strcmp(name, "libvulkan.so") == 0) {
        bionic_vulkan_adapter_handle = h;
      }
    } else {
      std::cerr << "  [warn]  Could not preload " << name
                << ": " << ::dlerror() << '\n';
    }
  }
  return BionicStubPreload{stub_names, stub_handles,
                         bionic_vulkan_adapter_handle};
}

// Walks kSymbolsToRegister and binds each name to whatever can satisfy it:
// for GL, the real GLES library first and then our stubs; otherwise the
// host. Anything still unresolved is counted, not registered as null.
GlSymbolStats RegisterEngineSymbols(bool has_window,
                                     void* real_gles_handle,
                                     const std::vector<void*>& stub_handles) {
  GlSymbolStats stats;
  for (const char* sym : kSymbolsToRegister) {
    ++stats.total_symbols;
    auto result = ResolveSymbolForBionic(sym, has_window, real_gles_handle,
                                         stub_handles);
    TallyGlSymbol(sym, result.source, result.address, &stats);
    if (result.address != nullptr) {
      linker::RegisterSymbol(sym, result.address);
      ++stats.registered;
    }
  }
  return stats;
}

// The Vulkan adapter exposes the loader entry points under both
// libvulkan.so and libvulkan.so.1, so the guest resolves them whichever
// SONAME it happens to import.
void RegisterVulkanAdapterExports(void* bionic_vulkan_adapter_handle) {
  if (bionic_vulkan_adapter_handle != nullptr) {
    size_t vulkan_exports = 0;
    for (const char* name : kVulkanAdapterExports) {
      void* address = ::dlsym(bionic_vulkan_adapter_handle, name);
      if (address == nullptr ||
          !StubOwnsSymbolAddress(bionic_vulkan_adapter_handle, address)) {
        continue;
      }
      linker::RegisterSyntheticSymbol("libvulkan.so", name, address);
      linker::RegisterSyntheticSymbol("libvulkan.so.1", name, address);
      linker::RegisterSymbol(name, address);
      ++vulkan_exports;
    }
    std::cout << "  [vulkan] registered exact Android loader adapter exports: "
              << vulkan_exports << '\n';
  }
}

// libroblox.so declares several hundred imports, far more than the
// kSymbolsToRegister table lists by hand. Rather than maintain a second
// list that drifts, walk its .dynsym once and let the same resolver bind
// anything it recognises. The file is mapped read-only and never run, and
// the map is released before returning.
GlSymbolStats RegisterUndefinedElfImports(const std::string& library_path,
                                          bool has_window,
                                          void* real_gles_handle,
                                          const std::vector<void*>& stub_handles) {
  // The guest's GL imports arrive through this pass rather than the table, so
  // they are counted here for the shared real-graphics audit.
  GlSymbolStats stats;
  int fd = ::open(library_path.c_str(), O_RDONLY);
  if (fd >= 0) {
    struct stat st;
    if (::fstat(fd, &st) == 0) {
      void* map = ::mmap(nullptr, static_cast<size_t>(st.st_size),
                         PROT_READ, MAP_PRIVATE, fd, 0);
      if (map != MAP_FAILED) {
        auto* ehdr = reinterpret_cast<Elf64_Ehdr*>(map);
        auto* shdr = reinterpret_cast<Elf64_Shdr*>(
            reinterpret_cast<char*>(map) + ehdr->e_shoff);
        Elf64_Shdr* dynsym_hdr = nullptr;
        Elf64_Shdr* dynstr_hdr = nullptr;
        for (int si = 0; si < ehdr->e_shnum; ++si) {
          if (shdr[si].sh_type == SHT_DYNSYM) dynsym_hdr = &shdr[si];
          else if (shdr[si].sh_type == SHT_STRTAB && si != ehdr->e_shstrndx)
            dynstr_hdr = &shdr[si];
        }
        if (dynsym_hdr && dynstr_hdr) {
          auto* syms = reinterpret_cast<Elf64_Sym*>(
              reinterpret_cast<char*>(map) + dynsym_hdr->sh_offset);
          const char* strtab = reinterpret_cast<const char*>(map)
                               + dynstr_hdr->sh_offset;
          size_t num_syms = dynsym_hdr->sh_size / sizeof(Elf64_Sym);
          int auto_registered = 0;
          for (size_t si = 0; si < num_syms; ++si) {
            if (syms[si].st_shndx != SHN_UNDEF) continue;
            const char* name = strtab + syms[si].st_name;
            if (!name || name[0] == '\0') continue;
            if (linker::GetBionicSymbols().count(name)) continue;
            auto result = ResolveSymbolForBionic(name, has_window,
                                                 real_gles_handle,
                                                 stub_handles);
            TallyGlSymbol(name, result.source, result.address, &stats);
            if (result.address != nullptr) {
              linker::RegisterSymbol(name, result.address);
              ++auto_registered;
            }
          }
          std::cout << "  [linker] Auto-registered " << auto_registered
                    << " additional ELF symbols from " << library_path
                    << '\n';
        }
        ::munmap(map, static_cast<size_t>(st.st_size));
      }
    }
    ::close(fd);
  }
  return stats;
}

int mocktail::legacy::Run(const runtime::CommandLineOptions& options,
                          RuntimeDependencies dependencies) {
  // Captured before anything can scrub the bearer value out of options.
  const bool started_with_launch_uri = !options.engine_launch_uri.empty();
  const bool user_overrode_start_lua_app_dm =
      HasEnvValue("MOCKTAIL_START_LUA_APP_DM");
  const bool user_overrode_start_lua_step =
      HasEnvValue("MOCKTAIL_STEP_START_LUA_APP_DM");
  const bool user_overrode_start_app_step =
      HasEnvValue("MOCKTAIL_STEP_START_APP_WITH_PARAMS");
  const bool user_overrode_call_start_app =
      HasEnvValue("MOCKTAIL_CALL_REAL_APP_BRIDGE_START");
  const bool user_overrode_main_thread_message_pump =
      HasEnvValue("MOCKTAIL_MAIN_THREAD_MESSAGE_PUMP");

  std::cout << "======================================================\n"
            << "  Roblox Linux Client\n"
            << "======================================================\n"
            << std::flush;

  ApplyRuntimeDefaults();
  const mocktail::runtime::ProcessEnvironment process_environment;
  const mocktail::runtime::RuntimeConfig runtime_config =
      mocktail::runtime::RuntimeConfig::FromEnvironment(process_environment);
  const mocktail::runtime::InputCapabilityConfig& input_capabilities =
      runtime_config.input_capabilities();
  if (!runtime_config.frame_rate().valid()) {
    std::cerr << "[FATAL] Invalid MOCKTAIL_FRAME_RATE_LIMIT; expected "
                 "-1, display, unlimited, or a positive integer\n";
    return EXIT_FAILURE;
  }
  if (!runtime_config.theme_mode_valid()) {
    std::cerr
        << "[FATAL] Invalid MOCKTAIL_THEME; expected roblox, system, light, "
           "or dark\n";
    return EXIT_FAILURE;
  }
  setenv("MOCKTAIL_TOUCH_ENABLED_INTERNAL",
         input_capabilities.touch_enabled ? "1" : "0", 1);
  setenv("MOCKTAIL_MOUSE_ENABLED_INTERNAL",
         input_capabilities.mouse_enabled ? "1" : "0", 1);
  setenv("MOCKTAIL_KEYBOARD_ENABLED_INTERNAL",
         input_capabilities.keyboard_enabled ? "1" : "0", 1);
  if (runtime_config.has_unsafe_detached_thread_overrides()) {
    std::cerr << "[FATAL] Unsupported detached legacy thread overrides:\n";
    for (const std::string& name :
         runtime_config.unsafe_detached_thread_overrides()) {
      std::cerr << "  - " << name << '\n';
    }
    std::cerr << "  Supported runtime requires synchronous or owned worker "
                 "execution.\n";
    return EXIT_FAILURE;
  }
  const runtime::ScopedRobloxCredentialBinding credential_binding(
      dependencies.jni_vm().get(), dependencies.roblox_credential());
  if (!credential_binding.bound()) {
    std::cerr << "[FATAL] Typed Pseudo-JVM credential provider is missing\n";
    return EXIT_FAILURE;
  }
  const std::string library_path =
      runtime_config.roblox_library_path().string();

  // Build-ID profiles are gone: every libroblox.so is treated the same way.
  // The version identity is a fixed constant the engine and the Roblox web
  // services accept, and stays overridable for local testing.
  const char* version_override = std::getenv("MOCKTAIL_ROBLOX_VERSION");
  const std::string version_name =
      (version_override != nullptr && version_override[0] != '\0')
          ? version_override
          : "2.739.691";
  SetEnvDefault("MOCKTAIL_ROBLOX_VERSION", version_name.c_str());
  SetEnvDefault("MOCKTAIL_ROBLOX_VERSION_CODE", "3120");
  const std::string default_user_agent =
      "Roblox/" + version_name + " (Linux; Android 33)";
  SetEnvDefault("MOCKTAIL_USER_AGENT", default_user_agent.c_str());
  std::cout << "  [compat] Roblox " << version_name << '\n'
            << "  [compat] Build-ID profiles: removed\n"
            << std::flush;

  ApplyAuthStartupDefaults(!dependencies.roblox_credential().empty(),
                           user_overrode_start_lua_app_dm,
                           user_overrode_start_lua_step,
                           user_overrode_start_app_step,
                           user_overrode_call_start_app);
  if (TraceAllEnabled()) {
    std::cout << "  [trace] full tracing enabled\n" << std::flush;
  }

  // Roblox internal threads trigger SI_KERNEL traps from CET shadow-stack
  // return mismatches. Unsupported kernels ignore this request.
#if defined(__x86_64__)
  {
    long r = syscall(SYS_arch_prctl, ARCH_SHSTK_DISABLE, ARCH_SHSTK_SHSTK);
    if (r == 0) {
      std::cout << "  [cet] shadow-stack (SHSTK) disabled\n";
    }
  }
#endif

  std::cout << "  [compat] signal-recovery handler disabled\n";
  PrintStage(1, "Initialising Bionic linker + Pseudo-JVM");

#ifdef MOCKTAIL_USE_BIONIC_LINKER
  ::linker::init();
  std::cout << "  mcpelauncher Bionic linker initialised.\n";
#endif

  auto jni_vm = dependencies.jni_vm();
  if (jni_vm == nullptr) {
    std::cerr << "[FATAL] Typed Pseudo-JVM dependency is missing\n";
    return EXIT_FAILURE;
  }
  std::cout << "  Pseudo-JVM instance created successfully.\n";

  // FindClass needs these descriptors before JNI_OnLoad.
  PrintStage(2, "Registering Android SDK JNI classes");

  RegisterAndroidSdkJniClasses(jni_vm);

  const bool has_window = InitializeHostWindow(
      runtime_config, dependencies,
      user_overrode_main_thread_message_pump, user_overrode_call_start_app);
  PrintStage(3, "Building Bionic symbol table from stubs");

  libc_shim::Install();

  // Android unwinders must see mcpelauncher-linker's soinfo list. Stage the
  // exact per-SONAME exports before any synthetic library snapshots the legacy
  // map, and keep the global compatibility map consistent for unresolved ELF
  // imports discovered below.
  void* bionic_dl_iterate_phdr = linker::BionicDlIteratePhdrAddress();
  linker::RegisterSyntheticSymbol("libc.so", "dl_iterate_phdr",
                                  bionic_dl_iterate_phdr);
  linker::RegisterSyntheticSymbol("libdl.so", "dl_iterate_phdr",
                                  bionic_dl_iterate_phdr);
  linker::RegisterSymbol("dl_iterate_phdr", bionic_dl_iterate_phdr);

  // SDL owns the host window and surface. Android libroblox must instead see
  // Mocktail's libEGL adapter, which reuses that surface. Do not resolve this
  // by SONAME: Chromium ANGLE also calls itself libEGL.so and would otherwise
  // win once the SDL backend has loaded it.
  mocktail::graphics::BionicEglBridge bionic_egl_bridge;
  if (has_window) {
    if (!bionic_egl_bridge.Load()) {
      std::cerr << "  [graphics] exact Bionic EGL adapter unavailable: "
                << bionic_egl_bridge.error() << '\n';
    } else {
      for (const auto& [name, address] : bionic_egl_bridge.exports()) {
        linker::RegisterSyntheticSymbol("libEGL.so", name, address);
        linker::RegisterSymbol(name, address);
      }
      std::cout << "  [graphics] Bionic libEGL adapter loaded from "
                << bionic_egl_bridge.library_path() << " ("
                << bionic_egl_bridge.exports().size() << " exports)\n";
    }
  }

  const BionicStubPreload stubs = PreloadBionicStubs(bionic_egl_bridge);
  void* real_gles_handle = nullptr;
  if (has_window) {
    real_gles_handle = OpenRealGlesLibrary();
  }

  GlSymbolStats gl_stats = RegisterEngineSymbols(
      has_window, real_gles_handle, stubs.handles);
  RegisterVulkanAdapterExports(stubs.vulkan_adapter);
  RegisterBionicNetworkAndPthreadSymbols();
  // Runs before the audit below so the guest's own GL imports are counted in
  // it. Registration order is unchanged: table, then Bionic, then ELF.
  gl_stats.MergeGlResolution(RegisterUndefinedElfImports(
      library_path, has_window, real_gles_handle, stubs.handles));
  std::cout << "  [linker] Registered " << gl_stats.registered << " / "
            << gl_stats.total_symbols << " known symbols.\n";
  std::cout << "  [linker] GL symbol resolution: total="
            << gl_stats.gl_symbol_count
            << ", window=" << gl_stats.gl_from_window
            << ", real_gles=" << gl_stats.gl_from_real_gles
            << ", stub=" << gl_stats.gl_from_stub
            << ", host=" << gl_stats.gl_from_host
            << ", unresolved=" << gl_stats.gl_unresolved << '\n';
  // Load synthetic libraries only after the import map is complete.
  linker::RegisterBionicPthreadKeyRuntimeForLibc();
  linker::RegisterBionicSysconfRuntimeForLibc();
  linker::RegisterBionicSignalRuntimeForLibc();
  linker::RegisterBionicStdioRuntimeForLibc();
  linker::RegisterBionicDynamicLoaderForLibdl();
  std::cout << "  [linker] Registered typed Bionic pthread/sysconf/signal/"
               "stdio ABI for libc.so\n";
  std::cout << "  [linker] dl_iterate_phdr routed to Bionic linker before "
               "SONAME snapshots\n"
            << std::flush;
  for (const char* name : stubs.names) {
    linker::LoadLibrary(name, name);
  }
  if (stubs.vulkan_adapter != nullptr) {
    linker::LoadLibrary("libvulkan.so.1", "libvulkan.so.1");
  }
  PreloadPthreadSymbols();

  std::cout << "  Library path: " << library_path << '\n';
  linker::LibraryHandle roblox_handle =
      linker::LoadLibrary(library_path, "libroblox");

  if (roblox_handle == nullptr) {
    std::cerr << "\n[FATAL] Could not load '" << library_path << "'.\n"
              << "  Extract lib/" << compat::kGuestAbi
              << "/libroblox.so from a Roblox APK and\n"
              << "  place it at the path above (or set ROBLOX_LIB_PATH).\n";
    return EXIT_FAILURE;
  }

  const linker::ProgramHeaderValidation program_headers =
      linker::ValidateBionicProgramHeaders(roblox_handle);
  if (!program_headers) {
    std::cerr << "\n[FATAL] Bionic program-header visibility failed: "
              << program_headers.error << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "  [linker] Bionic unwind metadata validated for libroblox\n"
            << std::flush;

  PrintStage(5, "Invoking JNI_OnLoad in libroblox.so");

  auto* jni_onload =
      reinterpret_cast<JniOnLoadFn>(
          linker::ResolveSymbol(roblox_handle, "JNI_OnLoad"));

  if (jni_onload == nullptr) {
    std::cerr << "\n[FATAL] Symbol 'JNI_OnLoad' not found in libroblox.so.\n";
    return EXIT_FAILURE;
  }

  JavaVM* raw_vm = jni_vm->GetJavaVM();
  PublishCurrentJniEnv(jni_vm->GetJNIEnv());
  const int jni_timeout_ms = 3000;
  bool soft_timeout_enabled = IsEnabled("MOCKTAIL_JNI_ONLOAD_SOFT_TIMEOUT");
  bool soft_timeout_forced =
      IsEnabled("MOCKTAIL_JNI_ONLOAD_SOFT_TIMEOUT_FORCE");
  g_stage6_jni_env =
      reinterpret_cast<uintptr_t>(jni_vm->GetJNIEnv());
  if (soft_timeout_enabled && !soft_timeout_forced) {
    std::cerr
        << "  [jvm] JNI_OnLoad soft timeout disabled in safe mode (set "
        << "MOCKTAIL_JNI_ONLOAD_SOFT_TIMEOUT_FORCE=1 to enable).\n"
        << std::flush;
  }
  g_jni_onload_soft_timeout = soft_timeout_enabled && soft_timeout_forced;

  jint jni_version = JNI_ERR;
  if (jni_timeout_ms > 0 && g_jni_onload_soft_timeout != 0) {
    std::cout << "  [jvm] JNI_OnLoad soft timeout watcher armed: "
              << jni_timeout_ms << "ms\n"
              << std::flush;
    JniOnLoadAsyncContext async_context{jni_onload, JNI_ERR, jni_vm.get()};
    mocktail::runtime::OwnedPthread onload_thread;
    const int create_result =
        onload_thread.Start(&RunJniOnLoadWorker, &async_context, 0);
    if (create_result != 0) {
      std::cout << "  [jvm] JNI_OnLoad worker thread creation failed: "
                << create_result << "\n"
                << std::flush;
      jni_version = jni_onload(raw_vm, nullptr);
    } else {
      const mocktail::runtime::OwnedPthreadWaitResult wait_result =
          onload_thread.WaitFor(jni_timeout_ms, 10);
      if (wait_result.joined()) {
        jni_version = async_context.result;
      } else {
        std::cerr << "  [jvm] JNI_OnLoad soft timeout exceeded; cancelling "
                     "without allowing runtime unwind\n"
                  << std::flush;
        const int cancel_join_grace_ms = 5000;
        const mocktail::runtime::OwnedPthreadCancelResult cancel_result =
            onload_thread.CancelAndJoinFor(cancel_join_grace_ms, 10);
        std::cerr << "[FATAL] JNI_OnLoad did not complete before its timeout; "
                  << "cancel=" << cancel_result.cancel_error << " join="
                  << mocktail::runtime::OwnedPthreadWaitStatusName(
                         cancel_result.wait.status)
                  << " error=" << cancel_result.wait.platform_error
                  << "; terminating without RAII unwind\n"
                  << std::flush;
        std::_Exit(EXIT_FAILURE);
      }
    }
  } else {
    if (jni_timeout_ms > 0) {
      g_jni_onload_in_progress = 1;
      g_jni_onload_timings_printed = 0;
      std::cout << "  [jvm] JNI_OnLoad timeout watchdog armed: "
                << jni_timeout_ms << "ms\n"
                << std::flush;
      struct sigaction action;
      action.sa_sigaction = JniOnLoadTimeoutAlarm;
      action.sa_flags = SA_SIGINFO | SA_RESETHAND;
      sigemptyset(&action.sa_mask);
      sigaction(SIGALRM, &action, nullptr);
      alarm((jni_timeout_ms + 999) / 1000);
    }

    jni_version = jni_onload(raw_vm, nullptr);

    if (jni_timeout_ms > 0) {
      g_jni_onload_in_progress = 0;
      g_jni_onload_jmp_armed = 0;
      alarm(0);
    }
  }

  std::cout << "  JNI_OnLoad returned. Requested JNI version: 0x"
            << std::hex << std::setw(8) << std::setfill('0') << jni_version
            << std::dec << '\n';

  // libroblox owns this tagged-pointer segment table.  Keep it intact by
  // default; replacing it with the pseudo-JNI table makes native Roblox
  // scheduler objects resolve to Java object stubs with the wrong layout.
  {
    uintptr_t base_addr = g_libroblox_base;
#ifdef MOCKTAIL_USE_BIONIC_LINKER
    const uintptr_t loader_base =
        static_cast<uintptr_t>(::linker::get_library_base(roblox_handle));
    if (loader_base != 0) {
      base_addr = loader_base;
    }
#endif
    if (base_addr == 0) {
      Dl_info symbol_info{};
      if (::dladdr(reinterpret_cast<void*>(jni_onload), &symbol_info) != 0) {
        base_addr = reinterpret_cast<uintptr_t>(symbol_info.dli_fbase);
      }
    }
    if (base_addr == 0) {
      std::cerr << "[FATAL] Bionic linker did not report libroblox base\n";
      return EXIT_FAILURE;
    }
    g_libroblox_base = base_addr;
    g_mocktail_abort_libroblox_base = base_addr;
    std::cout << "  [compat] skipped all fixed-offset post-JNI patches\n"
              << std::flush;
  }

  // Restore the pseudo-JNI table if JNI_OnLoad replaced it.
  {
    JNIEnv* main_env = jni_vm->GetJNIEnv();
    if (main_env) {
      std::cerr << "  [jvm] restoring env->functions after JNI_OnLoad"
                << " (was " << (void*)main_env->functions << ")\n" << std::flush;
      jni_vm->RestoreFunctions();
      std::cerr << "  [jvm] env->functions now " << (void*)main_env->functions
                << "\n"
                << std::flush;
    }
  }
  std::unique_ptr<mocktail::runtime::RobloxGameSessionRuntime>
      game_session_runtime;
  std::unique_ptr<mocktail::runtime::RobloxGameSessionSymbols>
      experience_game_symbols;
  std::shared_ptr<mocktail::runtime::RobloxExperienceComposition>
      experience_composition;
  GamePlatformProtocols game_protocols;
  std::shared_ptr<ExperienceLifecycleTarget> experience_lifecycle_target;
  std::shared_ptr<mocktail::runtime::RobloxWindowInputRuntime>
      window_input_runtime;
  std::unique_ptr<mocktail::runtime::RobloxTextInputJniBridge>
      text_input_bridge;
  mocktail::window::ScopedPresentObserver game_present_observer;
  if (!IsDisabled("MOCKTAIL_START_ENGINE")) {
    PrintStage(6, "Invoking Roblox startup entry points");

    const mocktail::runtime::RobloxSymbolLookup capability_lookup(
        &ResolveRobloxCapabilitySymbol, &roblox_handle);
    const mocktail::runtime::RobloxCapabilityResolution capability_resolution =
        mocktail::runtime::ResolveRobloxCapabilities(
            capability_lookup,
            mocktail::runtime::RobloxCapabilityRequirements{has_window});
    if (!capability_resolution.ok()) {
      std::cerr << "\n[FATAL] Roblox startup capability resolution failed: "
                << mocktail::runtime::RobloxCapabilityResolutionStatusName(
                       capability_resolution.status())
                << '\n';
      for (const std::string& symbol_name :
           capability_resolution.missing_required_symbols()) {
        std::cerr << "  - " << symbol_name << '\n';
      }
      return EXIT_FAILURE;
    }

    const mocktail::runtime::RobloxCapabilities& roblox_capabilities =
        *capability_resolution.capabilities();
    const mocktail::runtime::RobloxStartupSymbols& startup_symbols =
        roblox_capabilities.startup;
    if (has_window) {
      const mocktail::runtime::RobloxGameSessionResolution game_resolution =
          mocktail::runtime::ResolveRobloxGameSessionSymbols(capability_lookup);
      if (!game_resolution.ok()) {
        std::cerr << "\n[FATAL] GAME lifecycle symbol resolution failed: "
                  << mocktail::runtime::RobloxGameSessionResolutionStatusName(
                         game_resolution.status())
                  << '\n';
        for (const std::string& symbol_name :
             game_resolution.missing_required_symbols()) {
          std::cerr << "  - " << symbol_name << '\n';
        }
        return EXIT_FAILURE;
      }
      if (IsEnabled("MOCKTAIL_START_GAME_WITH_PARAM")) {
        mocktail::runtime::JniEnvironmentProvider environment{
            raw_vm, jni_vm.get(), &RestoreGameSessionJniEnvironment};
        mocktail::runtime::RobloxGameSurfaceJniConfig surface_config;
        surface_config.asset_folder_path = DefaultAssetPath();
        surface_config.dpi_scale =
            mocktail::window::GetWindowViewportSnapshot().dpi_scale;
        surface_config.is_touch_device = input_capabilities.touch_enabled;
        surface_config.is_mouse_device = input_capabilities.mouse_enabled;
        surface_config.is_keyboard_device =
            input_capabilities.keyboard_enabled;
        game_session_runtime =
            std::make_unique<mocktail::runtime::RobloxGameSessionRuntime>(
                environment, *game_resolution.symbols(),
                std::move(surface_config));
        std::cout << "  [game-session] eager typed GAME symbols ready\n"
                  << std::flush;
      } else {
        experience_game_symbols =
            std::make_unique<mocktail::runtime::RobloxGameSessionSymbols>(
                *game_resolution.symbols());
        std::cout << "  [experience] dynamic GAME symbols ready\n"
                  << std::flush;
      }
    }

    const RobloxEngineSymbols symbols = ResolveRobloxEngineSymbols(
        roblox_handle, startup_symbols, jni_vm.get());
    g_native_call_messages_from_main_thread =
        symbols.call_messages_from_main_thread;
    std::cout << "  [engine] Stage6 symbol resolve done" << std::endl;
    ReportResolvedEngineSymbols(symbols);
    void* message_bus_publish_response_raw = linker::ResolveSymbol(
        roblox_handle,
        "Java_com_roblox_universalapp_messagebus_MessageBus_"
        "publishProtocolMethodResponseRaw");

    if (StartupSymbolsMissing(symbols, message_bus_publish_response_raw)) {
      std::cerr << "\n[FATAL] One or more NativeGL startup symbols were not "
                << "found.\n";
      return EXIT_FAILURE;
    }

    std::cout << "  [compat] skipped fixed-offset Stage 6 patch set\n"
              << std::flush;
    const StartupDecisions decisions =
        ResolveStartupDecisions(has_window, symbols);

    // The direct GAME path still runs CoreScripts that query app protocols.
    // Install these independently of ExperienceProtocol's dynamic launch path.
    if (game_session_runtime != nullptr &&
        !InstallGamePlatformProtocols(raw_vm, jni_vm.get(), roblox_handle,
                                      runtime_config.microphone_enabled(),
                                      &game_protocols)) {
      return EXIT_FAILURE;
    }

    if (experience_game_symbols != nullptr &&
        !InstallExperiencePlatformProtocols(
            raw_vm, jni_vm.get(), roblox_handle, *experience_game_symbols,
            &game_present_observer, input_capabilities,
            runtime_config.microphone_enabled(), &experience_composition)) {
      return EXIT_FAILURE;
    }

    EngineStartupContext startup_context_value =
        BuildEngineStartupContext(jni_vm.get(), raw_vm, options, dependencies,
                                    game_session_runtime.get(), decisions,
                                    symbols);
    if (game_session_runtime != nullptr) {
      if (!game_present_observer.Register(
              &mocktail::runtime::RobloxGameSessionRuntime::
                  SuccessfulPresentCallback,
              game_session_runtime.get())) {
        std::cerr << "[FATAL] Could not register the GAME host present "
                     "observer\n";
        return EXIT_FAILURE;
      }
      std::cout << "  [game-session] host present observer registered\n"
                << std::flush;
    }
    const int engine_startup_result = RunEngineStartup(&startup_context_value);
    if (engine_startup_result != EXIT_SUCCESS) {
      return engine_startup_result;
    }
    if (game_session_runtime != nullptr &&
        !game_session_runtime->startup_status().ok()) {
      std::cerr << "[FATAL] Typed GAME lifecycle did not start: "
                << game_session_runtime->startup_status().message() << '\n';
      return EXIT_FAILURE;
    }
    if (experience_composition != nullptr &&
        dependencies.account_identity().user_id > 0) {
      const mocktail::window::WindowSurfaceSnapshot window_surface =
          mocktail::window::GetWindowSurfaceSnapshot();
      mocktail::runtime::RobloxLuaAppExperienceReadiness readiness;
      readiness.principal.kind =
          mocktail::runtime::GameSessionPrincipalKind::kAuthenticated;
      readiness.principal.generation = 1;
      readiness.principal.principal_id =
          std::to_string(dependencies.account_identity().user_id);
      readiness.principal.base_url = "https://www.roblox.com/";
      readiness.surface = {window_surface.generation,
                           window_surface.native_window, window_surface.width,
                           window_surface.height, window_surface.dpi_scale};
      readiness.username = dependencies.account_identity().username;
      const mocktail::Status experience_status =
          experience_composition->OnLuaAppReady(std::move(readiness));
      if (!experience_status.ok()) {
        std::cerr << "[FATAL] Dynamic ExperienceProtocol did not initialize: "
                  << experience_status.message() << '\n';
        return EXIT_FAILURE;
      }
      if (dependencies.initial_launch_request().has_value()) {
        const mocktail::Status initial_launch_status =
            experience_composition->Dispatch(
                *dependencies.initial_launch_request());
        if (!initial_launch_status.ok()) {
          std::cerr << "[FATAL] Could not queue the requested experience "
                       "launch: "
                    << initial_launch_status.message() << '\n';
          return EXIT_FAILURE;
        }
      }
      experience_lifecycle_target = std::make_shared<ExperienceLifecycleTarget>(
          ExperienceLifecycleTarget{experience_composition});
      jni_vm->SetRobloxExperienceLifecycleCallbacks(
          experience_lifecycle_target,
          jnivm::RobloxExperienceLifecycleCallbacks{&NotifyLuaAppDidReturn});
      std::cout << "  [experience] subscribed to dynamic launch requests\n"
                << std::flush;
    } else if (experience_composition != nullptr) {
      if (dependencies.initial_launch_request().has_value()) {
        std::cerr << "[FATAL] A place launch needs a signed-in session, but "
                     "this run is anonymous\n";
        return EXIT_FAILURE;
      }
      std::cout << "  [experience] dynamic launch subscription awaits an "
                   "authenticated identity\n"
                << std::flush;
    }
    if (!InitializeTypedInput(raw_vm, jni_vm,
                              roblox_capabilities.input,
                              window_input_runtime,
                              text_input_bridge)) {
      return EXIT_FAILURE;
    }
    std::cout << "  [engine] legacy startup call sequence returned\n"
              << std::flush;
  }

  std::cout << "\n======================================================\n"
            << "  Bootstrap sequence returned; validating readiness.\n"
            << "======================================================\n";

  RunCompletionState completion;
  completion.input_shutdown_completed = window_input_runtime == nullptr;
  if (mocktail::window::IsInitialised()) {
    if (!started_with_launch_uri) {
      std::cout
          << "  [main] note: no --launch-uri was given, so the engine has no\n"
             "         place to render and the window will stay blank. Start\n"
             "         with a place, e.g. --launch-uri roblox://placeId=1818\n"
          << std::flush;
    }
    std::cout << "  [main] entering SDL event loop (close the window to quit)\n"
              << std::flush;
    RunMainLoop(&completion.game_surface_events_completed, game_session_runtime.get(),
                experience_composition.get());
    std::cout << "  [main] window closed, shutting down\n" << std::flush;
    completion.real_frame_presented = mocktail::window::HasPresentedFrame();
    FinishWindowBridges(text_input_bridge.get(), window_input_runtime.get(),
                        &game_protocols, &completion);
    const bool experience_destroyed_app = FinishExperienceComposition(
        jni_vm.get(), experience_composition,
        &experience_lifecycle_target, &completion);
    FinishRobloxAppLifecycle(roblox_handle, game_session_runtime.get(),
                             &game_present_observer,
                             experience_destroyed_app, &completion);
    FinishWindowShutdown(dependencies, &completion);
  } else {
    std::cerr << "[FATAL] Windowed runtime exited without a usable window.\n";
    return EXIT_FAILURE;
  }

  if (!ValidateRunCompletion(completion)) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
