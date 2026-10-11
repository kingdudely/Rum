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


// The Vulkan entry points the adapter can stand in for. Only addresses the
// adapter itself owns are registered, so anything that resolves into the
// real driver still reaches the driver.

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
                game_session_runtime));
  } else if (experience_composition != nullptr &&
             experience_composition->subscribed()) {
    mocktail::window::WindowGameSurfaceConsumer experience_consumer{
        experience_composition, &ExperienceSurfaceCreated,
        &ExperienceSurfaceChanged, &ExperienceSurfaceDestroyed};
    surface_bridge =
        std::make_unique<mocktail::window::WindowGameSurfaceBridge>(
            mocktail::window::MakeWindowGameSurfaceEventSource(),
            experience_consumer);
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
};

// Shuts audio down ahead of the SDL teardown. Audio that refuses to release
// the device fails the run rather than tearing SDL down underneath it.
void FinishWindowShutdown(mocktail::legacy::RuntimeDependencies& dependencies,
                          RunCompletionState* completion) {
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

// One pass over kSymbolsToRegister, summarised so the caller can report
// where each GL symbol came from.

// One GL name's contribution to the audit. Shared by both resolution passes so
// the totals mean the same thing whichever pass supplied the address.

// Opens every Bionic library the guest can import from and keeps the
// handles. EGL and Vulkan prefer our own adapter over whatever the host
// happens to have, since the guest resolves those two by SONAME.

// Walks kSymbolsToRegister and binds each name to whatever can satisfy it:
// for GL, the real GLES library first and then our stubs; otherwise the
// host. Anything still unresolved is counted, not registered as null.

// The Vulkan adapter exposes the loader entry points under both
// libvulkan.so and libvulkan.so.1, so the guest resolves them whichever
// SONAME it happens to import.

// libroblox.so declares several hundred imports, far more than the
// kSymbolsToRegister table lists by hand. Rather than maintain a second
// list that drifts, walk its .dynsym once and let the same resolver bind
// anything it recognises. The file is mapped read-only and never run, and
// the map is released before returning.


}  // namespace legacy_internal

// Run itself lives outside legacy_internal (public API); the directive
// below restores the unqualified names the body has always used.
using namespace legacy_internal;

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

