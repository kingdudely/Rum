#ifndef MOCKTAIL_RUNTIME_COMMAND_LINE_H_
#define MOCKTAIL_RUNTIME_COMMAND_LINE_H_

#include <cstdint>
#include <string>
#include <vector>

namespace mocktail {
namespace runtime {

enum class CommandMode {
  kRun,
  kHelp,
};

enum class WindowMode {
  kUnspecified,
  kHeadless,
  kWindowed,
};

struct CommandLineOptions {
  CommandMode mode = CommandMode::kRun;
  WindowMode window_mode = WindowMode::kUnspecified;
  std::string program_name = "mocktail";
  std::string roblox_library_path;
  std::string assets_path;
  std::string graphics_backend;
  // Place to join without a roblox:// URI. Mutually exclusive with a launch
  // URI; both feed the same launch request.
  int64_t place_id = 0;
  // Raw roblox:// argument: a place or user launch, or a web-login ticket for
  // the engine's own linking protocols. Bearer material, scrubbed from argv.
  std::string raw_launch_argument;
  int launch_argument_index = -1;
  // A roblox:// argument that is not a place launch: a web-login ticket for
  // the engine's own linking protocols to redeem. Deliberately kept out of
  // raw_launch_argument, the re-exec argv and the environment, because it is
  // a bearer value. Cleared once the engine has consumed it.
  std::string engine_launch_uri;
  // .ROBLOSECURITY value. Same bearer handling as the launch URI: scrubbed
  // from argv, applied to the environment for the engine, never written to
  // disk.
  std::string roblosecurity;
  int roblosecurity_argument_index = -1;
};

struct CommandLineParseResult {
  CommandLineOptions options;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

CommandLineParseResult ParseCommandLine(int argc, const char* const argv[]);
// Overwrites the original bearer arguments in argv, then clears the owned
// strings, so cookies and tickets never outlive startup.
void ScrubCommandLineLaunchArguments(CommandLineOptions* options, int argc,
                                     char* argv[]);
// Erases the web-login bearer value once the engine has consumed it.
void ScrubEngineLaunchUri(CommandLineOptions* options);
bool ApplyCommandLineEnvironment(const CommandLineOptions& options,
                                 std::string* error);
std::string CommandLineUsage(const std::string& program_name);

}  // namespace runtime
}  // namespace mocktail

#endif  // MOCKTAIL_RUNTIME_COMMAND_LINE_H_
