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
#include "legacy/legacy_runtime_internal.h"
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

void PrintBacktraceNoSig(const char* prefix);
void PrintContextBacktrace(ucontext_t* ucontext, const char* prefix);

using JniOnLoadFn = jint (*)(JavaVM*, void*);

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


// dlsym searches the object and its dependency chain, so a
// stub that transitively links SDL or another host library would
// satisfy EGL/GL/Vulkan lookups with unrelated host symbols
//
// So, we should only accept the symbol if its stub owns it.


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


jobject BuildPlatformParams(JNIEnv* env, jobject surface);


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


// Every path, URL and identity value ConfigureNativeSettings hands to the
// engine. Each one comes from the environment with an Android-shaped
// default, so they are resolved together up front and then passed around as
// a single value instead of thirty separate locals.

// Resolves those values, and says once, up front, whether a Roblox
// credential was found -- so the caller can tell a signed-in cold start from
// an anonymous one before it starts calling into the engine.

// Lay down every directory the engine expects to already exist. It creates
// some of them itself but not all, and a missing parent makes its own
// boost::filesystem::create_directory fail rather than create, so this has
// to happen before the first native call.


// Resolve Android adapters from the runtime bundle even when a launcher
// relocates the executable. Host libraries with the same unversioned SONAME
// do not implement the Android ABI (notably VK_KHR_android_surface).


// The JNIEnv for the startup thread, re-acquired if a previous engine call
// left it cleared. Written back through the reference because re-attaching is
// observable to every later step.


// Tells the engine what the display can do: first its current refresh rate, then
// optionally the full list of supported rates. Both are optional in their own
// right.

// The last two notifications the engine waits on: that the app finished coming
// up, and that the joined experience finished loading. Both are guarded so a
// faulting call still lets startup continue to the next step.
}  // namespace legacy_internal
