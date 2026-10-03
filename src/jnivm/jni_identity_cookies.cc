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

// window callbacks, identities, credentials, and cookies.

void VM::SetRobloxExperienceLifecycleCallbacks(
    std::shared_ptr<void> context,
    const RobloxExperienceLifecycleCallbacks &callbacks) {
  std::shared_ptr<RobloxExperienceLifecycleBinding> binding;
  if (context != nullptr && callbacks.on_lua_app_did_return != nullptr) {
    binding = std::make_shared<RobloxExperienceLifecycleBinding>();
    binding->context = std::move(context);
    binding->callbacks = callbacks;
  }
  std::shared_ptr<RobloxExperienceLifecycleBinding> old_binding;
  {
    std::lock_guard<std::mutex> lock(roblox_experience_lifecycle_mutex_);
    old_binding = std::move(roblox_experience_lifecycle_binding_);
    roblox_experience_lifecycle_binding_ = std::move(binding);
  }
}

void VM::ClearRobloxExperienceLifecycleCallbacks() {
  std::shared_ptr<RobloxExperienceLifecycleBinding> old_binding;
  {
    std::lock_guard<std::mutex> lock(roblox_experience_lifecycle_mutex_);
    old_binding = std::move(roblox_experience_lifecycle_binding_);
  }
}

bool VM::DispatchRobloxExperienceLuaAppDidReturn() {
  std::shared_ptr<RobloxExperienceLifecycleBinding> binding;
  {
    std::lock_guard<std::mutex> lock(roblox_experience_lifecycle_mutex_);
    binding = roblox_experience_lifecycle_binding_;
  }
  if (binding == nullptr || binding->context == nullptr ||
      binding->callbacks.on_lua_app_did_return == nullptr) {
    return false;
  }
  binding->callbacks.on_lua_app_did_return(binding->context.get());
  return true;
}

void VM::SetAndroidWindowCallbacks(
    std::shared_ptr<void> context,
    const AndroidWindowCallbacks& callbacks) {
  std::lock_guard<std::mutex> lock(android_window_mutex_);
  android_window_binding_.context = std::move(context);
  android_window_binding_.callbacks = callbacks;
}

void VM::ClearAndroidWindowCallbacks() {
  std::lock_guard<std::mutex> lock(android_window_mutex_);
  android_window_binding_ = {};
}

bool VM::DispatchAndroidWindowFlags(int flags, int mask) {
  AndroidWindowBinding binding;
  {
    std::lock_guard<std::mutex> lock(android_window_mutex_);
    binding = android_window_binding_;
  }
  return binding.context != nullptr && binding.callbacks.set_flags != nullptr &&
         binding.callbacks.set_flags(binding.context.get(), flags, mask);
}

void VM::SetRobloxAuthIdentity(const RobloxAuthIdentity& identity) {
  std::lock_guard<std::mutex> lock(roblox_auth_identity_mutex_);
  roblox_auth_identity_ =
      identity.user_id > 0 ? identity : RobloxAuthIdentity{};
}

void VM::ClearRobloxAuthIdentity() {
  std::lock_guard<std::mutex> lock(roblox_auth_identity_mutex_);
  roblox_auth_identity_ = {};
}

RobloxAuthIdentity VM::GetRobloxAuthIdentitySnapshot() const {
  std::lock_guard<std::mutex> lock(roblox_auth_identity_mutex_);
  return roblox_auth_identity_;
}

void VM::SetPlatformIdentity(const PlatformIdentity& identity) {
  std::lock_guard<std::mutex> lock(platform_identity_mutex_);
  platform_identity_ = identity;
}

PlatformIdentity VM::GetPlatformIdentitySnapshot() const {
  std::lock_guard<std::mutex> lock(platform_identity_mutex_);
  return platform_identity_;
}

void VM::SetRobloxCredentialProvider(const void* context,
                                     RobloxCredentialProvider provider) {
  {
    std::lock_guard<std::mutex> lock(roblox_credential_provider_mutex_);
    ClearCookieString(&roblox_credential_override_);
    roblox_credential_provider_context_ = context;
    roblox_credential_provider_ = provider;
  }
  if (provider != nullptr) {
    ClearLegacyCookieStore();
  }
}

void VM::ClearRobloxCredentialProvider() {
  std::lock_guard<std::mutex> lock(roblox_credential_provider_mutex_);
  ClearCookieString(&roblox_credential_override_);
  roblox_credential_provider_ = nullptr;
  roblox_credential_provider_context_ = nullptr;
}

bool VM::CopyRobloxCredentialFromProvider(std::string* credential) const {
  std::lock_guard<std::mutex> lock(roblox_credential_provider_mutex_);
  if (!roblox_credential_override_.empty()) {
    if (credential != nullptr) {
      credential->assign(roblox_credential_override_);
    }
    return true;
  }
  if (roblox_credential_provider_ == nullptr) {
    if (credential != nullptr) {
      credential->clear();
    }
    return false;
  }
  if (credential == nullptr) {
    return true;
  }
  const RobloxCredentialView view =
      roblox_credential_provider_(roblox_credential_provider_context_);
  if (view.data == nullptr || view.size == 0) {
    credential->clear();
  } else {
    credential->assign(view.data, view.size);
  }
  return true;
}

void VM::SetRobloxCookieGetter(RobloxCookieGetter getter) {
  std::lock_guard<std::mutex> lock(roblox_cookie_getter_mutex_);
  roblox_cookie_getter_ = getter;
}

bool VM::RefreshRobloxCredentialFromEngine(JNIEnv* env) {
  RobloxCookieGetter getter = nullptr;
  {
    std::lock_guard<std::mutex> lock(roblox_cookie_getter_mutex_);
    getter = roblox_cookie_getter_;
  }
  if (getter == nullptr || env == nullptr) {
    return false;
  }

  jstring domain = env->NewStringUTF("https://www.roblox.com/");
  if (domain == nullptr) {
    return false;
  }
  jstring cookies = getter(env, nullptr, domain);
  env->DeleteLocalRef(domain);
  if (cookies == nullptr) {
    return false;
  }

  std::string raw_cookie = StringFromJString(cookies);
  env->DeleteLocalRef(cookies);
  std::string canonical_cookie = NormalizeCookieHeader(raw_cookie);
  ClearCookieString(&raw_cookie);
  if (canonical_cookie.empty()) {
    return false;
  }
  StoreCookieHeader(canonical_cookie);
  ClearCookieString(&canonical_cookie);
  return true;
}

void VM::SetRobloxCredentialSink(
    std::shared_ptr<void> context,
    const RobloxCredentialSinkCallbacks& callbacks) {
  std::lock_guard<std::mutex> lock(roblox_credential_sink_mutex_);
  roblox_credential_sink_binding_.context = std::move(context);
  roblox_credential_sink_binding_.callbacks = callbacks;
}

void VM::ClearRobloxCredentialSink() {
  RobloxCredentialSinkBinding old_binding;
  {
    std::lock_guard<std::mutex> lock(roblox_credential_sink_mutex_);
    old_binding = std::move(roblox_credential_sink_binding_);
    roblox_credential_sink_binding_ = {};
  }
}

bool VM::DispatchRobloxCredential(const char* data, std::size_t size) {
  RobloxCredentialSinkBinding binding;
  {
    std::lock_guard<std::mutex> lock(roblox_credential_sink_mutex_);
    binding = roblox_credential_sink_binding_;
  }
  const bool stored =
      binding.context != nullptr && binding.callbacks.store != nullptr &&
      data != nullptr && size != 0 &&
      binding.callbacks.store(binding.context.get(), data, size);
  if (!stored) {
    return false;
  }

  std::string accepted(data, size);
  {
    std::lock_guard<std::mutex> lock(roblox_credential_provider_mutex_);
    ClearCookieString(&roblox_credential_override_);
    roblox_credential_override_ = std::move(accepted);
  }
  ClearCookieString(&accepted);
  return true;
}

}  // namespace jnivm
