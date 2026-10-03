#include "compat/payload_compatibility.h"

#include <utility>

#include "compat/elf_build_id.h"
#include "compat/host_abi_profile.h"
#include "compat/host_abi_profile_loader.h"

namespace mocktail {
namespace compat {

PayloadCompatibilityResult CheckPayloadCompatibility(
    const std::string& library_path, const std::string& manifest_path,
    bool allow_unverified_build) {
  PayloadCompatibilityResult result;
  const BuildIdResult build_id = ReadElfBuildId(library_path);
  if (!build_id) {
    result.error = "Cannot identify Roblox library '" + library_path +
                   "': " + build_id.error;
    return result;
  }
  result.build_id = build_id.build_id;

  const ExternalHostAbiProfileResult external_host_abi =
      InitializeHostAbiProfileFromEnvironment(
          library_path, manifest_path, result.build_id, allow_unverified_build);
  if (!external_host_abi) {
    result.error =
        "Cannot load exact Roblox host ABI profile: " + external_host_abi.error;
    return result;
  }

  const ProfileLookupResult profile =
      FindBuildProfile(manifest_path, result.build_id);
  if (!profile) {
    result.error = "Cannot read Roblox compatibility profile: " + profile.error;
    return result;
  }
  if (!profile.profile.has_value()) {
    // An unresearched Build ID is not a reason to refuse to start. Nothing in
    // the synthesized profile points into the guest binary, so the run simply
    // has no allocator interposition, constructor replay, or vtable bridges.
    result.profile = MakeUnknownBuildProfile(result.build_id);
    result.used_unknown_build_profile = true;
  } else {
    result.profile = *profile.profile;
  }
  if (!result.profile.default_allowed && !allow_unverified_build) {
    result.error = "This Roblox build is not enabled for normal runs: " +
                   result.profile.reason +
                   "\n  Use --allow-unverified-build only for an explicit "
                   "compatibility run.";
    result.profile = {};
    return result;
  }
  // A host ABI profile is what makes allocator interposition and constructor
  // replay build-scoped. Without one both stay denied, which the engine runs
  // fine with: its own allocator and constructors do the work.
  if (FindHostAbiProfile(result.build_id) == nullptr) {
    result.missing_host_abi_profile = true;
  }
  return result;
}

}  // namespace compat
}  // namespace mocktail
