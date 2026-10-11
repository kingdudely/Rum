#ifndef ROBLOX_STATUS_H_
#define ROBLOX_STATUS_H_

#include <string>
#include <utility>

namespace roblox {

enum class StatusCode {
  kOk = 0,
  kInvalidArgument,
  kFailedPrecondition,
  kUnavailable,
  kUnsupported,
  kPlatformError,
};

class Status final {
 public:
  Status() = default;

  static Status Ok() { return Status(); }

  static Status Error(StatusCode code, std::string message) {
    return Status(code, std::move(message));
  }

  bool ok() const { return code_ == StatusCode::kOk; }
  explicit operator bool() const { return ok(); }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

 private:
  Status(StatusCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  StatusCode code_ = StatusCode::kOk;
  std::string message_;
};

}  // namespace roblox

#endif  // ROBLOX_STATUS_H_
