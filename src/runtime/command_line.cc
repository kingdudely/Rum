#include "runtime/command_line.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

#include "compat/guest_abi.h"
#include "mocktail/platform/posix_primitives.h"
#include "runtime/roblox_launch_uri.h"

namespace mocktail {
namespace runtime {
namespace {

bool ReadOptionValue(int argc, const char* const argv[], int* index,
                     const std::string& option, std::string* value,
                     std::string* error) {
  if (*index + 1 >= argc || argv[*index + 1] == nullptr ||
      argv[*index + 1][0] == '\0' ||
      std::string(argv[*index + 1]).rfind("--", 0) == 0) {
    *error = "missing value for " + option;
    return false;
  }
  *value = argv[++(*index)];
  return true;
}

bool SetEnvironment(const char* name, const std::string& value,
                    std::string* error) {
  if (setenv(name, value.c_str(), 1) == 0) {
    return true;
  }
  if (error != nullptr) {
    *error = std::string("could not apply command-line option: ") + name;
  }
  return false;
}

using platform::SecureErase;

bool HaveLaunch(const CommandLineOptions& options) {
  return options.place_id != 0 || !options.raw_launch_argument.empty() ||
         !options.engine_launch_uri.empty();
}

// Accepts a bare cookie value or a ".ROBLOSECURITY=<value>" header the way
// browsers show it. Anything else is rejected so a mistyped flag fails here
// instead of surfacing as an anonymous session later.
std::string NormalizeCookieValue(std::string value) {
  const auto is_space = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!value.empty() && is_space(value.front())) value.erase(value.begin());
  while (!value.empty() && is_space(value.back())) value.pop_back();
  constexpr std::string_view kPrefix = ".ROBLOSECURITY=";
  if (value.compare(0, kPrefix.size(), kPrefix) == 0) {
    value.erase(0, kPrefix.size());
  }
  if (value.empty() || value.find_first_of(";\t\r\n ") != std::string::npos) {
    return {};
  }
  return value;
}

}  // namespace

namespace {

// The engine decides for itself what a roblox:// argument means; Mocktail only
// needs to know whether the argument is addressed to Roblox at all. Anything
// Roblox-addressed that is not a place or user launch is a web-login ticket,
// which the engine's WebLoginProtocol redeems during native settings startup.
bool IsRobloxAddressedLaunchUri(std::string_view uri) {
  const std::size_t colon = uri.find(':');
  if (colon == std::string::npos || colon == 0) {
    return false;
  }
  std::string scheme(uri.substr(0, colon));
  for (char& c : scheme) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return scheme == "roblox" || scheme == "roblox-player";
}

}  // namespace

CommandLineParseResult ParseCommandLine(int argc, const char* const argv[]) {
  CommandLineParseResult result;
  if (argc > 0 && argv != nullptr && argv[0] != nullptr && argv[0][0] != '\0') {
    result.options.program_name = argv[0];
  }
  for (int index = 1; index < argc; ++index) {
    if (argv == nullptr || argv[index] == nullptr) {
      result.error = "invalid null command-line argument";
      return result;
    }
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h") {
      result.options.mode = CommandMode::kHelp;
      return result;
    }
    if (argument == "--roblox-lib") {
      if (!ReadOptionValue(argc, argv, &index, argument,
                           &result.options.roblox_library_path,
                           &result.error)) {
        return result;
      }
    } else if (argument == "--headless") {
      result.options.window_mode = WindowMode::kHeadless;
    } else if (argument == "--windowed") {
      result.options.window_mode = WindowMode::kWindowed;
    } else if (argument == "--graphics") {
      if (!ReadOptionValue(argc, argv, &index, argument,
                           &result.options.graphics_backend, &result.error)) {
        return result;
      }
    } else if (argument == "--assets") {
      if (!ReadOptionValue(argc, argv, &index, argument,
                           &result.options.assets_path,
                           &result.error)) {
        return result;
      }
    } else if (argument == "--place-id") {
      if (HaveLaunch(result.options)) {
        result.error = "duplicate option: --place-id";
        return result;
      }
      std::string value;
      if (!ReadOptionValue(argc, argv, &index, argument, &value,
                           &result.error)) {
        return result;
      }
      errno = 0;
      char* end = nullptr;
      const long long place_id =
          std::strtoll(value.c_str(), &end, 10);
      if (errno == ERANGE || end == value.c_str() || *end != '\0' ||
          place_id <= 0) {
        result.error = "invalid --place-id: expected a positive place ID";
        return result;
      }
      result.options.place_id = static_cast<int64_t>(place_id);
    } else if (argument == "--roblosecurity") {
      if (!result.options.roblosecurity.empty()) {
        result.error = "duplicate option: --roblosecurity";
        return result;
      }
      std::string value;
      if (!ReadOptionValue(argc, argv, &index, argument, &value,
                           &result.error)) {
        return result;
      }
      result.options.roblosecurity = NormalizeCookieValue(value);
      if (result.options.roblosecurity.empty()) {
        result.error = "invalid --roblosecurity: expected a cookie value";
        return result;
      }
      result.options.roblosecurity_argument_index = index;
    } else if (argument == "--launch-uri") {
      if (HaveLaunch(result.options)) {
        result.error = "duplicate option: --launch-uri";
        return result;
      }
      if (!ReadOptionValue(argc, argv, &index, argument,
                           &result.options.raw_launch_argument,
                           &result.error)) {
        return result;
      }
      RobloxExperienceLaunchRequest launch_request;
      const Status launch_status = ParseRobloxLaunchUri(
          result.options.raw_launch_argument, &launch_request);
      if (!launch_status.ok()) {
        if (IsRobloxAddressedLaunchUri(result.options.raw_launch_argument)) {
          result.options.engine_launch_uri =
              std::move(result.options.raw_launch_argument);
          result.options.launch_argument_index = index;
          ++index;
          continue;
        }
        result.options.raw_launch_argument.clear();
        result.error = "invalid Roblox launch URI: " + launch_status.message();
        return result;
      }
      result.options.launch_argument_index = index;
    } else if (!argument.empty() && argument.front() != '-' &&
               !HaveLaunch(result.options)) {
      RobloxExperienceLaunchRequest launch_request;
      const Status launch_status =
          ParseRobloxLaunchUri(argument, &launch_request);
      if (!launch_status.ok()) {
        if (IsRobloxAddressedLaunchUri(argument)) {
          result.options.engine_launch_uri = argument;
          result.options.launch_argument_index = index;
          ++index;
          continue;
        }
        result.error = "unknown argument";
        return result;
      }
      result.options.raw_launch_argument = argument;
      result.options.launch_argument_index = index;
    } else {
      result.error = !argument.empty() && argument.front() != '-'
                         ? "unknown argument"
                         : "unknown option: " + argument;
      return result;
    }
  }
  return result;
}

void ScrubEngineLaunchUri(CommandLineOptions* options) {
  if (options == nullptr || options->engine_launch_uri.empty()) return;
  SecureErase(options->engine_launch_uri.data(),
              options->engine_launch_uri.size());
  options->engine_launch_uri.clear();
}

void ScrubCommandLineLaunchArguments(CommandLineOptions* options, int argc,
                                     char* argv[]) {
  if (options == nullptr) return;
  // A web-login ticket moves out of raw_launch_argument into
  // engine_launch_uri at parse time, but its argv slot is the same one.
  const bool is_ticket = options->raw_launch_argument.empty() &&
                         !options->engine_launch_uri.empty();
  const std::string& launched =
      is_ticket ? options->engine_launch_uri : options->raw_launch_argument;
  if (!launched.empty() && options->launch_argument_index > 0 &&
      options->launch_argument_index < argc && argv != nullptr &&
      argv[options->launch_argument_index] != nullptr) {
    SecureErase(argv[options->launch_argument_index], launched.size());
  }
  if (!options->roblosecurity.empty() &&
      options->roblosecurity_argument_index > 0 &&
      options->roblosecurity_argument_index < argc && argv != nullptr &&
      argv[options->roblosecurity_argument_index] != nullptr) {
    SecureErase(argv[options->roblosecurity_argument_index],
                options->roblosecurity.size());
  }
  SecureErase(options->raw_launch_argument.data(),
              options->raw_launch_argument.size());
  options->raw_launch_argument.clear();
  options->launch_argument_index = -1;
  SecureErase(options->roblosecurity.data(), options->roblosecurity.size());
  options->roblosecurity.clear();
  options->roblosecurity_argument_index = -1;
}

bool ApplyCommandLineEnvironment(const CommandLineOptions& options,
                                 std::string* error) {
  if (!options.roblox_library_path.empty() &&
      !SetEnvironment("ROBLOX_LIB_PATH", options.roblox_library_path, error)) {
    return false;
  }
  if (!options.graphics_backend.empty() &&
      !SetEnvironment("MOCKTAIL_GRAPHICS_BACKEND", options.graphics_backend,
                      error)) {
    return false;
  }
  if (!options.roblosecurity.empty() &&
      !SetEnvironment("MOCKTAIL_ROBLOSECURITY", options.roblosecurity,
                      error)) {
    return false;
  }
  if (options.window_mode == WindowMode::kHeadless) {
    return SetEnvironment("MOCKTAIL_HEADLESS", "1", error);
  }
  if (options.window_mode == WindowMode::kWindowed) {
    return SetEnvironment("MOCKTAIL_HEADLESS", "0", error);
  }
  return true;
}

std::string CommandLineUsage(const std::string& program_name) {
  std::ostringstream usage;
  usage
      << "Usage: " << (program_name.empty() ? "mocktail" : program_name)
      << " [options] [roblox-uri]\n\n"
      << "Runs a locally provided Roblox client; it does not download Roblox\n"
      << "files. Defaults to libroblox.so and assets/content next to this\n"
      << "executable.\n\n"
      << "Options:\n"
      << "  --roblox-lib <path>    Path to libroblox.so\n"
      << "                           (default: <exe dir>/libroblox.so)\n"
      << "  --assets <path>        Roblox assets directory; either <assets> or\n"
      << "                           <assets/content> is accepted\n"
      << "                           (default: <exe dir>/assets/content)\n"
      << "  --headless             Run without creating an SDL window\n"
      << "  --windowed             Force windowed startup (default)\n"
      << "  --graphics <backend>   direct-vulkan | opengl | system | "
         "angle-vulkan (default: direct-vulkan)\n"
      << "  --launch-uri <uri>     A roblox:// link: placeId=... to join.\n"
      << "  --place-id <id>        Join a place without a roblox:// link.\n"
      << "  --roblosecurity <v>    Sign in with a .ROBLOSECURITY value.\n"
      << "                           Never written to disk; erased from the\n"
      << "                           command line once startup consumes it.\n"
      << "  --help, -h             Show this help\n\n"
      << "Auth:\n"
      << "  Pass --roblosecurity (or set MOCKTAIL_ROBLOSECURITY) to play as\n"
      << "  yourself. Without it, Roblox starts as a guest.\n\n"
      << "Additional runtime options are available as MOCKTAIL_* environment variables.\n";
  return usage.str();
}

}  // namespace runtime
}  // namespace mocktail
