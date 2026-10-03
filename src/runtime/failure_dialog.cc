#include "runtime/failure_dialog.h"

#include <cerrno>
#include <cstddef>
#include <optional>
#include <cstdio>
#include <string>
#include <string_view>

namespace mocktail {
namespace runtime {
namespace {

bool Enabled(const Environment& environment, std::string_view name) {
  const std::optional<std::string> value = environment.Get(name);
  return value.has_value() && !value->empty() && *value != "0";
}

bool HasNonEmpty(const Environment& environment, std::string_view name) {
  const std::optional<std::string> value = environment.Get(name);
  return value.has_value() && !value->empty();
}

}  // namespace

bool FailureDialogsEnabled(const Environment& environment) {
  if (HasNonEmpty(environment, "MOCKTAIL_FAILURE_DIALOGS")) {
    return Enabled(environment, "MOCKTAIL_FAILURE_DIALOGS");
  }
  return true;
}

bool ShowFailureDialog(const Environment& environment,
                       std::string_view message) {
  if (!FailureDialogsEnabled(environment)) {
    return false;
  }
  std::fprintf(stderr, "[failure] %.*s\n", static_cast<int>(message.size()),
               message.data());
  return true;
}

bool ShowWarningDialog(const Environment& environment,
                       std::string_view message) {
  if (!FailureDialogsEnabled(environment)) {
    return false;
  }
  std::fprintf(stderr, "[warning] %.*s\n", static_cast<int>(message.size()),
               message.data());
  return true;
}

FailureDialogMonitor::~FailureDialogMonitor() {
  if (socket_ >= 0) {
    Finish('F');
  }
}

FailureDialogMonitor::FailureDialogMonitor(FailureDialogMonitor&& other) noexcept
    : socket_(other.socket_), helper_pid_(other.helper_pid_) {
  other.socket_ = -1;
  other.helper_pid_ = -1;
}

FailureDialogMonitor& FailureDialogMonitor::operator=(
    FailureDialogMonitor&& other) noexcept {
  if (this != &other) {
    if (socket_ >= 0) {
      Finish('F');
    }
    socket_ = other.socket_;
    helper_pid_ = other.helper_pid_;
    other.socket_ = -1;
    other.helper_pid_ = -1;
  }
  return *this;
}

FailureDialogMonitor FailureDialogMonitor::Start(
    const Environment& environment, std::string_view initial_message) {
  // There is no dialog helper anymore, so there is nothing to arm and nothing
  // to announce. The initial_message was the text the helper displayed on
  // failure; printing it here would report a failure that has not happened yet.
  // Real failures are reported by their own call sites.
  (void)environment;
  (void)initial_message;
  return FailureDialogMonitor{};
}

void FailureDialogMonitor::SetMessage(std::string_view message) {
  std::fprintf(stderr, "[status] %.*s\n", static_cast<int>(message.size()),
               message.data());
}

void FailureDialogMonitor::MarkSuccessful() {
  if (socket_ >= 0) {
    Finish('S');
  }
}

void FailureDialogMonitor::Finish(char) {
  if (socket_ >= 0) {
    socket_ = -1;
  }
  helper_pid_ = -1;
}

}  // namespace runtime
}  // namespace mocktail
