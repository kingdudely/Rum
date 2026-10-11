#include "compat/bionic_atfork_runtime.h"

#include <pthread.h>

namespace roblox::compat {

int RegisterBionicAtFork(BionicAtForkCallback prepare,
                         BionicAtForkCallback parent,
                         BionicAtForkCallback child,
                         void* dso_handle) noexcept {
  // pthread_atfork preserves the Android/POSIX callback order and is provided
  // by both glibc and musl. Its public contract intentionally has no DSO
  // lifetime hook; see the constraint documented in the public header.
  static_cast<void>(dso_handle);
  return pthread_atfork(prepare, parent, child);
}

}  // namespace roblox::compat

extern "C" int
roblox_bionic_register_atfork(roblox::compat::BionicAtForkCallback prepare,
                                roblox::compat::BionicAtForkCallback parent,
                                roblox::compat::BionicAtForkCallback child,
                                void* dso_handle) {
  return roblox::compat::RegisterBionicAtFork(prepare, parent, child,
                                                dso_handle);
}
