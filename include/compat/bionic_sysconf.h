#ifndef ROBLOX_COMPAT_BIONIC_SYSCONF_H_
#define ROBLOX_COMPAT_BIONIC_SYSCONF_H_

namespace roblox::compat {

// Android Bionic assigns different integer values to sysconf names than
// glibc. These are the names observed in the active Roblox Build-ID profile
// and are kept explicit so a raw Bionic value can never reach host sysconf.
enum class BionicSysconfName : int {
  kArgMax = 0x0000,
  kChildMax = 0x0005,
  kClockTicks = 0x0006,
  kOpenMax = 0x000b,
  kIovMax = 0x0026,
  kPageSize = 0x0027,
  kPageSizeAlias = 0x0028,
  kProcessorsConfigured = 0x0060,
  kProcessorsOnline = 0x0061,
  kPhysicalPages = 0x0062,
  kAvailablePhysicalPages = 0x0063,
};

// Translates selected Bionic sysconf integer names to their host glibc
// equivalent. Unsupported names fail exactly as Bionic does: -1 and EINVAL.
long BionicSysconf(int name) noexcept;

}  // namespace roblox::compat

extern "C" long roblox_bionic_sysconf(int name);

#endif  // ROBLOX_COMPAT_BIONIC_SYSCONF_H_
