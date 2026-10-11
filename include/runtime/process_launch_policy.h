#ifndef ROBLOX_RUNTIME_PROCESS_LAUNCH_POLICY_H_
#define ROBLOX_RUNTIME_PROCESS_LAUNCH_POLICY_H_

#include <string>

namespace roblox::runtime {

// Call only for interactive launches, before creating any threads. Removes an
// inherited soft CPU limit when the hard limit is unlimited and demotes the
// calling thread from FIFO/RR scheduling. Returns diagnostics for the session
// log, including failures; never changes hard limits or signal dispositions.
std::string ApplyInteractiveProcessLaunchPolicy();

}  // namespace roblox::runtime

#endif  // ROBLOX_RUNTIME_PROCESS_LAUNCH_POLICY_H_
