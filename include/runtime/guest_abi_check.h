#ifndef MOCKTAIL_RUNTIME_GUEST_ABI_CHECK_H_
#define MOCKTAIL_RUNTIME_GUEST_ABI_CHECK_H_

#include <filesystem>
#include <string>
#include <string_view>

namespace mocktail {
namespace runtime {

enum class GuestAbiVerdict {
  kOk,
  kUnreadable,
  kNotElf,
  kWrongElfClass,
  kWrongMachine,
};

struct GuestAbiReport {
  GuestAbiVerdict verdict = GuestAbiVerdict::kUnreadable;
  // Human-readable and actionable, safe to print as-is. Empty when kOk.
  std::string message;
};

// Mocktail resolves the guest ABI when it is compiled (see compat/guest_abi.h),
// so a library built for another architecture cannot run here. Reading the ELF
// header up front turns that into one clear line instead of a failure deep
// inside the dynamic loader.
GuestAbiReport InspectGuestLibraryAbi(const std::filesystem::path& library);

// The Android ABI name for an ELF e_machine value, e.g. "x86_64" or
// "arm64-v8a". Returns "unknown" for machines we have no name for.
std::string_view ElfMachineAbiName(unsigned machine);

}  // namespace runtime
}  // namespace mocktail

#endif  // MOCKTAIL_RUNTIME_GUEST_ABI_CHECK_H_