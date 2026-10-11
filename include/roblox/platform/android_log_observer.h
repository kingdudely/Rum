#ifndef ROBLOX_PLATFORM_ANDROID_LOG_OBSERVER_H_
#define ROBLOX_PLATFORM_ANDROID_LOG_OBSERVER_H_

// Observes the fully formatted payload written through Roblox's liblog
// adapter. The callback must not retain tag or message; both are borrowed for
// the duration of the call. Passing nullptr removes the current observer.
using RobloxAndroidLogObserver = void (*)(int priority, const char* tag,
                                            const char* message);

extern "C" void roblox_android_log_set_observer(
    RobloxAndroidLogObserver observer);

#endif  // ROBLOX_PLATFORM_ANDROID_LOG_OBSERVER_H_
