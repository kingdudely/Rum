#ifndef ROBLOX_RUNTIME_CRASH_REPORT_POLICY_H_
#define ROBLOX_RUNTIME_CRASH_REPORT_POLICY_H_

#include <string>
#include <string_view>

namespace roblox {
namespace runtime {

// Disables Roblox crash uploads while preserving unrelated overrides.
bool MergeCrashReportClientSettingsOverrides(std::string_view base_json,
                                             std::string* merged_json,
                                             std::string* error);

// AppBridge accepts only the FastVariable subset of this policy.
bool MergeCrashReportFastFlagsOverrides(std::string_view base_json,
                                        std::string* merged_json,
                                        std::string* error);

}  // namespace runtime
}  // namespace roblox

#endif  // ROBLOX_RUNTIME_CRASH_REPORT_POLICY_H_
