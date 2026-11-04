// Part of the legacy Roblox runtime; see legacy_runtime_internal.h
// for the shared declarations. This file carries the engine symbol resolution and registration.
#include "legacy/legacy_runtime_internal.h"

#include <jni.h>

#include <iostream>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <link.h>

#include "linker/linker.h"
#include "roblox/graphics/bionic_egl_bridge.h"
#include "window/window.h"
namespace legacy_internal {

bool IsGlSymbol(const char* name) {
  return StartsWith(name, "gl");
}

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
    result.address = roblox::window::GetGLProcAddress(name);
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

bool IsRobloxStubGlesPath(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return true;
  }
  std::string value(path);
  return value == "libGLESv2.so" ||
         value.find("/build/libGLESv2.so") != std::string::npos;
}

void* OpenRealGlesLibrary() {
  const char* env_library = std::getenv("ROBLOX_GLES_LIBRARY");
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
    if (IsRobloxStubGlesPath(candidate)) {
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

static std::string RuntimeBionicAdapterPath(const char* name) {
  const char* override_dir = std::getenv("ROBLOX_RUNTIME_LIBRARY_DIR");
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

constexpr const char* kSymbolsToRegister[] = {
    // Symbols exported by the Android compatibility stubs.
    "AAsset_getLength64",
    "AAsset_getRemainingLength",
    "AAsset_getRemainingLength64",
    "AAsset_openFileDescriptor64",
    "AAsset_read",
    "AAsset_seek",
    "AAsset_seek64",
    // Camera2 NDK exports. Roblox resolves these by name after dlopen.
    "ACameraCaptureSession_abortCaptures",
    "ACameraCaptureSession_capture",
    "ACameraCaptureSession_close",
    "ACameraCaptureSession_setRepeatingRequest",
    "ACameraCaptureSession_stopRepeating",
    "ACameraDevice_close",
    "ACameraDevice_createCaptureRequest",
    "ACameraDevice_createCaptureSession",
    "ACameraManager_create",
    "ACameraManager_delete",
    "ACameraManager_deleteCameraIdList",
    "ACameraManager_getCameraCharacteristics",
    "ACameraManager_getCameraIdList",
    "ACameraManager_openCamera",
    "ACameraManager_registerAvailabilityCallback",
    "ACameraManager_unregisterAvailabilityCallback",
    "ACameraMetadata_free",
    "ACameraMetadata_getAllTags",
    "ACameraMetadata_getConstEntry",
    "ACameraOutputTarget_create",
    "ACameraOutputTarget_free",
    "ACaptureRequest_addTarget",
    "ACaptureRequest_free",
    "ACaptureRequest_removeTarget",
    "ACaptureRequest_setEntry_i32",
    "ACaptureSessionOutputContainer_add",
    "ACaptureSessionOutputContainer_create",
    "ACaptureSessionOutputContainer_free",
    "ACaptureSessionOutputContainer_remove",
    "ACaptureSessionOutput_create",
    "ACaptureSessionOutput_free",
    // Media NDK image exports used by the camera shim and its readers.
    "AImage_delete",
    "AImage_deleteAsync",
    "AImage_getCropRect",
    "AImage_getFormat",
    "AImage_getHardwareBuffer",
    "AImage_getHeight",
    "AImage_getNumberOfPlanes",
    "AImage_getPlaneData",
    "AImage_getPlanePixelStride",
    "AImage_getPlaneRowStride",
    "AImage_getTimestamp",
    "AImage_getWidth",
    "AImageReader_acquireLatestImage",
    "AImageReader_acquireLatestImageAsync",
    "AImageReader_acquireNextImage",
    "AImageReader_acquireNextImageAsync",
    "AImageReader_delete",
    "AImageReader_getFormat",
    "AImageReader_getHeight",
    "AImageReader_getMaxImages",
    "AImageReader_getWidth",
    "AImageReader_getWindow",
    "AImageReader_new",
    "AImageReader_newWithDataSpace",
    "AImageReader_newWithUsage",
    "AImageReader_setBufferRemovedListener",
    "AImageReader_setImageListener",
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
    "roblox_recover_stack_chk_fail",

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

RobloxEngineSymbols ResolveRobloxEngineSymbols(
    void* roblox_handle,
    const roblox::runtime::RobloxStartupSymbols& startup_symbols,
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

BionicStubPreload PreloadBionicStubs(
    const roblox::graphics::BionicEglBridge& bionic_egl_bridge) {
  std::vector<const char*> stub_names = {
      "libc.so", "libdl.so", "libm.so", "libz.so",
      "libandroid.so", "liblog.so", "libmediandk.so", "libcamera2ndk.so",
      "libOpenSLES.so", "libOpenMAXAL.so",
      "libEGL.so", "libGLESv2.so",
  };
  if (IsEnabled("ROBLOX_PRELOAD_VULKAN_SHIM")) {
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

}  // namespace legacy_internal
