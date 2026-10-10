#ifndef MOCKTAIL_COMPAT_GUEST_ABI_H_
#define MOCKTAIL_COMPAT_GUEST_ABI_H_

#include <elf.h>
#include <string_view>

namespace mocktail::compat {

#if defined(__aarch64__)
// Android ABI name of the guest libraries this build runs.
inline constexpr std::string_view kGuestAbi = "arm64-v8a";
// Short CPU label for device metadata and user-facing strings.
inline constexpr std::string_view kGuestCpuName = "aarch64";
inline constexpr int kGuestElfMachine = EM_AARCH64;
#elif defined(__x86_64__)
inline constexpr std::string_view kGuestAbi = "x86_64";
inline constexpr std::string_view kGuestCpuName = "x86_64";
inline constexpr int kGuestElfMachine = EM_X86_64;
#else
#error "mocktail only supports aarch64 and x86_64 hosts"
#endif

}  // namespace mocktail::compat

#endif  // MOCKTAIL_COMPAT_GUEST_ABI_H_
