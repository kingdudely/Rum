#ifndef MOCKTAIL_RUNTIME_RUNTIME_PATHS_H_
#define MOCKTAIL_RUNTIME_RUNTIME_PATHS_H_

#include <filesystem>
#include <system_error>

#include "runtime/environment.h"

namespace mocktail {
namespace runtime {

class RuntimePaths {
 public:
  static RuntimePaths FromEnvironment(const Environment& environment);

  const std::filesystem::path& home() const { return home_; }
  const std::filesystem::path& cache_root() const { return cache_root_; }
  const std::filesystem::path& config_root() const { return config_root_; }
  const std::filesystem::path& data_root() const { return data_root_; }
  const std::filesystem::path& state_root() const { return state_root_; }
  const std::filesystem::path& logs_root() const { return logs_root_; }
  const std::filesystem::path& android_runtime_root() const {
    return android_runtime_root_;
  }
  const std::filesystem::path& android_cache_root() const {
    return android_cache_root_;
  }
  const std::filesystem::path& vulkan_shader_cache_file() const {
    return vulkan_shader_cache_file_;
  }
  const std::filesystem::path& active_payload_manifest() const {
    return active_payload_manifest_;
  }

  std::filesystem::path DefaultAssetPath() const;

  static bool EnsureDirectory(const std::filesystem::path& path,
                              std::error_code* error = nullptr);

 private:
  std::filesystem::path home_ = "/root";
  std::filesystem::path cache_root_;
  std::filesystem::path config_root_;
  std::filesystem::path data_root_;
  std::filesystem::path state_root_;
  std::filesystem::path logs_root_;
  std::filesystem::path android_runtime_root_;
  std::filesystem::path android_cache_root_;
  std::filesystem::path vulkan_shader_cache_file_;
  std::filesystem::path active_payload_manifest_;
};

// Publishes the XDG-backed host paths consumed by the transitional Bionic
// filesystem shim. Existing non-empty overrides remain authoritative.
bool ExportRuntimePathEnvironment(const RuntimePaths& paths,
                                  std::string* error = nullptr);

// The directory holding the running executable. Empty if it cannot be
// determined. Every default path resolves against this, never the CWD, so the
// client behaves the same however it is invoked.
std::filesystem::path ExecutableDirectory();

// Bare-`roblox` layouts ship libroblox.so and assets/content side by side.
// Both default to the executable's own directory: <exe dir>/libroblox.so and
// <exe dir>/assets/content.
std::filesystem::path DefaultRobloxLibraryPath();
std::filesystem::path DefaultRobloxAssetPath();

// Accepts either the assets root or its content/ subdirectory and always
// returns the content root, which is the form every consumer expects.
// Idempotent, so normalising an already-normalised path changes nothing.
std::filesystem::path NormalizeRobloxAssetPath(
    const std::filesystem::path& path);

// True when `content_root` looks like a Roblox content directory rather than
// an arbitrary folder. Accepts any one of the long-standing marker
// subdirectories, so a content reorganisation cannot hard-fail startup.
bool LooksLikeRobloxContentDirectory(
    const std::filesystem::path& content_root);

}  // namespace runtime
}  // namespace mocktail

#endif  // MOCKTAIL_RUNTIME_RUNTIME_PATHS_H_
