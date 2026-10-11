#ifndef ROBLOX_RUNTIME_FRAME_RATE_POLICY_H_
#define ROBLOX_RUNTIME_FRAME_RATE_POLICY_H_

#include <string>
#include <string_view>

namespace roblox {
namespace runtime {

inline constexpr int kMaximumSupportedRobloxSchedulerFps = 240;

enum class FrameRateLimitMode {
  kUnmanaged,
  kDisplay,
  kFixed,
  kUnlimited,
  kInvalid,
};

struct FrameRatePolicy {
  FrameRateLimitMode mode = FrameRateLimitMode::kUnmanaged;
  int fixed_fps = 0;

  bool valid() const { return mode != FrameRateLimitMode::kInvalid; }
};

FrameRatePolicy ParseFrameRatePolicy(std::string_view value);

// Enables Roblox's Basic Settings frame-rate control and, when explicitly
// requested, merges a Roblox scheduler policy into client settings.
// Unmanaged (-1) does not add a scheduler target, so Roblox owns the cap.
// Every positive fixed value is forwarded verbatim to
// DFIntTaskSchedulerTargetFps; it is not restricted to common refresh rates.
// Unlimited selects the unmodified payload's maximum scheduler target (240).
// With VSync auto/off, graphics policy separately requests an unthrottled
// Vulkan present mode when the host exposes one.
// Existing unrelated overrides are retained. A conflicting explicit override
// is rejected instead of silently choosing one source of truth.
bool MergeFrameRateClientSettingsOverrides(const FrameRatePolicy &policy,
                                           std::string_view base_json,
                                           std::string *merged_json,
                                           std::string *error);

} // namespace runtime
} // namespace roblox

#endif // ROBLOX_RUNTIME_FRAME_RATE_POLICY_H_
