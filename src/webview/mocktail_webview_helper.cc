// Minimal WebView helper: no embedded browser. URLs are handed to the
// system browser via xdg-open and the control channel is drained so the
// launcher protocol stays satisfied.
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <spawn.h>
#include <string>

#include "runtime/webview_helper_launcher.h"

extern char** environ;

namespace {

constexpr std::size_t kMaximumRequestBytes = 64 * 1024;

bool ReadRequest(std::string* request) {
  request->clear();
  char buffer[1024];
  while (true) {
    const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (count == 0) {
      return true;
    }
    if (request->size() + static_cast<std::size_t>(count) > kMaximumRequestBytes) {
      return false;
    }
    request->append(buffer, static_cast<std::size_t>(count));
  }
}

void OpenUrl(const std::string& url) {
  std::string program = "xdg-open";
  char* argv[] = {program.data(), const_cast<char*>(url.c_str()), nullptr};
  pid_t child = -1;
  if (posix_spawnp(&child, program.c_str(), nullptr, nullptr, argv, environ) == 0) {
    int status = 0;
    while (waitpid(child, &status, WNOHANG) == 0) {
      break;  // fire and forget; avoid blocking the control channel
    }
  }
}

bool SendReady() {
  std::string packet;
  if (!mocktail::runtime::EncodeWebViewHelperEventPacket(
          mocktail::runtime::WebViewHelperEventType::kReady, {}, &packet)) {
    return false;
  }
  ssize_t total = 0;
  while (total < static_cast<ssize_t>(packet.size())) {
    const ssize_t count = send(mocktail::runtime::kWebViewHelperControlDescriptor,
                               packet.data() + total, packet.size() - total, 0);
    if (count <= 0) {
      return false;
    }
    total += count;
  }
  return true;
}

}  // namespace

int main() {
  std::string request;
  std::string url;
  std::string error;
  if (!ReadRequest(&request) ||
      !mocktail::runtime::DecodeWebViewRequest(request, &url, &error)) {
    std::cerr << "invalid webview request"
              << (error.empty() ? std::string{} : ": " + error) << '\n';
    return EXIT_FAILURE;
  }
  if (!mocktail::runtime::ValidateWebViewUrl(url, &error)) {
    std::cerr << "invalid webview URL: " << error << '\n';
    return EXIT_FAILURE;
  }
  if (!SendReady()) {
    return EXIT_FAILURE;
  }
  OpenUrl(url);

  char buffer[mocktail::runtime::kMaximumWebViewControlPacketBytes];
  while (true) {
    const ssize_t count = recv(mocktail::runtime::kWebViewHelperControlDescriptor,
                               buffer, sizeof(buffer), 0);
    if (count <= 0) {
      return EXIT_SUCCESS;  // launcher closed the channel: we are done
    }
    mocktail::runtime::WebViewHelperControlCommand command;
    if (!mocktail::runtime::DecodeWebViewHelperControlPacket(
            std::string_view(buffer, static_cast<std::size_t>(count)), &command,
            &error)) {
      return EXIT_SUCCESS;
    }
    switch (command.operation) {
      case mocktail::runtime::WebViewHelperControlOperation::kLoadUrl:
        if (mocktail::runtime::ValidateWebViewUrl(command.payload, &error)) {
          OpenUrl(command.payload);
        }
        break;
      case mocktail::runtime::WebViewHelperControlOperation::kClose:
        return EXIT_SUCCESS;
      default:
        break;  // SetTitle/SetVisible/EvaluateJavaScript/etc. need no browser
    }
  }
}
