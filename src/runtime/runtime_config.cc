#include "runtime/runtime_config.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cstdlib>
#include <optional>

namespace mocktail {
namespace runtime {
namespace {

// Overrides that put an engine call on a detached thread. Every name here has
// a step it belongs to, so retiring a step retires its thread override too.
constexpr std::array<std::string_view, 5> kUnsafeDetachedThreadOverrides = {
    "MOCKTAIL_APP_BRIDGE_APP_START_THREAD",
    "MOCKTAIL_CALL_REAL_APP_BRIDGE_INIT_THREAD",
    "MOCKTAIL_START_LUA_APP_DM_THREAD",
    "MOCKTAIL_CALL_REAL_APP_BRIDGE_START_THREAD",
    "MOCKTAIL_SEND_APP_READY_THREAD",
};

bool LegacyEnabled(const Environment& environment, std::string_view name) {
  const std::optional<std::string> value = environment.Get(name);
  return value.has_value() && !value->empty() && *value != "0";
}

bool ReadBoolean(const Environment& environment, std::string_view name,
                 bool default_value, bool* valid) {
  const std::optional<std::string> value = environment.Get(name);
  if (!value.has_value()) {
    return default_value;
  }
  if (*value == "1" || *value == "true" || *value == "on") {
    return true;
  }
  if (*value == "0" || *value == "false" || *value == "off") {
    return false;
  }
  *valid = false;
  return default_value;
}

std::optional<bool> InputEnabled(const Environment& environment,
                                 std::string_view name) {
  const std::optional<std::string> value = environment.Get(name);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return *value == "on" || *value == "1" || *value == "true";
}

bool ApplyDeviceProfileOverride(const Environment& environment,
                                std::string_view variable, std::size_t maximum,
                                std::string* target, bool* customized) {
  const std::optional<std::string> value = environment.Get(variable);
  if (!value.has_value()) {
    return true;
  }
  if (!IsValidDeviceProfileValue(*value, maximum)) {
    return false;
  }
  if (*target != *value) {
    *customized = true;
    *target = *value;
  }
  return true;
}

bool DesktopPlayabilityEnabled(const Environment& environment) {
  const std::optional<std::string> value =
      environment.Get("MOCKTAIL_DESKTOP_PLAYABILITY");
  return !value.has_value() ||
         (*value != "0" && *value != "off" && *value != "false");
}

int ReadPositiveInt(const Environment& environment, std::string_view name,
                    int default_value) {
  const std::optional<std::string> value = environment.Get(name);
  if (!value.has_value() || value->empty()) {
    return default_value;
  }

  errno = 0;
  char* end = nullptr;
  const long parsed = std::strtol(value->c_str(), &end, 10);
  if (end == value->c_str() || errno == ERANGE || parsed <= 0 ||
      parsed > INT_MAX) {
    return default_value;
  }
  return static_cast<int>(parsed);
}

std::optional<NetworkProxyConfig> ReadNetworkProxy(
    const Environment& environment) {
  const std::optional<std::string> host =
      environment.Get("MOCKTAIL_HTTP_PROXY_HOST");
  const std::optional<std::string> port =
      environment.Get("MOCKTAIL_HTTP_PROXY_PORT");
  const std::string scheme =
      environment.GetOr("MOCKTAIL_HTTP_PROXY_SCHEME", "http");
  if (!host.has_value() || host->empty() || !port.has_value()) {
    return std::nullopt;
  }
  return ParseNetworkProxyConfig(*host, *port, scheme);
}

}  // namespace

std::optional<NetworkProxyConfig> ParseNetworkProxyConfig(
    std::string_view host, std::string_view port, std::string_view scheme) {
  std::string normalized_scheme;
  if (scheme == "http" || scheme == "https") {
    // Desktop resolvers commonly label the proxy selected for an HTTPS URL as
    // `https://` even when the local endpoint speaks plain HTTP CONNECT.
    normalized_scheme = "http";
  } else if (scheme == "socks" || scheme == "socks5" ||
             scheme == "socks5h") {
    normalized_scheme = "socks5h";
  } else {
    return std::nullopt;
  }
  if (host.empty() || port.empty() || host.find("://") != std::string::npos ||
      std::any_of(host.begin(), host.end(), [](unsigned char character) {
        return character <= 0x20 || character == 0x7f || character == '/' ||
               character == '\\' || character == '@' || character == '[' ||
               character == ']' || character == '?' || character == '#';
      })) {
    return std::nullopt;
  }
  int parsed_port = 0;
  const auto conversion =
      std::from_chars(port.data(), port.data() + port.size(), parsed_port);
  if (conversion.ec != std::errc() ||
      conversion.ptr != port.data() + port.size() || parsed_port <= 0 ||
      parsed_port > 65535) {
    return std::nullopt;
  }
  return NetworkProxyConfig{std::move(normalized_scheme), std::string(host),
                            parsed_port};
}

std::string BuildNetworkProxyUrl(const NetworkProxyConfig& proxy) {
  const bool ipv6 = proxy.host.find(':') != std::string::npos;
  return proxy.scheme + "://" + std::string(ipv6 ? "[" : "") + proxy.host +
         (ipv6 ? "]" : "") + ":" + std::to_string(proxy.port);
}

RuntimeConfig RuntimeConfig::FromEnvironment(const Environment& environment) {
  RuntimeConfig config;
  config.headless_ = LegacyEnabled(environment, "MOCKTAIL_HEADLESS");
  config.roblox_library_path_ = environment.GetOr(
      "ROBLOX_LIB_PATH", config.roblox_library_path_.string());
  if (config.roblox_library_path_.empty()) {
    config.roblox_library_path_ = DefaultRobloxLibraryPath();
  }
  config.graphics_backend_name_ = environment.GetOr(
      "MOCKTAIL_GRAPHICS_BACKEND", config.graphics_backend_name_);
  config.graphics_backend_ =
      ParseGraphicsBackend(config.graphics_backend_name_);
  config.window_.width =
      ReadPositiveInt(environment, "MOCKTAIL_WIN_WIDTH", config.window_.width);
  config.window_.height = ReadPositiveInt(environment, "MOCKTAIL_WIN_HEIGHT",
                                          config.window_.height);
  config.window_.title =
      environment.GetOr("MOCKTAIL_WIN_TITLE", config.window_.title);
  config.window_.high_dpi = ReadBoolean(environment, "MOCKTAIL_WIN_HIGH_DPI",
                                        config.window_.high_dpi,
                                        &config.window_.high_dpi_valid);
  config.theme_mode_ =
      environment.GetOr("MOCKTAIL_THEME", config.theme_mode_);
  const std::optional<std::string> configured_device =
      environment.Get("MOCKTAIL_DEVICE_PROFILE");
  const bool has_explicit_device =
      configured_device.has_value() && !configured_device->empty();
  const bool has_legacy_playability =
      environment.Get("MOCKTAIL_DESKTOP_PLAYABILITY").has_value();
  std::string_view selected_device = kDefaultDeviceProfileName;
  if (has_explicit_device) {
    selected_device = *configured_device;
  } else if (has_legacy_playability) {
    selected_device = DesktopPlayabilityEnabled(environment)
                          ? std::string_view("pc-windows-11")
                          : std::string_view("mobile-pixel-7");
  }
  const DeviceProfile* profile = FindDeviceProfile(selected_device);
  config.device_profile_valid_ = profile != nullptr;
  if (profile != nullptr) {
    config.device_profile_ = *profile;
  }
  if (!has_legacy_playability || has_explicit_device) {
    config.input_capabilities_.touch_enabled =
        config.device_profile_.touch_enabled;
    config.input_capabilities_.mouse_enabled =
        config.device_profile_.mouse_enabled;
    config.input_capabilities_.keyboard_enabled =
        config.device_profile_.keyboard_enabled;
  }
  if (const std::optional<bool> touch =
          InputEnabled(environment, "MOCKTAIL_TOUCH_MODE");
      touch.has_value()) {
    config.input_capabilities_.touch_enabled = *touch;
  }
  if (const std::optional<bool> mouse =
          InputEnabled(environment, "MOCKTAIL_MOUSE_MODE");
      mouse.has_value()) {
    config.input_capabilities_.mouse_enabled = *mouse;
  }
  if (const std::optional<bool> keyboard =
          InputEnabled(environment, "MOCKTAIL_KEYBOARD_MODE");
      keyboard.has_value()) {
    config.input_capabilities_.keyboard_enabled = *keyboard;
  }
  bool customized_device = false;
  config.device_profile_valid_ =
      config.device_profile_valid_ &&
      ApplyDeviceProfileOverride(environment, "MOCKTAIL_DEVICE_PLATFORM_NAME",
                                 128, &config.device_profile_.platform_name,
                                 &customized_device) &&
      ApplyDeviceProfileOverride(environment, "MOCKTAIL_DEVICE_NAME", 128,
                                 &config.device_profile_.display_name,
                                 &customized_device) &&
      ApplyDeviceProfileOverride(environment, "MOCKTAIL_DEVICE_MANUFACTURER",
                                 128, &config.device_profile_.manufacturer,
                                 &customized_device) &&
      ApplyDeviceProfileOverride(environment, "MOCKTAIL_DEVICE_MODEL", 128,
                                 &config.device_profile_.model,
                                 &customized_device) &&
      ApplyDeviceProfileOverride(environment, "MOCKTAIL_DEVICE_BRAND", 64,
                                 &config.device_profile_.brand,
                                 &customized_device) &&
      ApplyDeviceProfileOverride(environment, "MOCKTAIL_DEVICE_CODE", 64,
                                 &config.device_profile_.device_code,
                                 &customized_device) &&
      ApplyDeviceProfileOverride(environment, "MOCKTAIL_DEVICE_SKU", 64,
                                 &config.device_profile_.device_sku,
                                 &customized_device) &&
      ApplyDeviceProfileOverride(environment, "MOCKTAIL_DEVICE_SOC_MODEL", 128,
                                 &config.device_profile_.soc_model,
                                 &customized_device);
  if (customized_device && config.device_profile_valid_) {
    config.device_profile_.cache_key =
        BuildCustomDeviceProfileCacheKey(config.device_profile_);
  }
  const std::optional<std::string> configured_user_agent =
      environment.Get("MOCKTAIL_USER_AGENT");
  if (configured_user_agent.has_value() && !configured_user_agent->empty()) {
    config.roblox_http_user_agent_ = *configured_user_agent;
  } else if (!config.device_profile_.roblox_http_user_agent.empty()) {
    config.roblox_http_user_agent_ =
        config.device_profile_.roblox_http_user_agent;
  }
  config.frame_rate_ = ParseFrameRatePolicy(
      environment.GetOr("MOCKTAIL_FRAME_RATE_LIMIT", "-1"));
  config.vsync_mode_ = environment.GetOr("MOCKTAIL_VSYNC", "auto");
  config.performance_ = ParsePerformancePolicy(
      environment.GetOr("MOCKTAIL_MULTITHREADED_RENDERING", "0"),
      environment.GetOr("MOCKTAIL_MEMORY_LIMIT_MB", "0"),
      environment.GetOr("MOCKTAIL_GAMEMODE", "auto"),
      environment.GetOr("MOCKTAIL_PHYSICS_WORKER_MODE", "throughput"));
  config.audio_output_device_ = environment.GetOr(
      "MOCKTAIL_AUDIO_OUTPUT_DEVICE", config.audio_output_device_);
  config.audio_output_device_valid_ =
      IsValidDeviceProfileValue(config.audio_output_device_, 512);
  config.audio_input_device_ = environment.GetOr(
      "MOCKTAIL_AUDIO_INPUT_DEVICE", config.audio_input_device_);
  config.audio_input_device_valid_ =
      IsValidDeviceProfileValue(config.audio_input_device_, 512);
  config.network_proxy_ = ReadNetworkProxy(environment);
  if (const std::optional<std::string> ca_bundle =
          environment.Get("MOCKTAIL_CA_BUNDLE");
      ca_bundle.has_value()) {
    config.ca_bundle_ = *ca_bundle;
    config.ca_bundle_valid_ = !ca_bundle->empty() &&
                              config.ca_bundle_->is_absolute();
  }
  for (const std::string_view name : kUnsafeDetachedThreadOverrides) {
    if (LegacyEnabled(environment, name)) {
      config.unsafe_detached_thread_overrides_.emplace_back(name);
    }
  }
  return config;
}

GraphicsBackend RuntimeConfig::ParseGraphicsBackend(std::string_view name) {
  if (name.empty() || name == "auto") {
    return GraphicsBackend::kAuto;
  }
  if (name == "system" || name == "gles" || name == "opengl") {
    return GraphicsBackend::kSystem;
  }
  if (name == "vulkan" || name == "native-vulkan" || name == "direct-vulkan") {
    return GraphicsBackend::kVulkan;
  }
  if (name == "angle-vulkan") {
    return GraphicsBackend::kAngleVulkan;
  }
  if (name == "angle-swiftshader") {
    return GraphicsBackend::kAngleSwiftShader;
  }
  return GraphicsBackend::kUnknown;
}

namespace {

std::string FrameRateValue(const FrameRatePolicy& policy) {
  if (policy.mode == FrameRateLimitMode::kUnmanaged) {
    return "-1";
  }
  if (policy.mode == FrameRateLimitMode::kUnlimited) {
    return "unlimited";
  }
  if (policy.mode == FrameRateLimitMode::kFixed) {
    return std::to_string(policy.fixed_fps);
  }
  return "display";
}

bool SetEnvironmentValue(const char* name, const std::string& value,
                         std::string* error) {
  if (setenv(name, value.c_str(), 1) == 0) {
    return true;
  }
  if (error != nullptr) {
    *error = std::string("cannot export resolved runtime setting: ") + name;
  }
  return false;
}

bool UnsetEnvironmentValue(const char* name, std::string* error) {
  if (unsetenv(name) == 0) {
    return true;
  }
  if (error != nullptr) {
    *error = std::string("cannot clear resolved runtime setting: ") + name;
  }
  return false;
}

}  // namespace

bool ExportRuntimeConfigEnvironment(const RuntimeConfig& config,
                                    std::string* error) {
  if (!config.device_profile_valid()) {
    if (error != nullptr) {
      *error = "cannot export an invalid device profile";
    }
    return false;
  }
  if (!config.audio_output_device_valid()) {
    if (error != nullptr) {
      *error = "cannot export an invalid audio output device";
    }
    return false;
  }
  if (!config.audio_input_device_valid()) {
    if (error != nullptr) {
      *error = "cannot export an invalid audio input device";
    }
    return false;
  }
  if (!config.performance().memory_limit_valid) {
    if (error != nullptr) {
      *error = "cannot export an invalid memory-limit policy";
    }
    return false;
  }
  if (!config.performance().game_mode_valid) {
    if (error != nullptr) {
      *error = "cannot export an invalid GameMode policy";
    }
    return false;
  }
  if (!config.performance().physics_worker_mode_valid) {
    if (error != nullptr) {
      *error = "cannot export an invalid physics worker policy";
    }
    return false;
  }
  if (!config.ca_bundle_valid()) {
    if (error != nullptr) {
      *error = "cannot export an invalid CA bundle path";
    }
    return false;
  }
  const DeviceProfile& device = config.device_profile();
  const bool base_exported =
      SetEnvironmentValue("MOCKTAIL_HEADLESS", config.headless() ? "1" : "0",
                          error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_PROFILE", std::string(device.name),
                          error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_CLASS",
                          std::string(DeviceClassName(device.device_class)),
                          error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_PLATFORM_NAME",
                          std::string(device.platform_name), error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_NAME",
                          std::string(device.display_name), error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_MANUFACTURER",
                          std::string(device.manufacturer), error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_MODEL", std::string(device.model),
                          error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_BRAND", std::string(device.brand),
                          error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_CODE",
                          std::string(device.device_code), error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_SKU", std::string(device.device_sku),
                          error) &&
      SetEnvironmentValue("MOCKTAIL_DEVICE_SOC_MODEL",
                          std::string(device.soc_model), error) &&
      SetEnvironmentValue("ROBLOX_LIB_PATH",
                          config.roblox_library_path().string(), error) &&
      SetEnvironmentValue("MOCKTAIL_GRAPHICS_BACKEND",
                          config.graphics_backend_name(), error) &&
      SetEnvironmentValue("MOCKTAIL_THEME", config.theme_mode(), error) &&
      SetEnvironmentValue("MOCKTAIL_WIN_WIDTH",
                          std::to_string(config.window().width), error) &&
      SetEnvironmentValue("MOCKTAIL_WIN_HEIGHT",
                          std::to_string(config.window().height), error) &&
      SetEnvironmentValue("MOCKTAIL_WIN_TITLE", config.window().title, error) &&
      SetEnvironmentValue("MOCKTAIL_WIN_HIGH_DPI",
                          config.window().high_dpi ? "1" : "0", error) &&
      SetEnvironmentValue(
          "MOCKTAIL_TOUCH_MODE",
          config.input_capabilities().touch_enabled ? "on" : "off", error) &&
      SetEnvironmentValue(
          "MOCKTAIL_MOUSE_MODE",
          config.input_capabilities().mouse_enabled ? "on" : "off", error) &&
      SetEnvironmentValue(
          "MOCKTAIL_KEYBOARD_MODE",
          config.input_capabilities().keyboard_enabled ? "on" : "off", error) &&
      SetEnvironmentValue("MOCKTAIL_DESKTOP_PLAYABILITY",
                          config.desktop_playability() ? "1" : "0", error) &&
      SetEnvironmentValue("MOCKTAIL_FRAME_RATE_LIMIT",
                          FrameRateValue(config.frame_rate()), error) &&
      SetEnvironmentValue("MOCKTAIL_VSYNC", config.vsync_mode(), error) &&
      SetEnvironmentValue(
          "MOCKTAIL_MULTITHREADED_RENDERING",
          config.performance().multithreaded_rendering ? "1" : "0", error) &&
      SetEnvironmentValue("MOCKTAIL_PHYSICS_WORKER_MODE",
                          std::string(PhysicsWorkerModeName(
                              config.performance().physics_worker_mode)),
                          error) &&
      SetEnvironmentValue("MOCKTAIL_MEMORY_LIMIT_MB",
                          std::to_string(config.performance().memory_limit_mb),
                          error) &&
      SetEnvironmentValue("MOCKTAIL_GAMEMODE",
                          GameModePolicyName(config.performance().game_mode),
                          error) &&
      SetEnvironmentValue("MOCKTAIL_AUDIO_OUTPUT_DEVICE",
                          config.audio_output_device(), error) &&
      SetEnvironmentValue("MOCKTAIL_AUDIO_INPUT_DEVICE",
                          config.audio_input_device(), error);
  if (!base_exported) {
    return false;
  }
  if (config.ca_bundle().has_value()) {
    if (!SetEnvironmentValue("MOCKTAIL_CA_BUNDLE",
                             config.ca_bundle()->string(), error)) {
      return false;
    }
  } else if (!UnsetEnvironmentValue("MOCKTAIL_CA_BUNDLE", error)) {
    return false;
  }
  if (config.roblox_http_user_agent().has_value() &&
      !SetEnvironmentValue("MOCKTAIL_USER_AGENT",
                           *config.roblox_http_user_agent(), error)) {
    return false;
  }
  if (!config.roblox_http_user_agent().has_value() &&
      !UnsetEnvironmentValue("MOCKTAIL_USER_AGENT", error)) {
    return false;
  }
  if (!config.network_proxy().has_value()) {
    return base_exported;
  }
  return SetEnvironmentValue("MOCKTAIL_HTTP_PROXY_HOST",
                             config.network_proxy()->host, error) &&
         SetEnvironmentValue("MOCKTAIL_HTTP_PROXY_PORT",
                             std::to_string(config.network_proxy()->port),
                             error) &&
         SetEnvironmentValue("MOCKTAIL_HTTP_PROXY_SCHEME",
                             config.network_proxy()->scheme, error) &&
         SetEnvironmentValue("MOCKTAIL_NATIVE_SET_HTTP_CLIENT_PROXY", "1",
                             error);
}

bool LoadRuntimeConfigFromEnvironment(const Environment& environment,
                                      RuntimeConfig* config,
                                      std::string* error) {
  if (config == nullptr) {
    if (error != nullptr) {
      *error = "runtime config output is null";
    }
    return false;
  }
  *config = RuntimeConfig::FromEnvironment(environment);
  const auto fail = [&](std::string message) {
    if (error != nullptr) {
      *error = std::move(message);
    }
    return false;
  };
  if (!config->frame_rate().valid()) {
    return fail("frame-rate policy is invalid");
  } else if (!config->device_profile_valid()) {
    return fail("device profile is invalid");
  } else if (!config->input_capabilities().touch_enabled &&
             !config->input_capabilities().mouse_enabled &&
             !config->input_capabilities().keyboard_enabled) {
    return fail("device must expose at least one usable input capability");
  } else if (config->graphics_backend() == GraphicsBackend::kUnknown) {
    return fail("graphics backend is invalid");
  } else if (!config->theme_mode_valid()) {
    return fail("theme mode is invalid");
  } else if (!config->window().high_dpi_valid) {
    return fail("window high-DPI policy is invalid");
  } else if (config->vsync_mode() != "auto" && config->vsync_mode() != "on" &&
             config->vsync_mode() != "off") {
    return fail("VSync policy is invalid");
  } else if (!config->audio_output_device_valid()) {
    return fail("audio output device is invalid");
  } else if (!config->audio_input_device_valid()) {
    return fail("audio input device is invalid");
  } else if (!config->performance().memory_limit_valid) {
    return fail("memory-limit policy is invalid");
  } else if (!config->performance().game_mode_valid) {
    return fail("GameMode policy is invalid");
  } else if (!config->performance().physics_worker_mode_valid) {
    return fail("physics worker policy is invalid");
  } else if (!config->ca_bundle_valid()) {
    return fail("CA bundle path is invalid");
  }
  return true;
}

}  // namespace runtime
}  // namespace mocktail
