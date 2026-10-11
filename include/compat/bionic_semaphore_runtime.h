#ifndef ROBLOX_COMPAT_BIONIC_SEMAPHORE_RUNTIME_H_
#define ROBLOX_COMPAT_BIONIC_SEMAPHORE_RUNTIME_H_

#include <semaphore.h>

// Bionic sem_t is a count word in guest storage (value << 1, shared bit 0).
// Wait/post use __atomic_* and futex on that word so inlined guest futex
// waiters wake. Process-shared semaphores are rejected.
extern "C" {

int roblox_bionic_sem_init(sem_t* semaphore, int process_shared,
                             unsigned int value);
int roblox_bionic_sem_destroy(sem_t* semaphore);
int roblox_bionic_sem_wait(sem_t* semaphore);
int roblox_bionic_sem_post(sem_t* semaphore);

}  // extern "C"

#endif  // ROBLOX_COMPAT_BIONIC_SEMAPHORE_RUNTIME_H_
