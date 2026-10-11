// Part of the legacy Roblox runtime; see legacy_runtime_internal.h
// for the shared declarations. This file carries the native-settings resolution and configuration.
#include "legacy/legacy_runtime_internal.h"

#include <jni.h>

#include <iostream>

#include <iostream>

#include <cstdint>
#include <string>
#include <algorithm>
#include <string_view>

#include "jnivm/jnivm.h"
#include "window/window.h"
namespace legacy_internal {

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

NativeSettingsValues ResolveNativeSettingsValues(
    const EngineStartupContext* context) {
  std::string data_dir = GetEnvStringDefaultPath(
      "ROBLOX_ANDROID_DATA_DIR",
      "/data/user/0/com.roblox.client");
  std::string files_dir = GetEnvStringDefaultPath(
      "ROBLOX_ANDROID_FILES_DIR",
      "/data/user/0/com.roblox.client/files");
  std::string settings_cache_dir = GetEnvStringDefaultPath(
      "ROBLOX_ANDROID_SETTINGS_CACHE_DIR",
      "/data/user/0/com.roblox.client");
  std::string cache_dir = GetEnvStringDefaultPath(
      "ROBLOX_ANDROID_CACHE_DIR",
      "/data/user/0/com.roblox.client/cache");
  std::string external_base = GetEnvStringDefaultPath(
      "ROBLOX_ANDROID_EXTERNAL_BASE_DIR",
      "/sdcard/Android/data/com.roblox.client");
  std::string external_dir = GetEnvStringDefaultPath(
      "ROBLOX_ANDROID_EXTERNAL_DIR",
      "/sdcard/Android/data/com.roblox.client/files");
  std::string preferences_file = GetEnvString("ROBLOX_ANDROID_PREFERENCES_FILE",
                                              "prefs");
  std::string default_policy_file = GetEnvString(
      "ROBLOX_DEFAULT_APP_POLICY_FILE",
      "content/guac/defaultConfigs/GuacDefaultPolicy-GlobalDist.json");
  std::string base_url =
      GetEnvString("ROBLOX_BASE_URL", "https://www.roblox.com/");
  std::string api_url =
      GetEnvString("ROBLOX_API_URL", "https://api.roblox.com/");
  std::string roblox_channel =
      GetEnvString("ROBLOX_ROBLOX_CHANNEL", "production");
  std::string channel_platform_name =
      GetEnvString("ROBLOX_CHANNEL_PLATFORM_NAME", "GoogleAndroidApp");
  std::string roblox_version =
      GetEnvString("ROBLOX_ROBLOX_VERSION", "unknown");
  std::string exception_reason_filename =
      GetEnvString("ROBLOX_EXCEPTION_REASON_FILENAME",
                   "exception_reason.txt");
  std::string http_proxy_host =
      GetEnvString("ROBLOX_HTTP_PROXY_HOST", "");
  jlong http_proxy_port =
      static_cast<jlong>(GetEnvInt("ROBLOX_HTTP_PROXY_PORT", 0));
  std::string cookie_base_url =
      GetEnvString("ROBLOX_COOKIE_BASE_URL", "https://www.roblox.com/");
  std::string cookie_domain =
      GetEnvString("ROBLOX_COOKIE_DOMAIN", "roblox.com");
  const roblox::runtime::SecureRobloxCredential* credential =
      context->roblox_credential;
  const std::string_view roblox_cookies =
      credential != nullptr ? credential->view() : std::string_view();
  std::string cookie_manager_cookies =
      CookieForJNICookieManager(roblox_cookies, cookie_domain);
  if (!roblox_cookies.empty()) {
    std::cout << "  [engine] typed credential prepared bytes="
              << roblox_cookies.size() << '\n'
              << std::flush;
  } else if (IsEnabled("ROBLOX_ALLOW_NO_COOKIE_LUA_APP")) {
    std::cout << "  [engine] no Roblox cookie found; proceeding without login\n"
              << std::flush;
  } else {
    std::cout << "  [engine] no Roblox credential found; proceeding without login\n"
              << std::flush;
  }
  std::string android_id =
      GetEnvString("ROBLOX_ANDROID_ID", "0000000000000000");
  std::string advertising_id =
      GetEnvString("ROBLOX_ADVERTISING_ID", "");
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
  roblox::platform::SecureClear(&values.cookie_manager_cookies);
}

void ConfigureLocalStorage(JNIEnv* env, const EngineStartupContext* context) {
  if (!env || !context) {
    return;
  }
  std::string data_dir = GetEnvStringDefaultPath(
      "ROBLOX_ANDROID_DATA_DIR",
      "/data/user/0/com.roblox.client");
  std::string files_dir = GetEnvStringDefaultPath(
      "ROBLOX_ANDROID_FILES_DIR",
      "/data/user/0/com.roblox.client/files");
  std::string cache_dir = GetEnvStringDefaultPath(
      "ROBLOX_ANDROID_CACHE_DIR",
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

void ReportDisplayRefreshRates(EngineStartupContext* context, JNIEnv*& env,
                            jclass native_gl_class) {
const roblox::platform::DisplayRefreshCapabilities display_refresh =
    roblox::window::GetDisplayRefreshCapabilities();
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

}  // namespace legacy_internal
