#ifndef MOCKTAIL_RUNTIME_ROBLOX_EXPERIENCE_COMPOSITION_H_
#define MOCKTAIL_RUNTIME_ROBLOX_EXPERIENCE_COMPOSITION_H_

#include <jni.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "mocktail/status.h"
#include "runtime/owned_pthread.h"
#include "runtime/roblox_call_protocol_bridge.h"
#include "runtime/roblox_experience_launch_bridge.h"
#include "runtime/roblox_experience_presence.h"
#include "runtime/roblox_fresh_game_launch_controller.h"
#include "runtime/roblox_permissions_bridge.h"

namespace jnivm {
class VM;
}  // namespace jnivm

namespace mocktail {
namespace runtime {

// SystemTheme.systemThemeUpdated rides the shared MessageBus and needs only
// the message-id lookup plus raw publish. It gets its own tiny symbol set so
// theme publication does not keep the WebViewProtocol resolver alive.
struct RobloxSystemThemeSymbols {
  using GetMessageIdFn = jstring (*)(JNIEnv *, jclass, jstring, jstring);
  using PublishRawFn = void (*)(JNIEnv *, jobject, jstring, jstring);

  GetMessageIdFn get_message_id = nullptr;
  PublishRawFn publish_raw = nullptr;

  bool complete() const {
    return get_message_id != nullptr && publish_raw != nullptr;
  }
};

// Pseudo-JVM integration needed to materialize the APK RawCallback interface.
// The composition obtains MessageBus.f() itself through its exact Java API.
struct RobloxExperienceJniFactory {
  void* context = nullptr;
  jobject (*create_raw_callback)(void* context,
                                 std::shared_ptr<void> callback_context,
                                 void (*run)(void*, JNIEnv*,
                                             jstring)) = nullptr;
  void (*clear_raw_callback)(void* context, jobject callback) = nullptr;
  bool (*set_platform_web_callbacks)(
      void* context, std::shared_ptr<void> callback_context,
      void (*on_data_model_notification)(void*, JNIEnv*, jstring, jstring),
      void (*on_app_bridge_notification)(void*, JNIEnv*, jstring, jstring),
      void (*on_native_overlay)(void*, JNIEnv*, jstring, jstring),
      void (*on_open_web_activity)(void*, JNIEnv*, jstring, jstring),
      void (*on_sync_cookies)(void*, JNIEnv*, jstring),
      void (*on_set_cookie)(void*, JNIEnv*, jstring, jstring)) = nullptr;
  void (*clear_platform_web_callbacks)(void* context) = nullptr;
  jobject (*create_async_request_handler)(
      void* context, std::shared_ptr<void> callback_context,
      void (*run)(void*, JNIEnv*, jstring, jstring)) = nullptr;
  void (*clear_async_request_handler)(void* context, jobject handler) = nullptr;

  bool complete() const {
    return context != nullptr && create_raw_callback != nullptr &&
           clear_raw_callback != nullptr &&
           set_platform_web_callbacks != nullptr &&
           clear_platform_web_callbacks != nullptr &&
           create_async_request_handler != nullptr &&
           clear_async_request_handler != nullptr;
  }
};

// Immutable values captured when LuaApp becomes ready. No Stage 6 local JNI
// reference is accepted here: the owner builds a fresh object graph and keeps
// its roots as global references until process-level shutdown.
struct RobloxLuaAppExperienceReadiness {
  GameSessionPrincipal principal;
  GameSurface surface;
  std::string username;
  bool is_under_13 = false;
  int32_t join_request_type = -1;

  bool complete() const;
};

// Supplies the current host surface to a composition that becomes
// authenticated after LuaApp startup. Production defaults to the SDL window
// snapshot; tests may inject an equivalent source.
struct RobloxExperienceSurfaceProvider {
  void* context = nullptr;
  GameSurface (*snapshot)(void* context) = nullptr;

  bool valid() const {
    return snapshot != nullptr;
  }
};

// Production composition for the APK platform protocols and ExperienceProtocol
// paths. InitializePlatformProtocols retains MessageBus.f() and binds the
// PermissionsProtocol observers before native bootstrap. OnLuaAppReady then
// adds the game JNI roots and ExperienceProtocol subscription. Every owned
// JSON launch request is dispatched to a fresh UGCGame controller without
// environment-derived fields.
class RobloxExperienceComposition final {
 public:
  RobloxExperienceComposition(
      JniEnvironmentProvider environment,
      RobloxExperienceMessageBusSymbols message_bus_symbols,
      RobloxSystemThemeSymbols system_theme_symbols,
      RobloxPermissionsMessageBusSymbols permissions_symbols,
      RobloxGameSessionSymbols game_symbols,
      RobloxExperienceJniFactory jni_factory,
      RobloxFreshLaunchPresentBoundary present_boundary,
      RobloxGameSurfaceJniConfig surface_config = {},
      RobloxExperienceSurfaceProvider surface_provider = {},
      RobloxExperiencePresenceObserver presence_observer = {},
      bool microphone_enabled = false);
  ~RobloxExperienceComposition();

  RobloxExperienceComposition(const RobloxExperienceComposition&) = delete;
  RobloxExperienceComposition& operator=(const RobloxExperienceComposition&) =
      delete;

  Status InitializePlatformProtocols();
  Status OnLuaAppReady(RobloxLuaAppExperienceReadiness readiness);
  // Callback-safe notification from the native app lifecycle. The callback
  // never invokes JNI or waits for an in-flight launch worker.
  void NotifyLuaAppDidReturn();
  Status DrainPlatformEvents();
  Status DrainLaunchRequests();
  Status LeaveGame();
  GameSessionUpdateResult SurfaceCreated(uint64_t generation);
  GameSessionUpdateResult SurfaceChanged(GameSurface surface);
  GameSessionUpdateResult SurfaceDestroyed(uint64_t generation);
  Status Shutdown();

  bool subscribed() const;
  GameSessionSnapshot Snapshot() const;

 private:
  struct GlobalObjects;
  struct LaunchTask;
  struct LifecycleTarget;

  static Status DispatchLaunch(void* context,
                               const RobloxExperienceLaunchRequest& request);
  Status DrainExternalLaunchRequests();
  static void GamePresented(void* context, uint64_t frame_serial);
  static void* RunLaunchWorker(void* context);
  Status Dispatch(const RobloxExperienceLaunchRequest& request);
  Status PromoteAuthenticatedSession();
  static void NotifyLateLuaAppDidReturn(void* context);
  Status RefreshLateSurface();
  void PublishPresentedPresence(uint64_t frame_serial);
  void RunActiveLaunch();
  Status BuildPlatformGlobalObjects();
  Status BuildLuaAppGlobalObjects();
  Status BuildLaunchObjects(const GameSurface& surface, jobject* surface_object,
                            jobject* platform_params);
  Status BuildLuaAppStartParams(
      JNIEnv* env, const RobloxLuaAppExperienceReadiness& readiness,
      jobject surface_object, jobject platform_params, jobject activity,
      jobject* start_app_params);
  Status RebindLuaAppSurface(const GameSurface& surface);
  Status RestartLuaAppSurface(const GameSurface& surface);
  Status ReleaseLuaAppGlobalObjects();
  Status ReleaseGlobalObjects();
  void NotifyPresence(RobloxExperiencePresencePhase phase,
                      const RobloxExperienceLaunchRequest* request) const;

  const JniEnvironmentProvider environment_;
  const RobloxExperienceMessageBusSymbols message_bus_symbols_;
  const RobloxSystemThemeSymbols system_theme_symbols_;
  const RobloxPermissionsMessageBusSymbols permissions_symbols_;
  const bool microphone_enabled_;
  const RobloxGameSessionSymbols game_symbols_;
  const RobloxExperienceJniFactory jni_factory_;
  const RobloxFreshLaunchPresentBoundary present_boundary_;
  const RobloxGameSurfaceJniConfig surface_config_;
  const bool consumes_window_surface_events_;
  const RobloxExperienceSurfaceProvider surface_provider_;
  const RobloxExperiencePresenceObserver presence_observer_;

  mutable std::mutex mutex_;
  std::mutex surface_operation_mutex_;
  RobloxLuaAppExperienceReadiness readiness_;
  std::unique_ptr<GlobalObjects> objects_;
  std::shared_ptr<RobloxFreshGameLaunchController> controller_;
  std::unique_ptr<RobloxExperienceLaunchBridge> bridge_;
  std::unique_ptr<RobloxPermissionsBridge> permissions_bridge_;
  std::unique_ptr<RobloxCallProtocolBridge> call_protocol_bridge_;
  std::shared_ptr<LifecycleTarget> lifecycle_target_;
  std::deque<RobloxExperienceLaunchRequest> pending_launch_requests_;
  std::unique_ptr<LaunchTask> active_launch_;
  std::optional<RobloxExperienceLaunchRequest> presence_request_;
  std::string active_game_canonical_json_;
  OwnedPthread launch_worker_;
  uint64_t next_request_id_ = 1;
  bool platform_protocols_initialized_ = false;
  bool subscribed_ = false;
  bool launch_in_progress_ = false;
  bool game_active_ = false;
  bool playing_presence_published_ = false;
  bool lua_app_return_pending_ = false;
  bool controlled_switch_waiting_for_return_ = false;
  bool lua_app_surface_recreation_pending_ = false;
  bool late_surface_tracking_ = false;
  GameSurface late_surface_snapshot_;
  jnivm::VM* late_lifecycle_vm_ = nullptr;
};

}  // namespace runtime
}  // namespace mocktail

#endif  // MOCKTAIL_RUNTIME_ROBLOX_EXPERIENCE_COMPOSITION_H_
