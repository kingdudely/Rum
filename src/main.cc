#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "legacy/legacy_runtime.h"
#include "libc_shim/libc_shim.h"
#include "mocktail/audio/fmod_jni_audio_bridge.h"
#include "mocktail/audio/webrtc_jni_audio_bridge.h"
#include "mocktail/platform/posix_primitives.h"
#include "runtime/auth_runtime_composition.h"
#include "runtime/command_line.h"
#include "runtime/crash_report_policy.h"
#include "runtime/environment.h"
#include "runtime/external_launch_broker.h"
#include "runtime/failure_dialog.h"
#include "runtime/game_mode.h"
#include "runtime/guest_abi_check.h"
#include "runtime/graphics_launch_policy.h"
#include "runtime/memory_limit.h"
#include "runtime/performance_policy.h"
#include "runtime/platform_cache_migration.h"
#include "runtime/process_diagnostics.h"
#include "runtime/process_launch_policy.h"
#include "runtime/roblox_desktop_app_policy.h"
#include "runtime/roblox_experience_launch_bridge.h"
#include "runtime/runtime_config_bootstrap.h"
#include "runtime/runtime_config_file.h"
#include "runtime/runtime_paths.h"
#include "runtime/session_log.h"
#include "runtime/single_instance_lock.h"
#include "runtime/support_bundle.h"
#include "runtime/system_proxy.h"
#include "services/auth_service.h"
#include "services/client_settings_service.h"
#include "services/http_client.h"
#include "window/window.h"

// main() is a startup pipeline: every step below either continues the pipeline,
// ends the process successfully, or ends it with a failure. Each step is one
// named function so the ordering contract is visible in one place and the
// individual policies stay independently readable.
namespace {

using mocktail::runtime::CommandLineOptions;
using mocktail::runtime::CommandMode;
using mocktail::runtime::ProcessEnvironment;
using mocktail::runtime::RobloxExperienceLaunchRequest;
using mocktail::runtime::RuntimeConfig;
using mocktail::runtime::RuntimeConfigLoadResult;
using mocktail::runtime::RuntimePaths;

enum class StepResult {
  kContinue,
  kExitSuccess,
  kExitFailure,
};

struct AndroidWindowBridgeContext {};

class ExternalLaunchBrokerScope final {
 public:
  ~ExternalLaunchBrokerScope() { (void)Shutdown(); }

  std::shared_ptr<mocktail::runtime::ExternalLaunchBroker>& broker() {
    return broker_;
  }

  mocktail::Status Shutdown() {
    if (broker_ == nullptr) {
      return mocktail::Status::Ok();
    }
    mocktail::runtime::ClearActiveExternalLaunchBroker(broker_.get());
    mocktail::Status status = broker_->Shutdown();
    broker_.reset();
    return status;
  }

 private:
  std::shared_ptr<mocktail::runtime::ExternalLaunchBroker> broker_;
};

using mocktail::platform::SecureErase;

void SecureEraseArguments(std::vector<std::string>* arguments) {
  if (arguments == nullptr) return;
  for (std::string& argument : *arguments) {
    SecureErase(argument.data(), argument.size());
  }
  arguments->clear();
}

bool QueueAndroidWindowFlags(void*, int flags, int mask) {
  return mocktail::window::RequestFullscreenFromAndroidWindowFlags(flags, mask);
}

mocktail::Status ShutdownPlatformBridges(jnivm::VM* vm) {
  if (vm == nullptr) {
    return mocktail::Status::Error(mocktail::StatusCode::kInvalidArgument,
                                   "platform bridge shutdown requires a VM");
  }
  vm->ClearAndroidWindowCallbacks();
  const mocktail::Status voice_status =
      mocktail::audio::ShutdownWebRtcJniAudioBridge(vm);
  const mocktail::Status playback_status =
      mocktail::audio::ShutdownFmodJniAudioBridge(vm);
  return !voice_status.ok() ? voice_status : playback_status;
}

void PromptFirstLaunchSignIn(
    const ProcessEnvironment& environment, const RuntimePaths& paths,
    mocktail::services::AuthService& auth_service,
    const std::shared_ptr<mocktail::services::HttpClient>& http_client,
    mocktail::runtime::AuthRuntimeComposition* composition) {
  // Runs only when no usable session exists. A fresh install has no cookie
  // file at all, which composes to kInvalidCredentials rather than kGuest, so
  // gating on kGuest (as the WebKit flow used to) made this unreachable.
  if (composition == nullptr ||
      composition->status == mocktail::runtime::AuthRuntimeStatus::kAuthenticated ||
      composition->status == mocktail::runtime::AuthRuntimeStatus::kGuest) {
    return;
  }
  if (environment.Get("MOCKTAIL_SKIP_FIRST_LAUNCH_LOGIN") == "1") {
    return;
  }

  std::cout << "\n======================================================\n"
            << "  First-Time Setup: Roblox Sign-In\n"
            << "======================================================\n"
            << "  Open https://www.roblox.com/login in your browser, sign in,\n"
            << "  then copy the .ROBLOSECURITY cookie value from your browser's\n"
            << "  developer tools. Paste it below and press Enter to continue,\n"
            << "  or just press Enter to play as guest.\n\n";
  std::cout << "  .ROBLOSECURITY cookie: " << std::flush;
  std::string cookie;
  std::getline(std::cin, cookie);
  // Strip surrounding quotes/whitespace and any ".ROBLOSECURITY=" prefix.
  auto is_space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r'; };
  while (!cookie.empty() && is_space(cookie.front())) cookie.erase(cookie.begin());
  while (!cookie.empty() && is_space(cookie.back())) cookie.pop_back();
  constexpr std::string_view kPrefix = ".ROBLOSECURITY=";
  if (cookie.compare(0, kPrefix.size(), kPrefix) == 0) {
    cookie.erase(0, kPrefix.size());
  }
  if (cookie.empty()) {
    // Guest mode only composes when the no-cookie LuaApp path is enabled, and
    // that same switch is what the engine runtime enables for itself later.
    // Turn it on now so the offer above is truthful instead of falling
    // through to the preflight failure.
    setenv("MOCKTAIL_ALLOW_NO_COOKIE_LUA_APP", "1", 1);
    mocktail::runtime::AuthRuntimeComposition guest_comp =
        mocktail::runtime::ComposeAuthRuntime(environment, paths,
                                              auth_service, http_client);
    if (guest_comp.status == mocktail::runtime::AuthRuntimeStatus::kGuest) {
      *composition = std::move(guest_comp);
      std::cout << "  [auth] continuing as guest\n";
    } else {
      std::cerr << "  [auth] guest session unavailable: " << guest_comp.error
                << '\n';
    }
    return;
  }
  if (mocktail::runtime::PersistRobloxCookie(paths.cookie_file(), cookie)) {
    mocktail::runtime::AuthRuntimeComposition new_comp =
        mocktail::runtime::ComposeAuthRuntime(environment, paths,
                                              auth_service, http_client);
    if (new_comp.status ==
        mocktail::runtime::AuthRuntimeStatus::kAuthenticated) {
      *composition = std::move(new_comp);
      std::cout << "  [auth] cookie saved; launching authenticated session\n";
    } else {
      std::cerr << "  [auth] cookie could not be verified; launching as guest\n";
    }
  } else {
    std::cerr << "  [auth] could not save cookie; launching as guest\n";
  }
}

void ConfigureHostDriverEnvironment() {
  auto set_if_unset = [](const char* name, const char* value) {
    if (std::getenv(name) == nullptr) {
      setenv(name, value, 1);
    }
  };
  // Bypass X11 compositor redirection to eliminate presentation latency on X11/XWayland.
  set_if_unset("SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR", "1");
  // Allow Adaptive Sync / Variable Refresh Rate (G-Sync/FreeSync) where supported.
  set_if_unset("__GL_VRR_ALLOWED", "1");
  // Expand shader disk cache to 2GB so compiled Vulkan/GL shaders are preserved across runs.
  set_if_unset("__GL_SHADER_DISK_CACHE", "1");
  set_if_unset("__GL_SHADER_DISK_CACHE_SIZE", "2147483648");
  // Avoid busy-spin vblank waits on NVIDIA Linux driver.
  set_if_unset("__GL_YIELD", "USLEEP");
  set_if_unset("__GL_THREADED_OPTIMIZATIONS", "1");
}

bool IsRunning(const CommandLineOptions& options) {
  return options.mode == CommandMode::kRun;
}

// ---------------------------------------------------------------------------
// Step: resolve the website launch request carried on the command line.
// ---------------------------------------------------------------------------
StepResult ResolveLaunchRequest(
    const CommandLineOptions& options,
    std::optional<RobloxExperienceLaunchRequest>* launch_request) {
  if (options.launch_request_json.empty()) {
    return StepResult::kContinue;
  }
  launch_request->emplace();
  const mocktail::Status status =
      mocktail::runtime::ParseRobloxExperienceLaunchJson(
          options.launch_request_json, &**launch_request);
  if (!status.ok()) {
    std::cerr << "[FATAL] Invalid controlled website launch request\n";
    return StepResult::kExitFailure;
  }
  return StepResult::kContinue;
}

// ---------------------------------------------------------------------------
// Step: take ownership of the launch, or hand it to the instance that already
// owns it. A website join aimed at a live client is forwarded over the broker
// socket and this process exits successfully without starting the engine.
// ---------------------------------------------------------------------------
StepResult AcquireLaunchOwnership(
    const ProcessEnvironment& environment, const RuntimePaths& paths,
    const CommandLineOptions& options,
    const std::optional<RobloxExperienceLaunchRequest>& launch_request,
    bool isolated_canary,
    const mocktail::runtime::ExternalLaunchBrokerOptions& broker_options,
    std::optional<mocktail::runtime::SingleInstanceLock>* instance_lock,
    mocktail::runtime::FailureSupportBundleGuard* support_bundle_guard) {
  if (!IsRunning(options)) {
    return StepResult::kContinue;
  }
  instance_lock->emplace(
      mocktail::runtime::SingleInstanceLock::AcquireForLaunch(environment, paths));
  if ((*instance_lock)->acquired()) {
    return StepResult::kContinue;
  }
  if (!isolated_canary && (*instance_lock)->already_running() &&
      launch_request.has_value()) {
    const mocktail::Status forward_status =
        mocktail::runtime::ExternalLaunchBroker::ForwardToOwner(
            broker_options, *launch_request);
    if (forward_status.ok()) {
      std::cout << "  [launch] sent website join to the running client\n";
      support_bundle_guard->SetExitCode(EXIT_SUCCESS);
      return StepResult::kExitSuccess;
    }
    std::cerr << "[FATAL] Cannot send website join to running client: "
              << forward_status.message() << '\n';
    (void)mocktail::runtime::ShowFailureDialog(
        environment,
        "Roblox is already running, but the requested experience could not be "
        "sent to it.");
  } else if ((*instance_lock)->already_running()) {
    std::cerr << "[FATAL] Roblox is already running for this user\n";
    (void)mocktail::runtime::ShowFailureDialog(
        environment, "An instance of Roblox is already running.");
  } else {
    std::cerr << "[FATAL] " << (*instance_lock)->error() << '\n';
    (void)mocktail::runtime::ShowFailureDialog(
        environment, "Roblox could not acquire its launch lock.");
  }
  return StepResult::kExitFailure;
}

// ---------------------------------------------------------------------------
// Step: materialize, load and validate the runtime configuration. Loading runs
// twice when a host system proxy is in play, because the proxy is itself a
// configuration input.
// ---------------------------------------------------------------------------
StepResult LoadRuntimeConfiguration(
    const ProcessEnvironment& environment, const RuntimePaths& paths,
    const CommandLineOptions& options,
    RuntimeConfigLoadResult* runtime_config, bool* created_config_file) {
  const mocktail::runtime::RuntimeConfigBootstrapResult config_bootstrap =
      mocktail::runtime::EnsureRuntimeConfigFile(paths.config_file());
  *created_config_file = config_bootstrap.created();
  if (!config_bootstrap) {
    std::cerr << "[FATAL] Cannot prepare " << paths.config_file() << ": "
              << config_bootstrap.error << '\n';
    if (IsRunning(options)) {
      (void)mocktail::runtime::ShowFailureDialog(
          environment, "Roblox could not prepare its configuration.");
    }
    return StepResult::kExitFailure;
  }
  *runtime_config = mocktail::runtime::LoadRuntimeConfig(
      environment, paths.config_file());
  if (!*runtime_config) {
    std::cerr << "[FATAL] Cannot load " << paths.config_file() << ": "
              << runtime_config->error << '\n';
    if (IsRunning(options)) {
      (void)mocktail::runtime::ShowFailureDialog(
          environment, "Roblox could not load its configuration.");
    }
    return StepResult::kExitFailure;
  }
  if (runtime_config->config.use_system_proxy()) {
    const mocktail::runtime::SystemProxyResult system_proxy =
        mocktail::runtime::ResolveSystemProxy();
    const std::string proxy_host = system_proxy.proxy.has_value()
                                       ? system_proxy.proxy->host
                                       : std::string();
    const std::string proxy_port =
        system_proxy.proxy.has_value()
            ? std::to_string(system_proxy.proxy->port)
            : std::string();
    const std::string proxy_scheme = system_proxy.proxy.has_value()
                                         ? system_proxy.proxy->scheme
                                         : std::string();
    if (!system_proxy ||
        setenv("MOCKTAIL_HTTP_PROXY_HOST", proxy_host.c_str(), 1) != 0 ||
        setenv("MOCKTAIL_HTTP_PROXY_PORT", proxy_port.c_str(), 1) != 0 ||
        setenv("MOCKTAIL_HTTP_PROXY_SCHEME", proxy_scheme.c_str(), 1) != 0) {
      std::cerr << "[FATAL] Cannot resolve host system proxy";
      if (!system_proxy.error.empty()) {
        std::cerr << ": " << system_proxy.error;
      }
      std::cerr << '\n';
      return StepResult::kExitFailure;
    }
    *runtime_config = mocktail::runtime::LoadRuntimeConfig(
        environment, paths.config_file());
    if (!*runtime_config) {
      std::cerr << "[FATAL] Cannot apply host system proxy: "
                << runtime_config->error << '\n';
      return StepResult::kExitFailure;
    }
  }
  if (IsRunning(options) &&
      runtime_config->config.has_unsafe_detached_thread_overrides()) {
    std::cerr << "[FATAL] Unsupported detached legacy thread overrides:\n";
    for (const std::string& name :
         runtime_config->config.unsafe_detached_thread_overrides()) {
      std::cerr << "  - " << name << '\n';
    }
    std::cerr << "  Supported runtime requires synchronous or owned worker "
                 "execution.\n";
    (void)mocktail::runtime::ShowFailureDialog(
        environment,
        "Roblox cannot start with unsupported legacy thread overrides "
        "enabled.");
    return StepResult::kExitFailure;
  }
  std::string error;
  if (IsRunning(options) &&
      !mocktail::runtime::ApplyGraphicsLaunchPolicy(runtime_config->config,
                                                     &error)) {
    std::cerr << "[FATAL] " << error << '\n';
    return StepResult::kExitFailure;
  }
  return StepResult::kContinue;
}

// ---------------------------------------------------------------------------
// Step: decide whether a memory ceiling applies, and whether the process has to
// re-exec itself into a cgroup scope to get one.
// ---------------------------------------------------------------------------
struct MemoryLimitPlan {
  bool enabled = false;
  std::uint64_t bytes = 0;
  mocktail::runtime::CgroupMemoryLimitResult cgroup;
};

MemoryLimitPlan PlanMemoryLimit(int argc, char* argv[],
                                const CommandLineOptions& options,
                                const RuntimeConfig& config,
                                const std::vector<std::string>& reexec_arguments) {
  MemoryLimitPlan plan;
  plan.enabled = IsRunning(options) && config.performance().memory_limit_enabled();
  if (!plan.enabled) {
    return plan;
  }
  plan.bytes = config.performance().memory_limit_bytes();
  plan.cgroup = mocktail::runtime::MaybeReexecWithCgroupMemoryLimit(
      argc, argv, plan.bytes, &reexec_arguments);
  return plan;
}

// ---------------------------------------------------------------------------
// Step: start the session log and print what the process resolved to.
// ---------------------------------------------------------------------------
void ReportStartupSummary(const ProcessEnvironment& environment,
                          const RuntimePaths& paths,
                          const CommandLineOptions& options,
                          const RuntimeConfig& config, bool created_config_file,
                          const MemoryLimitPlan& memory_plan,
                          mocktail::runtime::SessionLog* session_log,
                          std::chrono::system_clock::time_point process_started_at,
                          const std::string& process_launch_diagnostics) {
  if (IsRunning(options)) {
    *session_log = mocktail::runtime::SessionLog::Start(
        environment, paths, process_started_at);
    if (*session_log) {
      std::cout << session_log->Header(environment, paths,
                                       config.graphics_backend_name())
                << std::flush;
    } else if (session_log->attempted()) {
      std::cerr << "  [session] automatic logging unavailable: "
                << session_log->error() << '\n';
    }
    std::cout << process_launch_diagnostics << std::flush;
    mocktail::runtime::InstallCpuLimitDiagnostics();
    mocktail::runtime::LogProcessDiagnostics(
        mocktail::runtime::ProcessDiagnosticStage::kStartup);
  }
  if (created_config_file) {
    std::cout << "  [runtime] created first-run configuration: "
              << paths.config_file() << '\n';
  }
  if (IsRunning(options) && config.use_system_proxy()) {
    if (config.network_proxy().has_value()) {
      std::cout << "  [network] system proxy="
                << mocktail::runtime::BuildNetworkProxyUrl(
                       *config.network_proxy())
                << '\n';
    } else {
      std::cout << "  [network] system proxy=direct\n";
    }
  }
  if (memory_plan.enabled && memory_plan.cgroup.active()) {
    std::cout << "  [memory] hard process-tree limit active: "
              << config.performance().memory_limit_mb
              << " MiB RAM, swap disabled\n";
  }
  if (IsRunning(options)) {
    std::cout << "  [runtime] graphics backend="
              << config.graphics_backend_name()
              << (config.graphics_backend() ==
                          mocktail::runtime::GraphicsBackend::kSystem
                      ? " (EGL/OpenGL ES)"
                      : "")
              << '\n';
  }
}

// ---------------------------------------------------------------------------
// Step: ask libgamemode for a performance governor. libgamemode caches its
// D-Bus connection, so this must happen in the final game process.
// ---------------------------------------------------------------------------
mocktail::runtime::GameModeSession StartGameModeSession(
    const CommandLineOptions& options, const RuntimeConfig& config) {
  const mocktail::runtime::GameModePolicy policy =
      config.performance().game_mode;
  mocktail::runtime::GameModeSession session =
      IsRunning(options) ? mocktail::runtime::GameModeSession::Start(policy)
                         : mocktail::runtime::GameModeSession();
  switch (session.state()) {
    case mocktail::runtime::GameModeSessionState::kActive:
      std::cout << "  [gamemode] performance request active\n";
      break;
    case mocktail::runtime::GameModeSessionState::kAlreadyActive:
      std::cout << "  [gamemode] already active for this process\n";
      break;
    case mocktail::runtime::GameModeSessionState::kUnavailable:
    case mocktail::runtime::GameModeSessionState::kRequestFailed:
      if (policy == mocktail::runtime::GameModePolicy::kOn) {
        std::cerr << "  [gamemode] requested but unavailable: "
                  << session.detail()
                  << "; continuing without GameMode\n";
      } else {
        std::cout << "  [gamemode] unavailable; continuing normally\n";
      }
      break;
    case mocktail::runtime::GameModeSessionState::kDisabled:
      if (IsRunning(options)) {
        std::cout << "  [gamemode] disabled by runtime policy\n";
      }
      break;
    case mocktail::runtime::GameModeSessionState::kStopped:
    case mocktail::runtime::GameModeSessionState::kStopFailed:
      break;
  }
  return session;
}

// ---------------------------------------------------------------------------
// Step: bind every on-disk location the runtime will use. There is no managed
// payload: libroblox.so and assets/content default to this executable's own
// directory, and every writable path comes from the resolved RuntimePaths.
// ---------------------------------------------------------------------------
StepResult BindRuntimeStorage(const ProcessEnvironment& environment,
                              const RuntimePaths& paths,
                              const CommandLineOptions& options,
                              const RuntimeConfig& config,
                              std::filesystem::path* app_storage_file,
                              std::string* error) {
  const std::filesystem::path roblox_library = config.roblox_library_path();
  if (roblox_library.empty() ||
      !std::filesystem::is_regular_file(roblox_library)) {
    std::cerr << "[FATAL] No libroblox.so. Place it next to this executable, "
                 "or pass --roblox-lib <path>\n";
    return StepResult::kExitFailure;
  }
  // The guest ABI is fixed at build time, so a library for another
  // architecture can only fail somewhere deep in the loader. Say so here.
  const mocktail::runtime::GuestAbiReport abi =
      mocktail::runtime::InspectGuestLibraryAbi(roblox_library);
  if (abi.verdict != mocktail::runtime::GuestAbiVerdict::kOk) {
    std::cerr << "[FATAL] " << abi.message << '\n';
    return StepResult::kExitFailure;
  }
  // Everything downstream reads MOCKTAIL_ASSET_PATH, so the asset location is
  // resolved once here and normalised to the content root. An explicit
  // --assets wins over the environment: silently discarding the flag the user
  // just typed is worse than overriding a stale variable.
  std::filesystem::path asset_root;
  const std::string asset_env = environment.GetOr("MOCKTAIL_ASSET_PATH", "");
  if (!options.assets_path.empty()) {
    asset_root = options.assets_path;
  } else if (!asset_env.empty()) {
    asset_root = asset_env;
  } else {
    asset_root = mocktail::runtime::DefaultRobloxAssetPath();
    if (asset_root.empty()) {
      std::cerr << "[FATAL] No assets directory. Place it at "
                   "<exe dir>/assets/content, or pass --assets <path>\n";
      return StepResult::kExitFailure;
    }
  }
  const std::filesystem::path content_root =
      mocktail::runtime::NormalizeRobloxAssetPath(asset_root);
  if (!mocktail::runtime::LooksLikeRobloxContentDirectory(content_root)) {
    std::cerr << "[FATAL] Assets path does not contain Roblox content: "
              << content_root
              << "\n        Expected an existing directory holding the content "
                 "tree (one of\n        configs/, guac/, localization/, fonts/, "
                 "textures/).\n        Both <assets> and <assets/content> are "
                 "accepted.\n";
    return StepResult::kExitFailure;
  }
  if (setenv("MOCKTAIL_ASSET_PATH", content_root.c_str(), 1) != 0) {
    std::cerr << "[FATAL] Cannot apply the assets path\n";
    return StepResult::kExitFailure;
  }
  std::cout << "  [runtime] assets content root: " << content_root << '\n';
  if (!mocktail::runtime::ExportRuntimePathEnvironment(paths, error)) {
    std::cerr << "[FATAL] " << *error << '\n';
    return StepResult::kExitFailure;
  }
  if (!IsRunning(options)) {
    return StepResult::kContinue;
  }
  const mocktail::Status window_state_status =
      mocktail::window::ConfigureWindowStatePersistence(
          paths.state_root() / "window-state.json");
  if (!window_state_status.ok()) {
    std::cerr << "[FATAL] Cannot configure window state persistence: "
              << window_state_status.message() << '\n';
    return StepResult::kExitFailure;
  }
  // Identity and policy caches are keyed by device profile, so switching
  // profiles has to invalidate them before the engine reads any.
  const mocktail::runtime::InputCapabilityConfig& input =
      config.input_capabilities();
  const std::string platform_profile =
      mocktail::runtime::BuildPlatformProfileRevision(
          config.device_profile().cache_key, input.touch_enabled,
          input.mouse_enabled, input.keyboard_enabled);
  const mocktail::runtime::PlatformCacheMigrationResult cache_migration =
      mocktail::runtime::MigratePlatformProfileCaches(environment, paths,
                                                      platform_profile);
  if (!cache_migration) {
    std::cerr << "[FATAL] Platform cache migration failed: "
              << cache_migration.error << '\n';
    return StepResult::kExitFailure;
  }
  *app_storage_file = cache_migration.app_storage_file;
  setenv("MOCKTAIL_APP_STORAGE_FILE_INTERNAL", app_storage_file->c_str(), 1);
  if (cache_migration.transitioned) {
    std::cout << "  [runtime] platform cache profile transitioned; "
                 "refreshable identity and policy caches invalidated="
              << (cache_migration.app_storage_updated ? 1 : 0) << '\n';
  }
  return StepResult::kContinue;
}

// ---------------------------------------------------------------------------
// Step: turn the runtime's own policy into the client-settings and fast-flag
// documents the engine reads. Order matters: frame rate and performance are
// merged first, microphone permission on top, then crash-report policy.
// ---------------------------------------------------------------------------
StepResult ApplyClientSettingsPolicy(const ProcessEnvironment& environment,
                                     const RuntimePaths& paths,
                                     const CommandLineOptions& options,
                                     const RuntimeConfig& config,
                                     std::string* error) {
  if (!mocktail::runtime::ExportRuntimeConfigEnvironment(config, error)) {
    std::cerr << *error << '\n';
    return StepResult::kExitFailure;
  }
  if (!IsRunning(options)) {
    return StepResult::kContinue;
  }
  if (!environment.HasNonEmpty("MOCKTAIL_NATIVE_SET_DEFAULT_POLICY_FILE") &&
      setenv("MOCKTAIL_NATIVE_SET_DEFAULT_POLICY_FILE", "1", 1) != 0) {
    std::cerr << "[FATAL] Cannot enable Roblox default app-policy file\n";
    return StepResult::kExitFailure;
  }
  std::cout << "  [runtime] device profile=" << config.device_profile().name
            << " class="
            << mocktail::runtime::DeviceClassName(
                   config.device_profile().device_class)
            << " model=\"" << config.device_profile().display_name << "\"\n";

  const std::filesystem::path fflags_path = paths.config_root() / "fflags.json";
  const auto fflags = mocktail::services::LoadAndMergeFflagsFile(
      fflags_path,
      environment.GetOr("MOCKTAIL_CLIENT_SETTINGS_OVERRIDES_JSON", "{}"));
  if (!fflags.error.empty()) {
    std::cerr << "[FATAL] Cannot load FFlag overrides from " << fflags_path
              << ": " << fflags.error << '\n';
    return StepResult::kExitFailure;
  }
  if (fflags.loaded) {
    std::cout << "  [runtime] loaded " << fflags.count
              << " FFlag overrides from " << fflags_path << '\n';
  }
  std::string client_settings_overrides;
  if (!mocktail::runtime::MergeRuntimeClientSettingsOverrides(
          config.frame_rate(), config.performance(), fflags.json,
          &client_settings_overrides, error)) {
    std::cerr << "[FATAL] Cannot apply runtime client-settings policy: "
              << *error << '\n';
    return StepResult::kExitFailure;
  }
  std::string audio_capture_overrides;
  if (!mocktail::runtime::MergeAudioCaptureClientSettingsOverrides(
          config.microphone_enabled(), client_settings_overrides,
          &audio_capture_overrides, error)) {
    std::cerr << "[FATAL] Cannot apply microphone permission policy: "
              << *error << '\n';
    return StepResult::kExitFailure;
  }
  if (setenv("MOCKTAIL_CLIENT_SETTINGS_OVERRIDES_JSON",
             audio_capture_overrides.c_str(), 1) != 0) {
    std::cerr << "[FATAL] Cannot export runtime client-settings policy\n";
    return StepResult::kExitFailure;
  }
  std::cout << "  [runtime] HttpClient compatibility mode enabled: "
               "generic LuaApp retry and RuntimeMutexRv disabled\n";

  std::string fast_flags_overrides;
  if (!mocktail::runtime::MergeCrashReportFastFlagsOverrides(
          environment.GetOr("MOCKTAIL_FAST_FLAGS_JSON", "{}"),
          &fast_flags_overrides, error)) {
    std::cerr << "[FATAL] Cannot apply crash-report fast-flags policy: "
              << *error << '\n';
    return StepResult::kExitFailure;
  }
  if (setenv("MOCKTAIL_FAST_FLAGS_JSON", fast_flags_overrides.c_str(), 1) != 0) {
    std::cerr << "[FATAL] Cannot export crash-report fast-flags policy\n";
    return StepResult::kExitFailure;
  }
  std::cout << "  [runtime] Roblox crash-report uploads disabled by "
               "mandatory policy\n";

  const auto physics_worker_mode = config.performance().physics_worker_mode;
  const int physical_core_count = config.performance().physical_core_count;
  const int engine_worker_count =
      physics_worker_mode == mocktail::runtime::PhysicsWorkerMode::kThroughput
          ? mocktail::runtime::CalculateThroughputWorkerCount(
                physical_core_count)
          : physical_core_count;
  if (physics_worker_mode == mocktail::runtime::PhysicsWorkerMode::kLatency) {
    std::cout << "  [runtime] physics worker mode=latency: Roblox-managed "
                 "worker pools, midphase batch=128\n";
  } else if (physics_worker_mode ==
             mocktail::runtime::PhysicsWorkerMode::kThroughput) {
    std::cout << "  [runtime] physics worker mode=throughput: "
              << engine_worker_count << " scheduler workers, async minimum="
              << std::min(engine_worker_count, 3) << ", midphase batch=128\n";
  }
  if ((config.performance().multithreaded_rendering ||
       physics_worker_mode == mocktail::runtime::PhysicsWorkerMode::kThroughput) &&
      physics_worker_mode != mocktail::runtime::PhysicsWorkerMode::kLatency) {
    std::cout << "  [runtime] full multithreaded engine queues enabled: "
              << engine_worker_count << " scheduler workers, "
              << std::max(1, engine_worker_count / 2) << " occlusion workers\n";
  } else if (config.performance().multithreaded_rendering &&
             physics_worker_mode ==
                 mocktail::runtime::PhysicsWorkerMode::kLatency) {
    std::cout << "  [runtime] latency mode leaves scheduler/render worker "
                 "counts under Roblox client-settings control\n";
  }
  if (config.frame_rate().mode ==
      mocktail::runtime::FrameRateLimitMode::kUnlimited) {
    std::cout << "  [runtime] frame-rate mode=unlimited scheduler_target="
              << mocktail::runtime::kMaximumSupportedRobloxSchedulerFps
              << " vsync=" << config.vsync_mode() << '\n';
  }
  return StepResult::kContinue;
}

// ---------------------------------------------------------------------------
// Step: resolve who the guest is and install every bridge the engine expects
// before native bootstrap: typed Roblox identity, desktop app-policy, window
// callbacks, and the FMOD/WebRTC audio bridges.
// ---------------------------------------------------------------------------
StepResult BuildRuntimeDependencies(
    const ProcessEnvironment& environment, const RuntimePaths& paths,
    const CommandLineOptions& options, const RuntimeConfig& config,
    const std::filesystem::path& app_storage_file, bool has_launch_request,
    std::string* error, mocktail::legacy::RuntimeDependencies* dependencies) {
  if (!IsRunning(options)) {
    return StepResult::kContinue;
  }
  auto http_client = std::make_shared<mocktail::services::CurlHttpClient>();
  mocktail::services::AuthService auth_service(*http_client);
  mocktail::runtime::AuthRuntimeComposition composition =
      mocktail::runtime::ComposeAuthRuntime(environment, paths, auth_service,
                                            http_client);
  // A website launch already carries its destination, so it must not be
  // interrupted by an interactive sign-in prompt.
  if (!has_launch_request &&
      options.window_mode != mocktail::runtime::WindowMode::kHeadless) {
    PromptFirstLaunchSignIn(environment, paths, auth_service, http_client,
                            &composition);
  }
  if (!composition) {
    std::cerr << "[FATAL] Typed Roblox authentication preflight failed: "
              << composition.error;
    if (composition.http_status != 0) {
      std::cerr << " (HTTP " << composition.http_status << ')';
    }
    std::cerr << '\n';
    return StepResult::kExitFailure;
  }
  const auto& device_profile = config.device_profile();
  composition.jni_vm->SetPlatformIdentity(jnivm::PlatformIdentity{
      config.input_capabilities().touch_enabled,
      config.input_capabilities().mouse_enabled,
      config.input_capabilities().keyboard_enabled,
      device_profile.pc_hardware,
      std::string(device_profile.platform_name),
      std::string(device_profile.display_name),
      std::string(device_profile.manufacturer),
      std::string(device_profile.model),
      std::string(device_profile.brand),
      std::string(device_profile.device_code),
      std::string(device_profile.device_sku),
      std::string(device_profile.soc_model),
  });
  if (composition.rejected_credential_retired) {
    constexpr std::string_view kSignedOutMessage =
        "Your saved Roblox session is no longer valid. Sign in again to "
        "continue.";
    std::cout << "  [auth] saved Roblox session expired; continuing with "
                 "native sign-in\n";
    (void)mocktail::runtime::ShowWarningDialog(environment, kSignedOutMessage);
  }
  const bool authenticated =
      composition.status == mocktail::runtime::AuthRuntimeStatus::kAuthenticated;
  if (authenticated) {
    if (setenv("MOCKTAIL_NATIVE_SET_USER_ID", "1", 1) != 0) {
      std::cerr << "[FATAL] Cannot enable authenticated NativeSettings "
                   "identity\n";
      return StepResult::kExitFailure;
    }
    std::cout << "  [auth] typed Roblox identity resolved for production VM\n";
  } else {
    std::cout << "  [auth] explicit guest identity selected for production VM\n";
  }
  if (config.desktop_playability()) {
    std::filesystem::path assets = paths.DefaultAssetPath();
    if (environment.HasNonEmpty("MOCKTAIL_ASSET_PATH")) {
      assets = environment.GetOr("MOCKTAIL_ASSET_PATH", "");
    }
    const std::filesystem::path default_app_policy =
        assets / "guac/defaultConfigs/GuacDefaultPolicy-GlobalDist.json";
    const std::int64_t app_policy_user_id =
        authenticated ? composition.account_identity.user_id : -1;
    const mocktail::runtime::DesktopAppPolicyResult desktop_policy =
        mocktail::runtime::ApplyDesktopAppPolicy(
            app_storage_file, default_app_policy, app_policy_user_id,
            config.theme_mode());
    if (!desktop_policy) {
      std::cerr << "[FATAL] Desktop Roblox app-policy failed: "
                << desktop_policy.error << '\n';
      return StepResult::kExitFailure;
    }
    std::string desktop_client_settings;
    if (!mocktail::runtime::MergeDesktopAppPolicyClientSettingsOverride(
            desktop_policy.policy_json,
            environment.GetOr("MOCKTAIL_CLIENT_SETTINGS_OVERRIDES_JSON", "{}"),
            &desktop_client_settings, error)) {
      std::cerr << "[FATAL] Cannot activate desktop Roblox app-policy: "
                << *error << '\n';
      return StepResult::kExitFailure;
    }
    if (setenv("MOCKTAIL_CLIENT_SETTINGS_OVERRIDES_JSON",
               desktop_client_settings.c_str(), 1) != 0) {
      std::cerr << "[FATAL] Cannot export desktop Roblox app-policy\n";
      return StepResult::kExitFailure;
    }
    std::cout << "  [runtime] desktop app-policy ready: normalized="
              << desktop_policy.normalized_policy_count
              << " updated=" << (desktop_policy.updated ? 1 : 0)
              << " runtime_override=1\n";
  }
  auto android_window_context = std::make_shared<AndroidWindowBridgeContext>();
  jnivm::AndroidWindowCallbacks android_window_callbacks;
  android_window_callbacks.set_flags = &QueueAndroidWindowFlags;
  composition.jni_vm->SetAndroidWindowCallbacks(std::move(android_window_context),
                                                android_window_callbacks);
  const mocktail::Status audio_status =
      mocktail::audio::InstallFmodJniAudioBridge(composition.jni_vm.get());
  if (!audio_status.ok()) {
    std::cerr << "[FATAL] Typed FMOD Java audio composition failed: "
              << audio_status.message() << '\n';
    return StepResult::kExitFailure;
  }
  const mocktail::Status voice_status =
      mocktail::audio::InstallWebRtcJniAudioBridge(composition.jni_vm.get());
  if (!voice_status.ok()) {
    (void)mocktail::audio::ShutdownFmodJniAudioBridge(composition.jni_vm.get());
    std::cerr << "[FATAL] WebRTC voice audio composition failed: "
              << voice_status.message() << '\n';
    return StepResult::kExitFailure;
  }
  *dependencies = mocktail::legacy::RuntimeDependencies(
      std::move(composition), &ShutdownPlatformBridges);
  return StepResult::kContinue;
}

// ---------------------------------------------------------------------------
// Step: start listening for website launches aimed at this instance. This runs
// only after preflight and bridge setup, so an ACK can never precede a known
// startup failure. Isolated canaries expose no endpoint.
// ---------------------------------------------------------------------------
StepResult StartLaunchBroker(const CommandLineOptions& options, bool isolated_canary,
                             std::optional<RobloxExperienceLaunchRequest>* launch_request,
                             ExternalLaunchBrokerScope* broker) {
  if (!IsRunning(options) || isolated_canary) {
    return StepResult::kContinue;
  }
  mocktail::runtime::ExternalLaunchBrokerOptions broker_options;
  mocktail::Status status =
      mocktail::runtime::ExternalLaunchBroker::StartOwnerAfterLockAcquired(
          broker_options, &broker->broker(),
          launch_request->has_value() ? &**launch_request : nullptr);
  if (status.ok()) {
    launch_request->reset();
    status = mocktail::runtime::InstallActiveExternalLaunchBroker(
        broker->broker());
  }
  if (!status.ok()) {
    std::cerr << "[FATAL] Cannot activate website launch bridge: "
              << status.message() << '\n';
    return StepResult::kExitFailure;
  }
  return StepResult::kContinue;
}

// ---------------------------------------------------------------------------
// Step: tear the pipeline down in reverse ownership order and pick the process
// exit status.
// ---------------------------------------------------------------------------
int FinishRuntime(const CommandLineOptions& options, int runtime_status,
                  mocktail::runtime::FailureDialogMonitor* failure_dialog,
                  mocktail::runtime::FailureSupportBundleGuard* support_bundle_guard,
                  mocktail::runtime::MemoryLimitWatchdog* memory_limit_watchdog,
                  mocktail::runtime::GameModeSession* game_mode_session,
                  ExternalLaunchBrokerScope* broker) {
  const mocktail::Status broker_shutdown_status = broker->Shutdown();
  if (!broker_shutdown_status.ok()) {
    std::cerr << "  [launch] website bridge shutdown failed: "
              << broker_shutdown_status.message() << '\n';
    runtime_status = EXIT_FAILURE;
  }
  if (runtime_status != EXIT_SUCCESS) {
    failure_dialog->SetMessage("Roblox stopped with error code " +
                               std::to_string(runtime_status) + ".");
  }
  support_bundle_guard->SetExitCode(runtime_status);
  memory_limit_watchdog->Stop();
  const mocktail::Status game_mode_stop_status = game_mode_session->Stop();
  if (!game_mode_stop_status.ok()) {
    std::cerr << "  [gamemode] release failed: "
              << game_mode_stop_status.message() << '\n';
  }
  if (runtime_status != EXIT_SUCCESS) {
    return runtime_status;
  }
  failure_dialog->MarkSuccessful();
  support_bundle_guard->Disarm();
  if (IsRunning(options)) {
    // Guest atexit handlers target workers that cannot be joined. Host
    // shutdown is already complete here, so do not re-enter guest teardown.
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(EXIT_SUCCESS);
  }
  return runtime_status;
}

}  // namespace

int main(int argc, char* argv[]) {
  ConfigureHostDriverEnvironment();
  const auto process_started_at = std::chrono::system_clock::now();

  mocktail::runtime::CommandLineParseResult command_line =
      mocktail::runtime::ParseCommandLine(argc, argv);
  if (!command_line) {
    std::cerr << command_line.error << "\n\n"
              << mocktail::runtime::CommandLineUsage(
                     command_line.options.program_name);
    return EXIT_FAILURE;
  }
  const CommandLineOptions& options = command_line.options;
  if (options.mode == CommandMode::kHelp) {
    std::cout << mocktail::runtime::CommandLineUsage(options.program_name);
    return EXIT_SUCCESS;
  }
  // Normalize inherited process state before bootstrap or helpers can create
  // threads. Keep research/canary resource limits under their caller's control.
  const std::string process_launch_diagnostics =
      IsRunning(options)
          ? mocktail::runtime::ApplyInteractiveProcessLaunchPolicy()
          : std::string{};
  std::string error;
  if (!mocktail::runtime::ApplyCommandLineEnvironment(options, &error)) {
    std::cerr << error << '\n';
    return EXIT_FAILURE;
  }

  const ProcessEnvironment environment;
  const RuntimePaths paths = RuntimePaths::FromEnvironment(environment);
  const bool isolated_canary = environment.GetOr("MOCKTAIL_ISOLATED_CANARY", "0") == "1";

  std::optional<RobloxExperienceLaunchRequest> launch_request;
  if (ResolveLaunchRequest(options, &launch_request) == StepResult::kExitFailure) {
    return EXIT_FAILURE;
  }
  if (isolated_canary && launch_request.has_value()) {
    std::cerr << "[FATAL] Website launches are unavailable inside isolated "
                 "canary\n";
    return EXIT_FAILURE;
  }

  // Built before the command line is scrubbed, then erased: these arguments
  // are the input to a possible cgroup re-exec and must not outlive it.
  std::vector<std::string> cgroup_reexec_arguments;
  if (!mocktail::runtime::BuildCommandLineReexecArguments(
          options, argc, argv, &cgroup_reexec_arguments, &error)) {
    std::cerr << "[FATAL] " << error << '\n';
    return EXIT_FAILURE;
  }
  mocktail::runtime::ScrubCommandLineLaunchArguments(&command_line.options, argc, argv);

  // Lifetime objects. Declared here so their destructors run in reverse order
  // after the engine has fully shut down.
  mocktail::runtime::SessionLog session_log;
  mocktail::runtime::FailureSupportBundleGuard support_bundle_guard(
      environment, paths, IsRunning(options));
  std::optional<mocktail::runtime::SingleInstanceLock> instance_lock;
  ExternalLaunchBrokerScope launch_broker;
  mocktail::runtime::FailureDialogMonitor failure_dialog;
  mocktail::runtime::MemoryLimitWatchdog memory_limit_watchdog;
  mocktail::legacy::RuntimeDependencies dependencies;

  const mocktail::runtime::ExternalLaunchBrokerOptions broker_options;
  const StepResult ownership =
      AcquireLaunchOwnership(environment, paths, options, launch_request,
                             isolated_canary, broker_options, &instance_lock,
                             &support_bundle_guard);
  if (ownership != StepResult::kContinue) {
    return ownership == StepResult::kExitSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  RuntimeConfigLoadResult runtime_config;
  bool created_config_file = false;
  if (LoadRuntimeConfiguration(environment, paths, options, &runtime_config,
                               &created_config_file) == StepResult::kExitFailure) {
    return EXIT_FAILURE;
  }

  const MemoryLimitPlan memory_plan =
      PlanMemoryLimit(argc, argv, options, runtime_config.config,
                      cgroup_reexec_arguments);
  ReportStartupSummary(environment, paths, options, runtime_config.config,
                       created_config_file, memory_plan, &session_log,
                       process_started_at, process_launch_diagnostics);

  mocktail::runtime::GameModeSession game_mode_session =
      StartGameModeSession(options, runtime_config.config);

  if (IsRunning(options)) {
    failure_dialog = mocktail::runtime::FailureDialogMonitor::Start(
        environment,
        "Roblox could not start because of an internal error.");
  }
  if (memory_plan.enabled) {
    if (!memory_limit_watchdog.Start(memory_plan.bytes, &error)) {
      failure_dialog.SetMessage(
          "Roblox could not start its memory safety monitor.");
      std::cerr << "[FATAL] " << error << '\n';
      return EXIT_FAILURE;
    }
    std::cout << "  [memory] RSS+swap watchdog active at "
              << runtime_config.config.performance().memory_limit_mb
              << " MiB\n";
  }
  SecureEraseArguments(&cgroup_reexec_arguments);

  std::filesystem::path app_storage_file;
  if (BindRuntimeStorage(environment, paths, options, runtime_config.config,
                         &app_storage_file, &error) ==
      StepResult::kExitFailure) {
    return EXIT_FAILURE;
  }
  if (ApplyClientSettingsPolicy(environment, paths, options, runtime_config.config,
                                &error) == StepResult::kExitFailure) {
    return EXIT_FAILURE;
  }

  const libc_shim::HostCaBundleResolution ca_bundle =
      libc_shim::ResolveHostCaBundle();
  if (ca_bundle.status == libc_shim::HostCaBundleStatus::kInvalidOverride) {
    std::cerr << "Invalid MOCKTAIL_CA_BUNDLE: expected a readable, non-empty "
                 "absolute regular file\n";
    return 2;
  }

  if (BuildRuntimeDependencies(environment, paths, options, runtime_config.config,
                               app_storage_file, launch_request.has_value(),
                               &error, &dependencies) ==
      StepResult::kExitFailure) {
    return EXIT_FAILURE;
  }
  if (StartLaunchBroker(options, isolated_canary, &launch_request, &launch_broker) ==
      StepResult::kExitFailure) {
    return EXIT_FAILURE;
  }

  failure_dialog.SetMessage(
      "Roblox closed unexpectedly because of an internal error.");
  if (IsRunning(options)) {
    mocktail::runtime::LogProcessDiagnostics(
        mocktail::runtime::ProcessDiagnosticStage::kNativeRuntime);
  }
  const int runtime_status =
      mocktail::legacy::Run(command_line.options, std::move(dependencies));
  // The engine has had its chance at the web-login ticket; it is a bearer
  // value, so it does not outlive native startup.
  mocktail::runtime::ScrubEngineLaunchUri(&command_line.options);
  if (IsRunning(options)) {
    mocktail::runtime::LogProcessDiagnostics(
        mocktail::runtime::ProcessDiagnosticStage::kShutdown);
    std::cerr << "[diagnostic] native runtime exit_status=" << runtime_status
              << '\n';
  }
  return FinishRuntime(options, runtime_status, &failure_dialog,
                       &support_bundle_guard, &memory_limit_watchdog,
                       &game_mode_session, &launch_broker);
}