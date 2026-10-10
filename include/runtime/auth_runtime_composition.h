#ifndef MOCKTAIL_RUNTIME_AUTH_RUNTIME_COMPOSITION_H_
#define MOCKTAIL_RUNTIME_AUTH_RUNTIME_COMPOSITION_H_

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "jnivm/jnivm.h"

namespace mocktail {
namespace services {
class AuthService;
class HttpClient;
}  // namespace services

namespace runtime {

class Environment;
class RuntimePaths;

enum class AuthRuntimeStatus {
  kAuthenticated,
  kGuest,
  kInvalidCredentials,
  kUnavailable,
};

// Move-only credential storage that clears its allocation. Never log its view.
class SecureRobloxCredential final {
 public:
  SecureRobloxCredential() = default;
  explicit SecureRobloxCredential(std::string canonical_header);
  ~SecureRobloxCredential();

  SecureRobloxCredential(const SecureRobloxCredential&) = delete;
  SecureRobloxCredential& operator=(const SecureRobloxCredential&) = delete;
  SecureRobloxCredential(SecureRobloxCredential&& other) noexcept;
  SecureRobloxCredential& operator=(SecureRobloxCredential&& other) noexcept;

  bool empty() const { return bytes_.empty(); }
  size_t size() const { return bytes_.empty() ? 0 : bytes_.size() - 1; }
  const char* c_str() const { return bytes_.empty() ? "" : bytes_.data(); }
  std::string_view view() const { return {c_str(), size()}; }
  void Clear();

 private:
  std::vector<char> bytes_;
};

// Must be created after the credential reaches its final address and destroyed
// before the credential or VM. Guest bindings block legacy env/disk fallback.
class ScopedRobloxCredentialBinding final {
 public:
  ScopedRobloxCredentialBinding(jnivm::VM* jni_vm,
                                const SecureRobloxCredential& credential);
  ~ScopedRobloxCredentialBinding();

  ScopedRobloxCredentialBinding(const ScopedRobloxCredentialBinding&) = delete;
  ScopedRobloxCredentialBinding& operator=(
      const ScopedRobloxCredentialBinding&) = delete;
  ScopedRobloxCredentialBinding(ScopedRobloxCredentialBinding&&) = delete;
  ScopedRobloxCredentialBinding& operator=(
      ScopedRobloxCredentialBinding&&) = delete;

  bool bound() const { return jni_vm_ != nullptr; }

 private:
  static jnivm::RobloxCredentialView ProvideCredential(const void* context);

  jnivm::VM* jni_vm_ = nullptr;
};

// Retains the validated credential without reopening its source.
struct AuthRuntimeComposition {
  AuthRuntimeStatus status = AuthRuntimeStatus::kUnavailable;
  std::shared_ptr<jnivm::VM> jni_vm;
  jnivm::RobloxAuthIdentity account_identity;
  SecureRobloxCredential credential;
  long http_status = 0;
  std::string error;

  explicit operator bool() const { return jni_vm != nullptr; }
};

// A VM requires authentication or explicit guest mode. The credential comes
// only from MOCKTAIL_ROBLOSECURITY and is never written to disk.
AuthRuntimeComposition ComposeAuthRuntime(const Environment& environment,
                                          services::AuthService& auth_service);

// Retains HTTP transport for native sign-in and validates the credential.
AuthRuntimeComposition ComposeAuthRuntime(
    const Environment& environment, services::AuthService& auth_service,
    std::shared_ptr<services::HttpClient> live_auth_http_client);

}  // namespace runtime
}  // namespace mocktail

#endif  // MOCKTAIL_RUNTIME_AUTH_RUNTIME_COMPOSITION_H_
