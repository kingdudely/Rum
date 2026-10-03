#include "runtime/platform_cache_migration.h"

#define JSON_NOEXCEPTION 1
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "mocktail/platform/posix_primitives.h"
#include "runtime/atomic_json_file.h"
#include "runtime/environment.h"
#include "runtime/runtime_paths.h"

namespace mocktail {
namespace runtime {
namespace {

constexpr std::uintmax_t kMaximumJsonBytes = 16U * 1024U * 1024U;
constexpr int kFingerprintSchemaVersion = 1;
constexpr std::string_view kAndroidDataPrefix =
    "/data/user/0/com.roblox.client";
constexpr std::string_view kLegacyAndroidDataPrefix =
    "/data/data/com.roblox.client";

using platform::ScopedFileDescriptor;
using platform::WriteAll;

constexpr AtomicJsonWriteNames kAtomicWriteNames{
    "platform cache metadata", ".mocktail-platform-cache.tmp."};

enum class ReadStatus { kMissing, kReady, kInvalid };

struct JsonReadResult {
  ReadStatus status = ReadStatus::kMissing;
  nlohmann::json value;
  std::string error;
};

bool HasPathPrefix(const std::filesystem::path& path,
                   const std::filesystem::path& prefix) {
  auto path_component = path.begin();
  auto prefix_component = prefix.begin();
  while (prefix_component != prefix.end()) {
    if (path_component == path.end() || *path_component != *prefix_component) {
      return false;
    }
    ++path_component;
    ++prefix_component;
  }
  return true;
}

std::optional<std::filesystem::path> TranslateAndroidFilesPath(
    std::string_view configured, const std::filesystem::path& data_root) {
  for (const std::string_view prefix :
       {kAndroidDataPrefix, kLegacyAndroidDataPrefix}) {
    if (configured == prefix) {
      return data_root;
    }
    if (configured.size() > prefix.size() &&
        configured.compare(0, prefix.size(), prefix) == 0 &&
        configured[prefix.size()] == '/') {
      return data_root / std::string(configured.substr(prefix.size() + 1));
    }
  }
  const std::filesystem::path host_path(configured);
  return host_path.is_absolute() ? std::optional(host_path) : std::nullopt;
}

JsonReadResult ReadJson(const std::filesystem::path& path) {
  const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0) {
    if (errno == ENOENT) {
      return {};
    }
    return {
        ReadStatus::kInvalid, {}, "cannot safely open platform cache metadata"};
  }
  const ScopedFileDescriptor file(descriptor);
  struct stat status{};
  if (fstat(file.get(), &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_size < 0 ||
      static_cast<std::uintmax_t>(status.st_size) > kMaximumJsonBytes) {
    return {ReadStatus::kInvalid,
            {},
            "platform cache metadata is not a bounded regular file"};
  }

  std::string bytes(static_cast<std::size_t>(status.st_size), '\0');
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count =
        read(file.get(), bytes.data() + offset, bytes.size() - offset);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {ReadStatus::kInvalid, {}, "cannot read platform cache metadata"};
    }
    if (count == 0) {
      return {ReadStatus::kInvalid,
              {},
              "platform cache metadata changed while being read"};
    }
    offset += static_cast<std::size_t>(count);
  }
  nlohmann::json parsed = nlohmann::json::parse(bytes, nullptr, false, true);
  std::fill(bytes.begin(), bytes.end(), '\0');
  if (parsed.is_discarded() || !parsed.is_object()) {
    return {ReadStatus::kInvalid,
            {},
            "platform cache metadata is not a JSON object"};
  }
  return {ReadStatus::kReady, std::move(parsed), {}};
}

std::string FingerprintRevision(const nlohmann::json& fingerprint) {
  const auto schema = fingerprint.find("schema_version");
  const auto revision = fingerprint.find("profile_revision");
  if (schema == fingerprint.end() || !schema->is_number_integer() ||
      schema->get<int>() != kFingerprintSchemaVersion ||
      revision == fingerprint.end() || !revision->is_string()) {
    return {};
  }
  return revision->get<std::string>();
}

}  // namespace

std::string BuildPlatformProfileRevision(std::string_view device_profile,
                                         bool touch_enabled, bool mouse_enabled,
                                         bool keyboard_enabled) {
  return "device-v1-" + std::string(device_profile) +
         (touch_enabled ? "-t1" : "-t0") + (mouse_enabled ? "-m1" : "-m0") +
         (keyboard_enabled ? "-k1" : "-k0");
}

PlatformCacheMigrationResult MigratePlatformProfileCaches(
    const Environment& environment, const RuntimePaths& paths,
    std::string_view desired_revision) {
  PlatformCacheMigrationResult result;
  if (desired_revision.empty() || desired_revision.size() > 64) {
    result.error = "platform profile revision is invalid";
    return result;
  }

  const std::filesystem::path runtime_root =
      environment.HasNonEmpty("MOCKTAIL_RUNTIME_ROOT")
          ? std::filesystem::path(
                environment.GetOr("MOCKTAIL_RUNTIME_ROOT", ""))
          : paths.android_runtime_root();
  const std::filesystem::path data_root = runtime_root / "data";
  std::filesystem::path files_root = data_root / "files";
  if (environment.HasNonEmpty("MOCKTAIL_ANDROID_FILES_DIR")) {
    const std::optional<std::filesystem::path> translated =
        TranslateAndroidFilesPath(
            environment.GetOr("MOCKTAIL_ANDROID_FILES_DIR", ""), data_root);
    if (!translated.has_value()) {
      result.error = "configured Android files directory is not absolute";
      return result;
    }
    files_root = *translated;
  }

  const std::filesystem::path normalized_data = data_root.lexically_normal();
  const std::filesystem::path normalized_files = files_root.lexically_normal();
  if (!normalized_data.is_absolute() || !normalized_files.is_absolute() ||
      !HasPathPrefix(normalized_files, normalized_data)) {
    result.error = "platform cache path escapes the Mocktail Android data root";
    return result;
  }

  result.app_storage_file =
      normalized_files / "appData/LocalStorage/appStorage.json";
  result.fingerprint_file = normalized_data / ".mocktail-platform-profile.json";
  std::error_code filesystem_error;
  if (!RuntimePaths::EnsureDirectory(normalized_data, &filesystem_error)) {
    result.error = "cannot create Mocktail Android data root";
    return result;
  }

  const std::filesystem::path lock_path =
      normalized_data / ".mocktail-platform-profile.lock";
  const int lock_descriptor =
      open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock_descriptor < 0) {
    result.error = "cannot open platform cache migration lock";
    return result;
  }
  const ScopedFileDescriptor lock(lock_descriptor);
  struct stat lock_status = {};
  if (fstat(lock.get(), &lock_status) != 0 || !S_ISREG(lock_status.st_mode) ||
      lock_status.st_uid != geteuid() || lock_status.st_nlink != 1 ||
      (lock_status.st_mode & (S_IRWXG | S_IRWXO)) != 0 ||
      flock(lock.get(), LOCK_EX | LOCK_NB) != 0) {
    result.error = "another process owns the platform cache migration lock";
    return result;
  }

  const JsonReadResult fingerprint = ReadJson(result.fingerprint_file);
  if (fingerprint.status == ReadStatus::kInvalid) {
    result.error = fingerprint.error;
    return result;
  }
  if (fingerprint.status == ReadStatus::kReady) {
    result.previous_revision = FingerprintRevision(fingerprint.value);
  }
  if (result.previous_revision == desired_revision) {
    return result;
  }
  result.transitioned = true;

  JsonReadResult app_storage = ReadJson(result.app_storage_file);
  if (app_storage.status == ReadStatus::kInvalid) {
    result.error = app_storage.error;
    return result;
  }
  if (app_storage.status == ReadStatus::kReady) {
    result.app_storage_found = true;
    const std::size_t removed =
        app_storage.value.erase("PlayerHydrationBlob") +
        app_storage.value.erase("PlayerHydrationSignature") +
        app_storage.value.erase("AppConfiguration");
    if (removed != 0) {
      if (!AtomicWriteJson(result.app_storage_file, app_storage.value,
                           kAtomicWriteNames, &result.error)) {
        return result;
      }
      result.app_storage_updated = true;
    }
  }

  nlohmann::json updated_fingerprint = {
      {"schema_version", kFingerprintSchemaVersion},
      {"profile_revision", std::string(desired_revision)},
  };
  if (!AtomicWriteJson(result.fingerprint_file, updated_fingerprint,
                       kAtomicWriteNames, &result.error)) {
    return result;
  }
  return result;
}

RobloxThemeCacheResult ReadRobloxThemeCache(
    const std::filesystem::path& app_storage_file,
    std::int64_t authenticated_user_id) {
  RobloxThemeCacheResult result;
  if (!app_storage_file.is_absolute() || authenticated_user_id < -1) {
    result.error = "Roblox theme cache path or user ID is invalid";
    return result;
  }

  const JsonReadResult storage = ReadJson(app_storage_file);
  if (storage.status == ReadStatus::kInvalid) {
    result.error = storage.error;
    return result;
  }
  if (storage.status == ReadStatus::kMissing) {
    return result;
  }

  const auto read_theme = [&result](const nlohmann::json& value,
                                    std::string_view name) {
    if (!value.is_string()) {
      result.error = std::string(name) + " is not a string";
      return;
    }
    const std::string& theme = value.get_ref<const std::string&>();
    if (theme != "dark" && theme != "light") {
      result.error = std::string(name) + " must be dark or light";
      return;
    }
    result.dark_theme = theme == "dark";
  };

  if (authenticated_user_id >= 0) {
    const auto encoded_device_themes = storage.value.find("DeviceLevelTheme");
    if (encoded_device_themes != storage.value.end()) {
      if (!encoded_device_themes->is_string()) {
        result.error = "Roblox DeviceLevelTheme is not an encoded object";
        return result;
      }
      const nlohmann::json device_themes = nlohmann::json::parse(
          encoded_device_themes->get_ref<const std::string&>(), nullptr, false,
          true);
      if (device_themes.is_discarded() || !device_themes.is_object()) {
        result.error = "Roblox DeviceLevelTheme encoding is invalid";
        return result;
      }
      const auto account_theme =
          device_themes.find(std::to_string(authenticated_user_id));
      if (account_theme != device_themes.end()) {
        read_theme(*account_theme, "Roblox account theme");
        return result;
      }
    }
  }

  const auto authenticated_theme = storage.value.find("AuthenticatedTheme");
  if (authenticated_theme != storage.value.end()) {
    read_theme(*authenticated_theme, "Roblox authenticated theme");
  }
  return result;
}

bool ApplyRobloxThemeCacheOverride(
    const std::filesystem::path& app_storage_file,
    std::int64_t authenticated_user_id, bool dark_theme, std::string* error) {
  if (error == nullptr || !app_storage_file.is_absolute() ||
      authenticated_user_id < -1) {
    if (error != nullptr) {
      *error = "Roblox theme cache path or user ID is invalid";
    }
    return false;
  }

  JsonReadResult storage = ReadJson(app_storage_file);
  if (storage.status == ReadStatus::kInvalid) {
    *error = storage.error;
    return false;
  }
  if (storage.status == ReadStatus::kMissing) {
    storage.value = nlohmann::json::object();
  }

  const std::string desired_theme = dark_theme ? "dark" : "light";
  const nlohmann::json original = storage.value;
  storage.value["AuthenticatedTheme"] = desired_theme;

  if (authenticated_user_id >= 0) {
    nlohmann::json device_themes = nlohmann::json::object();
    const auto encoded_device_themes = storage.value.find("DeviceLevelTheme");
    if (encoded_device_themes != storage.value.end()) {
      if (!encoded_device_themes->is_string()) {
        *error = "Roblox DeviceLevelTheme is not an encoded object";
        return false;
      }
      device_themes = nlohmann::json::parse(
          encoded_device_themes->get_ref<const std::string&>(), nullptr, false,
          true);
      if (device_themes.is_discarded() || !device_themes.is_object()) {
        *error = "Roblox DeviceLevelTheme encoding is invalid";
        return false;
      }
    }
    device_themes[std::to_string(authenticated_user_id)] = desired_theme;
    storage.value["DeviceLevelTheme"] = device_themes.dump();
  }

  if (storage.value == original) {
    return true;
  }
  return AtomicWriteJson(app_storage_file, storage.value, kAtomicWriteNames,
                       error);
}

}  // namespace runtime
}  // namespace mocktail
