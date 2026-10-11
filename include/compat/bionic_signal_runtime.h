#ifndef ROBLOX_COMPAT_BIONIC_SIGNAL_RUNTIME_H_
#define ROBLOX_COMPAT_BIONIC_SIGNAL_RUNTIME_H_

#include <signal.h>

#include <cstdint>

namespace roblox::compat {

// Android LP64 exposes a POSIX sigaction layout that differs
// from both the Linux kernel and glibc layouts.
struct BionicSigaction {
  int flags = 0;
  int padding = 0;
  union {
    void (*handler)(int);
    void (*action)(int, siginfo_t*, void*);
  } callback{};
  std::uint64_t mask = 0;
  void (*restorer)() = nullptr;
};

static_assert(sizeof(BionicSigaction) == 32,
              "Android x86-64 sigaction ABI changed");

int BionicSigactionCall(int signal_number, const BionicSigaction* action,
                        BionicSigaction* old_action) noexcept;

}  // namespace roblox::compat

extern "C" int roblox_bionic_sigaction(
    int signal_number, const roblox::compat::BionicSigaction* action,
    roblox::compat::BionicSigaction* old_action);

#endif  // ROBLOX_COMPAT_BIONIC_SIGNAL_RUNTIME_H_
