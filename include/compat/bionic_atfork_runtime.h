#ifndef ROBLOX_COMPAT_BIONIC_ATFORK_RUNTIME_H_
#define ROBLOX_COMPAT_BIONIC_ATFORK_RUNTIME_H_

namespace roblox::compat {

using BionicAtForkCallback = void (*)(void);

// POSIX cannot unregister by dso_handle, so registered guest callbacks must
// remain mapped for every later fork.
int RegisterBionicAtFork(BionicAtForkCallback prepare,
                         BionicAtForkCallback parent,
                         BionicAtForkCallback child, void* dso_handle) noexcept;

}  // namespace roblox::compat

extern "C" {

int roblox_bionic_register_atfork(
    roblox::compat::BionicAtForkCallback prepare,
    roblox::compat::BionicAtForkCallback parent,
    roblox::compat::BionicAtForkCallback child, void* dso_handle);

}  // extern "C"

#endif  // ROBLOX_COMPAT_BIONIC_ATFORK_RUNTIME_H_
