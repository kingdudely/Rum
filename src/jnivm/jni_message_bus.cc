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

// the message bus and in-memory storage native callbacks.

jobject
VM::CreateMessageBusRawCallback(std::shared_ptr<void> context,
                                const MessageBusRawCallbacks &callbacks) {
  if (context == nullptr || callbacks.run == nullptr) {
    return nullptr;
  }
  JNIEnv *env = GetJNIEnv();
  if (env == nullptr) {
    return nullptr;
  }
  jclass callback_class =
      env->FindClass("com/roblox/universalapp/messagebus/RawCallback");
  if (callback_class == nullptr) {
    return nullptr;
  }
  jobject callback = env->AllocObject(callback_class);
  env->DeleteLocalRef(callback_class);
  if (callback == nullptr) {
    return nullptr;
  }
  auto binding = std::make_shared<MessageBusRawBinding>();
  binding->context = std::move(context);
  binding->callbacks = callbacks;
  {
    std::lock_guard<std::mutex> lock(message_bus_raw_mutex_);
    message_bus_raw_bindings_[callback] = std::move(binding);
  }
  return callback;
}

void VM::ClearMessageBusRawCallback(jobject callback) {
  std::shared_ptr<MessageBusRawBinding> old_binding;
  {
    std::lock_guard<std::mutex> lock(message_bus_raw_mutex_);
    const auto found = message_bus_raw_bindings_.find(callback);
    if (found == message_bus_raw_bindings_.end()) {
      return;
    }
    old_binding = std::move(found->second);
    message_bus_raw_bindings_.erase(found);
  }
}

bool VM::DispatchMessageBusRawCallback(jobject callback, JNIEnv *env,
                                       jstring message) {
  std::shared_ptr<MessageBusRawBinding> binding;
  {
    std::lock_guard<std::mutex> lock(message_bus_raw_mutex_);
    const auto found = message_bus_raw_bindings_.find(callback);
    if (found != message_bus_raw_bindings_.end()) {
      binding = found->second;
    }
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.run == nullptr || env == nullptr ||
      message == nullptr) {
    return false;
  }
  binding->callbacks.run(binding->context.get(), env, message);
  return true;
}

jobject VM::CreateMessageBusRequestHandler(
    std::shared_ptr<void> context,
    const MessageBusRequestHandlerCallbacks& callbacks) {
  if (context == nullptr || callbacks.run == nullptr) {
    return nullptr;
  }
  JNIEnv* env = GetJNIEnv();
  jclass handler_class = env != nullptr
                             ? env->FindClass(
                                   "com/roblox/universalapp/messagebus/"
                                   "RequestHandlerRaw")
                             : nullptr;
  if (handler_class == nullptr) {
    return nullptr;
  }
  jobject handler = env->AllocObject(handler_class);
  env->DeleteLocalRef(handler_class);
  if (handler == nullptr) {
    return nullptr;
  }
  auto binding = std::make_shared<MessageBusRequestHandlerBinding>();
  binding->context = std::move(context);
  binding->callbacks = callbacks;
  std::lock_guard<std::mutex> lock(message_bus_request_handler_mutex_);
  message_bus_request_handler_bindings_[handler] = std::move(binding);
  return handler;
}

void VM::ClearMessageBusRequestHandler(jobject handler) {
  std::lock_guard<std::mutex> lock(message_bus_request_handler_mutex_);
  message_bus_request_handler_bindings_.erase(handler);
}

jstring VM::DispatchMessageBusRequestHandler(jobject handler, JNIEnv* env,
                                             jstring message) {
  std::shared_ptr<MessageBusRequestHandlerBinding> binding;
  {
    std::lock_guard<std::mutex> lock(message_bus_request_handler_mutex_);
    const auto found = message_bus_request_handler_bindings_.find(handler);
    if (found != message_bus_request_handler_bindings_.end()) {
      binding = found->second;
    }
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.run == nullptr || env == nullptr) {
    return nullptr;
  }
  const std::string response =
      binding->callbacks.run(binding->context.get(), env, message);
  return env->NewStringUTF(response.c_str());
}

jobject VM::CreateMessageBusAsyncRequestHandler(
    std::shared_ptr<void> context,
    const MessageBusAsyncRequestHandlerCallbacks& callbacks) {
  if (context == nullptr || callbacks.run == nullptr) return nullptr;
  JNIEnv* env = GetJNIEnv();
  if (env == nullptr) return nullptr;
  jclass cls = env->FindClass(
      "com/roblox/universalapp/messagebus/RequestHandlerAsyncRaw");
  if (cls == nullptr) return nullptr;
  jobject handler = env->AllocObject(cls);
  env->DeleteLocalRef(cls);
  if (handler == nullptr) return nullptr;
  auto binding = std::make_shared<MessageBusAsyncRequestHandlerBinding>();
  binding->context = std::move(context);
  binding->callbacks = callbacks;
  std::lock_guard<std::mutex> lock(message_bus_async_request_handler_mutex_);
  message_bus_async_request_handler_bindings_[handler] = std::move(binding);
  return handler;
}

void VM::ClearMessageBusAsyncRequestHandler(jobject handler) {
  std::shared_ptr<MessageBusAsyncRequestHandlerBinding> removed;
  {
    std::lock_guard<std::mutex> lock(message_bus_async_request_handler_mutex_);
    const auto found =
        message_bus_async_request_handler_bindings_.find(handler);
    if (found == message_bus_async_request_handler_bindings_.end()) return;
    removed = std::move(found->second);
    message_bus_async_request_handler_bindings_.erase(found);
  }
}

bool VM::DispatchMessageBusAsyncRequestHandler(jobject handler, JNIEnv* env,
                                               jstring message,
                                               jstring response_id) {
  std::shared_ptr<MessageBusAsyncRequestHandlerBinding> binding;
  {
    std::lock_guard<std::mutex> lock(message_bus_async_request_handler_mutex_);
    const auto found =
        message_bus_async_request_handler_bindings_.find(handler);
    if (found != message_bus_async_request_handler_bindings_.end()) {
      binding = found->second;
    }
  }
  if (binding == nullptr || env == nullptr || response_id == nullptr) {
    return false;
  }
  binding->callbacks.run(binding->context.get(), env, message, response_id);
  return true;
}

jobject VM::CreateMemStorageCallback(
    std::shared_ptr<void> context,
    const MemStorageCallbackCallbacks& callbacks) {
  if (context == nullptr || callbacks.on_item_set == nullptr) {
    return nullptr;
  }
  JNIEnv* env = GetJNIEnv();
  if (env == nullptr) {
    return nullptr;
  }
  jclass callback_class =
      env->FindClass("com/roblox/engine/jni/memstorage/Callback");
  if (callback_class == nullptr) {
    return nullptr;
  }
  jobject callback = env->AllocObject(callback_class);
  env->DeleteLocalRef(callback_class);
  if (callback == nullptr) {
    return nullptr;
  }

  auto binding = std::make_shared<MemStorageCallbackBinding>();
  binding->context = std::move(context);
  binding->callbacks = callbacks;
  {
    std::lock_guard<std::mutex> lock(mem_storage_callback_mutex_);
    mem_storage_callback_bindings_[callback] = std::move(binding);
  }
  return callback;
}

void VM::ClearMemStorageCallback(jobject callback) {
  std::shared_ptr<MemStorageCallbackBinding> old_binding;
  {
    std::lock_guard<std::mutex> lock(mem_storage_callback_mutex_);
    const auto found = mem_storage_callback_bindings_.find(callback);
    if (found == mem_storage_callback_bindings_.end()) {
      return;
    }
    old_binding = std::move(found->second);
    mem_storage_callback_bindings_.erase(found);
  }
}

bool VM::DispatchMemStorageCallback(jobject callback, JNIEnv *env,
                                    jstring value) {
  std::shared_ptr<MemStorageCallbackBinding> binding;
  {
    std::lock_guard<std::mutex> lock(mem_storage_callback_mutex_);
    const auto found = mem_storage_callback_bindings_.find(callback);
    if (found != mem_storage_callback_bindings_.end()) {
      binding = found->second;
    }
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.on_item_set == nullptr || env == nullptr ||
      value == nullptr) {
    return false;
  }
  binding->callbacks.on_item_set(binding->context.get(), env, value);
  return true;
}

void VM::SetRobloxDataModelNotificationCallbacks(
    std::shared_ptr<void> context,
    const RobloxDataModelNotificationCallbacks &callbacks) {
  std::shared_ptr<RobloxDataModelNotificationBinding> binding;
  if (context != nullptr && callbacks.on_notification != nullptr) {
    binding = std::make_shared<RobloxDataModelNotificationBinding>();
    binding->context = std::move(context);
    binding->callbacks = callbacks;
  }
  std::shared_ptr<RobloxDataModelNotificationBinding> old_binding;
  {
    std::lock_guard<std::mutex> lock(roblox_data_model_notification_mutex_);
    old_binding = std::move(roblox_data_model_notification_binding_);
    roblox_data_model_notification_binding_ = std::move(binding);
  }
}

void VM::ClearRobloxDataModelNotificationCallbacks() {
  std::shared_ptr<RobloxDataModelNotificationBinding> old_binding;
  {
    std::lock_guard<std::mutex> lock(roblox_data_model_notification_mutex_);
    old_binding = std::move(roblox_data_model_notification_binding_);
  }
}

bool VM::DispatchRobloxDataModelNotification(JNIEnv *env, jstring type,
                                             jstring data) {
  std::shared_ptr<RobloxDataModelNotificationBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_data_model_notification_mutex_);
    binding = roblox_data_model_notification_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.on_notification == nullptr || env == nullptr ||
      type == nullptr || data == nullptr) {
    return false;
  }
  binding->callbacks.on_notification(binding->context.get(), env, type, data);
  return true;
}

bool VM::DispatchRobloxAppBridgeNotification(JNIEnv* env, jstring type,
                                             jstring data) {
  std::shared_ptr<RobloxDataModelNotificationBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_data_model_notification_mutex_);
    binding = roblox_data_model_notification_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.on_app_bridge_notification == nullptr ||
      env == nullptr || type == nullptr || data == nullptr) {
    return false;
  }
  binding->callbacks.on_app_bridge_notification(binding->context.get(), env,
                                                type, data);
  return true;
}

bool VM::DispatchRobloxNativeOverlay(JNIEnv* env, jstring title, jstring url) {
  std::shared_ptr<RobloxDataModelNotificationBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_data_model_notification_mutex_);
    binding = roblox_data_model_notification_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.on_native_overlay == nullptr || env == nullptr ||
      title == nullptr || url == nullptr) {
    return false;
  }
  binding->callbacks.on_native_overlay(binding->context.get(), env, title, url);
  return true;
}

bool VM::DispatchRobloxOpenWebActivity(JNIEnv* env, jstring url,
                                       jstring title) {
  std::shared_ptr<RobloxDataModelNotificationBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_data_model_notification_mutex_);
    binding = roblox_data_model_notification_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.on_open_web_activity == nullptr || env == nullptr ||
      url == nullptr || title == nullptr) {
    return false;
  }
  binding->callbacks.on_open_web_activity(binding->context.get(), env, url,
                                          title);
  return true;
}

bool VM::DispatchRobloxCookieSync(JNIEnv* env, jstring cookie) {
  std::shared_ptr<RobloxDataModelNotificationBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_data_model_notification_mutex_);
    binding = roblox_data_model_notification_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.on_sync_cookies == nullptr || env == nullptr ||
      cookie == nullptr) {
    return false;
  }
  binding->callbacks.on_sync_cookies(binding->context.get(), env, cookie);
  return true;
}

bool VM::DispatchRobloxCookieSet(JNIEnv* env, jobjectArray cookies,
                                jstring url) {
  std::shared_ptr<RobloxDataModelNotificationBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_data_model_notification_mutex_);
    binding = roblox_data_model_notification_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.on_set_cookie == nullptr || env == nullptr ||
      cookies == nullptr || url == nullptr) {
    return false;
  }
  const jsize cookie_count = env->GetArrayLength(cookies);
  if (cookie_count < 0 || cookie_count > kMaximumCookieSetCount) {
    return false;
  }
  for (jsize index = 0; index < cookie_count; ++index) {
    auto cookie =
        static_cast<jstring>(env->GetObjectArrayElement(cookies, index));
    if (cookie == nullptr) {
      continue;
    }
    const jsize cookie_size = env->GetStringUTFLength(cookie);
    if (cookie_size <= 0 || cookie_size > kMaximumCookieSetBytes) {
      env->DeleteLocalRef(cookie);
      continue;
    }
    const char* cookie_chars = env->GetStringUTFChars(cookie, nullptr);
    if (cookie_chars == nullptr) {
      env->DeleteLocalRef(cookie);
      continue;
    }
    std::string raw_cookie(cookie_chars,
                           static_cast<std::size_t>(cookie_size));
    env->ReleaseStringUTFChars(cookie, cookie_chars);
    env->DeleteLocalRef(cookie);
    std::string canonical_cookie = NormalizeCookieHeader(raw_cookie);
    ClearCookieString(&raw_cookie);
    if (canonical_cookie.empty()) {
      continue;
    }
    StoreCookieHeader(canonical_cookie);
    jstring callback_cookie = env->NewStringUTF(canonical_cookie.c_str());
    ClearCookieString(&canonical_cookie);
    if (callback_cookie == nullptr) {
      continue;
    }
    binding->callbacks.on_set_cookie(binding->context.get(), env,
                                     callback_cookie, url);
    env->DeleteLocalRef(callback_cookie);
  }
  return true;
}

}  // namespace jnivm
