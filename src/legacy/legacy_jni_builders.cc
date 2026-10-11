// Part of the legacy Roblox runtime; see legacy_runtime_internal.h
// for the shared declarations. This file carries the JNI object and field builders.
#include "legacy/legacy_runtime_internal.h"

#include <jni.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "jnivm/jnivm.h"
#include "runtime/display_size.h"
#include "compat/guest_abi.h"
#include "runtime/texture_memory_policy.h"
#include "runtime/device_memory_profile.h"
#include "window/window.h"
namespace legacy_internal {

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

JNIEnv* EnsureStartupEnv(EngineStartupContext* context, JNIEnv*& env) {
  if (!env) {
    env = context->vm->GetJNIEnv();
  }
  return env;
}

}  // namespace legacy_internal
