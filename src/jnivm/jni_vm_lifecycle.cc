#include "jnivm/jnivm.h"

#include "jni_helpers.h"
#include "jni_references.h"
#include "jni_fields.h"
#include "jni_strings.h"

#include "mocktail/audio/fmod_thread_floating_point.h"
#include "mocktail/platform/posix_primitives.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <list>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/display_size.h"


namespace jnivm {

using namespace internal;

// VM construction, teardown, and the JNIEnv/class entry points.

jobject CreateAndroidConfiguration(JNIEnv* env) {
  if (env == nullptr) {
    return nullptr;
  }
  jclass clazz = env->FindClass("android/content/res/Configuration");
  if (clazz == nullptr) {
    return nullptr;
  }
  jobject configuration = env->AllocObject(clazz);
  env->DeleteLocalRef(clazz);
  if (configuration == nullptr) {
    return nullptr;
  }
  const PlatformIdentity identity =
      CurrentVM() != nullptr ? CurrentVM()->GetPlatformIdentitySnapshot()
                             : PlatformIdentity{};
  SetIntFieldRaw(configuration, "colorMode", 0);
  SetIntFieldRaw(configuration, "densityDpi", 160);
  SetIntFieldRaw(configuration, "fontWeightAdjustment", 0);
  SetIntFieldRaw(configuration, "hardKeyboardHidden",
                 identity.keyboard_enabled ? 1 : 2);
  SetIntFieldRaw(configuration, "keyboard", identity.keyboard_enabled ? 2 : 1);
  SetIntFieldRaw(configuration, "keyboardHidden",
                 identity.keyboard_enabled ? 1 : 2);
  SetIntFieldRaw(configuration, "mcc", 0);
  SetIntFieldRaw(configuration, "mnc", 0);
  SetIntFieldRaw(configuration, "navigation", 1);
  SetIntFieldRaw(configuration, "navigationHidden", 1);
  SetIntFieldRaw(configuration, "orientation", 2);
  const mocktail::runtime::DisplaySize config_window =
      mocktail::runtime::ParseDisplaySize(
          std::getenv(mocktail::runtime::kWindowSizeEnvironment));
  SetIntFieldRaw(configuration, "screenHeightDp", config_window.height);
  SetIntFieldRaw(configuration, "screenLayout", 0);
  SetIntFieldRaw(configuration, "screenWidthDp", config_window.width);
  SetIntFieldRaw(configuration, "smallestScreenWidthDp",
                 std::min(config_window.width, config_window.height));
  SetIntFieldRaw(configuration, "touchscreen", identity.touch_enabled ? 3 : 1);
  SetIntFieldRaw(configuration, "uiMode", 0);
  return configuration;
}

VM::VM() {
  InitJNIFunctionTables();
  std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
  g_live_vms.push_back(this);
}

VM::~VM() {
  {
    std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
    g_live_vms.erase(std::remove(g_live_vms.begin(), g_live_vms.end(), this),
                    g_live_vms.end());
    if (g_thread_vm_instance == this) {
      g_thread_audio_fp_mode.Restore();
      g_thread_vm_instance = nullptr;
      g_thread_local_env = nullptr;
      g_thread_env_storage.functions = nullptr;
    }
  }
  {
    std::lock_guard<std::mutex> lock(message_bus_async_request_handler_mutex_);
    message_bus_async_request_handler_bindings_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(message_bus_raw_mutex_);
    message_bus_raw_bindings_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(message_bus_request_handler_mutex_);
    message_bus_request_handler_bindings_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(mem_storage_callback_mutex_);
    mem_storage_callback_bindings_.clear();
  }
  ClearRobloxDataModelNotificationCallbacks();
  ClearRobloxExperienceLifecycleCallbacks();
  ClearRobloxCredentialSink();
  ClearRobloxCredentialProvider();
  ClearRobloxTextInputCallbacks();
  ClearAndroidWindowCallbacks();
  ClearWebRtcAudioManagerCallbacks();
  ClearWebRtcAudioTrackCallbacks();
  ClearWebRtcAudioRecordCallbacks();
  ClearFmodAudioDeviceCallbacks();
}

VM* VM::FromJavaVM(JavaVM* java_vm) {
  std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
  for (VM* vm : g_live_vms) {
    if (vm->java_vm_ == java_vm) {
      return vm;
    }
  }
  return nullptr;
}

JNIEnv* VM::GetJNIEnv() {
  std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
  if (JniVmTraceEnabled()) {
    std::cout << "  [JNI] GetJNIEnv enter\n";
  }
  if (!jni_env_) {
    jni_env_ = &jni_env_storage_;
  }
  jni_env_->functions = &native_interface_;
  if (g_thread_vm_instance == this && IsThreadLocalEnvValid()) {
    if (JniVmTraceEnabled()) {
      std::cout << "  [JNI] GetJNIEnv thread-local hit\n";
    }
    return g_thread_local_env;
  }
  g_thread_audio_fp_mode.Restore();
  g_thread_vm_instance = this;
  g_thread_env_storage.functions = &native_interface_;
  g_thread_local_env = &g_thread_env_storage;
  if (JniVmTraceEnabled()) {
    std::cout << "  [JNI] GetJNIEnv attached thread-local env\n";
  }
  return g_thread_local_env;
}

void VM::RestoreFunctions() {
  std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
  // JNI_OnLoad replaces env->functions; restore all known environments.
  java_vm_storage_.functions = &invoke_interface_;
  java_vm_ = &java_vm_storage_;
  if (jni_env_) {
    jni_env_->functions = &native_interface_;
  }
  if (g_thread_vm_instance == this && IsThreadLocalEnvValid()) {
    g_thread_local_env->functions = &native_interface_;
  }
}

std::shared_ptr<Class> VM::RegisterClass(const std::string& class_name) {
  if (JniVmTraceEnabled()) {
    fprintf(stderr, "  [JNI-VM] RegisterClass this=%p name_ref=%p name=\"%s\"\n",
            static_cast<void*>(this), static_cast<const void*>(&class_name),
            class_name.c_str());
  }
  std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
  auto it = class_registry_.find(class_name);
  if (it != class_registry_.end()) {
    return it->second;
  }
  auto cls = std::make_shared<Class>(class_name);
  class_registry_[class_name] = cls;
  return cls;
}

std::shared_ptr<Class> VM::FindClass(const std::string& class_name) const {
  std::lock_guard<std::recursive_mutex> lock(g_jni_state_mutex);
  auto it = class_registry_.find(class_name);
  if (it == class_registry_.end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace jnivm
