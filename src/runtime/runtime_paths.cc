// Includes changes by vii from CoderDayton/nightcap.
#include "runtime/runtime_paths.h"

#define JSON_NOEXCEPTION 1
#include <cstdlib>
#include <optional>
#include <string>
#include <unistd.h>

namespace mocktail {
namespace runtime {
namespace {

                           bool SetEnvironmentDefault(const char* name,
                           const std::filesystem::path& value,
                           std::string* error) {
  const char* existing = std::getenv(name);
  if (existing != nullptr && existing[0] != '\0') {
    return true;
  }
  if (setenv(name, value.c_str(), 1) == 0) {
    return true;
  }
  if (error != nullptr) {
    *error = std::string("cannot export resolved runtime path: ") + name;
  }
  return false;
}

}  // namespace

RuntimePaths RuntimePaths::FromEnvironment(const Environment& environment) {
  RuntimePaths paths;
  paths.home_ = environment.GetOr("HOME", "/root");

  // Every writable root defaults under ./data next to the executable, so
  // deleting that directory uninstalls everything Mocktail ever wrote.
  // Each root stays overridable for isolation and testing.
  const std::filesystem::path executable_directory = ExecutableDirectory();
  const std::filesystem::path data_default =
      executable_directory.empty()
          ? std::filesystem::path("data")
          : executable_directory / "data";
  paths.data_root_ =
      environment.HasNonEmpty("MOCKTAIL_DATA_ROOT")
          ? std::filesystem::path(environment.GetOr("MOCKTAIL_DATA_ROOT", ""))
          : data_default;
  paths.cache_root_ =
      environment.HasNonEmpty("MOCKTAIL_CACHE_ROOT")
          ? std::filesystem::path(environment.GetOr("MOCKTAIL_CACHE_ROOT", ""))
          : data_default / "cache";
  paths.state_root_ =
      environment.HasNonEmpty("MOCKTAIL_STATE_ROOT")
          ? std::filesystem::path(environment.GetOr("MOCKTAIL_STATE_ROOT", ""))
          : data_default / "state";
  if (environment.HasNonEmpty("MOCKTAIL_CONFIG_ROOT")) {
    paths.config_root_ = environment.GetOr("MOCKTAIL_CONFIG_ROOT", "");
  } else {
    paths.config_root_ = data_default / "config";
  }
  paths.logs_root_ = paths.state_root_ / "logs";
  paths.android_runtime_root_ = paths.data_root_ / "android";
  paths.android_cache_root_ = paths.cache_root_ / "android";
  paths.vulkan_shader_cache_file_ =
      paths.cache_root_ / "graphics/shadercachevk.bin";
  paths.active_payload_manifest_ = paths.data_root_ / "current.json";

  return paths;
}

// The asset root is always relative, so the guest resolves it against the
// rbx_bin directory the Bionic shim mounts, never against the caller's CWD.
std::filesystem::path RuntimePaths::DefaultAssetPath() const {
  return "rbx_bin/assets/content";
}

bool RuntimePaths::EnsureDirectory(const std::filesystem::path& path,
                                   std::error_code* error) {
  std::error_code local_error;
  if (path.empty()) {
    local_error = std::make_error_code(std::errc::invalid_argument);
  } else if (!std::filesystem::create_directories(path, local_error) &&
             !local_error) {
    if (!std::filesystem::is_directory(path, local_error) && !local_error) {
      local_error = std::make_error_code(std::errc::not_a_directory);
    }
  }
  if (error != nullptr) {
    *error = local_error;
  }
  return !local_error;
}

bool ExportRuntimePathEnvironment(const RuntimePaths& paths,
                                  std::string* error) {
  std::error_code filesystem_error;
  for (const std::filesystem::path& directory :
       {paths.android_runtime_root(), paths.android_cache_root(),
        paths.vulkan_shader_cache_file().parent_path()}) {
    if (!RuntimePaths::EnsureDirectory(directory, &filesystem_error)) {
      if (error != nullptr) {
        *error = "cannot create resolved runtime path: " + directory.string();
      }
      return false;
    }
  }

  return SetEnvironmentDefault("MOCKTAIL_RUNTIME_ROOT",
                               paths.android_runtime_root(), error) &&
         SetEnvironmentDefault("MOCKTAIL_ANDROID_CACHE_HOST_ROOT",
                               paths.android_cache_root(), error) &&
         SetEnvironmentDefault("MOCKTAIL_VULKAN_SHADER_CACHE_HOST_PATH",
                               paths.vulkan_shader_cache_file(), error) &&
         SetEnvironmentDefault("MOCKTAIL_TEXTURE_OVERRIDE_DIR",
                               paths.config_root() / "textures", error);
}

std::filesystem::path ExecutableDirectory() {
  static const std::filesystem::path cached = [] {
    std::error_code error;
    const std::filesystem::path self =
        std::filesystem::read_symlink("/proc/self/exe", error);
    if (error || self.empty() || !self.has_parent_path()) {
      return std::filesystem::path();
    }
    return self.parent_path().lexically_normal();
  }();
  return cached;
}

std::filesystem::path DefaultRobloxLibraryPath() {
  const std::filesystem::path directory = ExecutableDirectory();
  return directory.empty() ? std::filesystem::path()
                           : (directory / "libroblox.so").lexically_normal();
}

std::filesystem::path DefaultRobloxAssetPath() {
  const std::filesystem::path directory = ExecutableDirectory();
  return directory.empty()
             ? std::filesystem::path()
             : (directory / "assets" / "content").lexically_normal();
}

std::filesystem::path NormalizeRobloxAssetPath(
    const std::filesystem::path& path) {
  if (path.empty()) {
    return path;
  }
  std::filesystem::path normalized = path.lexically_normal();
  if (normalized.filename() == "content") {
    normalized = normalized.parent_path();
  }
  return (normalized / "content").lexically_normal();
}

bool LooksLikeRobloxContentDirectory(
    const std::filesystem::path& content_root) {
  std::error_code error;
  if (!std::filesystem::is_directory(content_root, error)) {
    return false;
  }
  for (const char* marker : {"configs", "guac", "localization", "fonts",
                             "textures"}) {
    if (std::filesystem::is_directory(content_root / marker, error)) {
      return true;
    }
  }
  return false;
}

}  // namespace runtime
}  // namespace mocktail
