#ifndef MOCKTAIL_COMPAT_PAYLOAD_COMPATIBILITY_H_
#define MOCKTAIL_COMPAT_PAYLOAD_COMPATIBILITY_H_

#include <string>

#include "compat/build_profile.h"

namespace mocktail {
namespace compat {

// Result of the side-effect-free compatibility gate used before any network,
// authentication, or native-loader work. The legacy runtime repeats the
// profile application while that ownership is migrated, but external startup
// services must not run until this gate succeeds.
struct PayloadCompatibilityResult {
  BuildProfile profile;
  std::string build_id;
  std::string error;
  // Set when the build had no researched profile. Every Build-ID-scoped
  // capability is denied, so this is a degraded run rather than a validated
  // one.
  bool used_unknown_build_profile = false;
  // Set when no Build-ID-scoped host ABI profile is loaded. Allocator
  // interposition and constructor replay are denied for this run.
  bool missing_host_abi_profile = false;

  explicit operator bool() const noexcept { return error.empty(); }
};

// `allow_unverified_build` covers a build that has a profile but is not
// enabled by default. A build with no profile at all is reported through
// `used_unknown_build_profile` and still runs.
PayloadCompatibilityResult CheckPayloadCompatibility(
    const std::string& library_path, const std::string& manifest_path,
    bool allow_unverified_build);

}  // namespace compat
}  // namespace mocktail

#endif  // MOCKTAIL_COMPAT_PAYLOAD_COMPATIBILITY_H_
