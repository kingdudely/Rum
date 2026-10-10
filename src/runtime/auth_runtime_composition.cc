#include "runtime/auth_runtime_composition.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "jnivm/jnivm.h"
#include "mocktail/platform/posix_primitives.h"
#include "runtime/environment.h"
#include "services/auth_service.h"

namespace mocktail {
namespace runtime {
namespace {

enum class CookieLoadStatus {
  kFound,
  kMissing,
  kUnavailable,
};

struct CookieLoadResult {
  CookieLoadStatus status = CookieLoadStatus::kMissing;
  std::string value;
  std::string error;
};

using platform::SecureClear;

bool Enabled(const Environment& environment, const char* name,
             bool default_value) {
  const std::optional<std::string> value = environment.Get(name);
  if (!value.has_value() || value->empty()) {
    return default_value;
  }
  return *value != "0";
}

// The credential lives only in memory: MOCKTAIL_ROBLOSECURITY (or the
// --roblosecurity flag that sets it). Nothing is read from or written to
// disk, so a relaunch with a different value is a different identity.
CookieLoadResult LoadSavedCookie(const Environment& environment) {
  CookieLoadResult result;
  std::optional<std::string> environment_cookie =
      environment.Get("MOCKTAIL_ROBLOSECURITY");
  if (environment_cookie.has_value() && !environment_cookie->empty()) {
    result.status = CookieLoadStatus::kFound;
    result.value = std::move(*environment_cookie);
  }
  return result;
}

}  // namespace

SecureRobloxCredential::SecureRobloxCredential(std::string canonical_header) {
  if (!canonical_header.empty()) {
    bytes_.assign(canonical_header.begin(), canonical_header.end());
    bytes_.push_back('\0');
  }
  SecureClear(&canonical_header);
}

SecureRobloxCredential::~SecureRobloxCredential() { Clear(); }

SecureRobloxCredential::SecureRobloxCredential(
    SecureRobloxCredential&& other) noexcept
    : bytes_(std::move(other.bytes_)) {}

SecureRobloxCredential& SecureRobloxCredential::operator=(
    SecureRobloxCredential&& other) noexcept {
  if (this != &other) {
    Clear();
    bytes_ = std::move(other.bytes_);
  }
  return *this;
}

void SecureRobloxCredential::Clear() {
  volatile char* byte = bytes_.empty() ? nullptr : bytes_.data();
  for (size_t index = 0; index < bytes_.size(); ++index) {
    byte[index] = '\0';
  }
  bytes_.clear();
}

ScopedRobloxCredentialBinding::ScopedRobloxCredentialBinding(
    jnivm::VM* jni_vm, const SecureRobloxCredential& credential)
    : jni_vm_(jni_vm) {
  if (jni_vm_ != nullptr) {
    jni_vm_->SetRobloxCredentialProvider(&credential, &ProvideCredential);
  }
}

ScopedRobloxCredentialBinding::~ScopedRobloxCredentialBinding() {
  if (jni_vm_ != nullptr) {
    jni_vm_->ClearRobloxCredentialProvider();
  }
}

jnivm::RobloxCredentialView
ScopedRobloxCredentialBinding::ProvideCredential(const void* context) {
  const auto* credential =
      static_cast<const SecureRobloxCredential*>(context);
  return credential != nullptr
             ? jnivm::RobloxCredentialView{credential->c_str(),
                                            credential->size()}
             : jnivm::RobloxCredentialView{};
}

AuthRuntimeComposition ComposeAuthRuntime(const Environment& environment,
                                          services::AuthService& auth_service) {
  AuthRuntimeComposition composition;
  CookieLoadResult cookie = LoadSavedCookie(environment);
  if (cookie.status == CookieLoadStatus::kUnavailable) {
    composition.error = std::move(cookie.error);
    return composition;
  }

  SecureRobloxCredential credential;
  if (cookie.status == CookieLoadStatus::kFound) {
    std::string cookie_value =
        services::AuthService::ExtractRoblosecurityValue(cookie.value);
    if (!cookie_value.empty()) {
      std::string canonical_header = ".ROBLOSECURITY=";
      canonical_header += cookie_value;
      credential = SecureRobloxCredential(std::move(canonical_header));
    }
    SecureClear(&cookie_value);
  }
  const bool allow_guest =
      Enabled(environment, "MOCKTAIL_ALLOW_NO_COOKIE_LUA_APP", true);
  const services::AuthSession session = auth_service.ResolveSession(
      credential.empty() ? std::string_view(cookie.value) : credential.view(),
      allow_guest);

  SecureClear(&cookie.value);

  composition.http_status = session.http_status;
  composition.error = session.error;
  switch (session.status) {
    case services::AuthSessionStatus::kAuthenticated: {
      auto jni_vm = std::make_shared<jnivm::VM>();
      jnivm::RobloxAuthIdentity identity;
      identity.user_id = session.identity.user_id;
      identity.username = session.identity.username;
      identity.display_name = session.identity.display_name;
      jni_vm->SetRobloxAuthIdentity(identity);
      if (!credential.empty()) {
        (void)jni_vm->DispatchRobloxCredential(credential.c_str(),
                                               credential.size());
      }
      composition.status = AuthRuntimeStatus::kAuthenticated;
      composition.jni_vm = std::move(jni_vm);
      composition.account_identity = std::move(identity);
      composition.credential = std::move(credential);
      break;
    }
    case services::AuthSessionStatus::kGuest:
      composition.status = AuthRuntimeStatus::kGuest;
      composition.jni_vm = std::make_shared<jnivm::VM>();
      break;
    case services::AuthSessionStatus::kInvalid:
      composition.status = AuthRuntimeStatus::kInvalidCredentials;
      break;
    case services::AuthSessionStatus::kUnavailable:
      composition.status = AuthRuntimeStatus::kUnavailable;
      break;
  }
  return composition;
}

}  // namespace runtime
}  // namespace mocktail
