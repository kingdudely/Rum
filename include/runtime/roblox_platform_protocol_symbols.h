#ifndef MOCKTAIL_RUNTIME_ROBLOX_PLATFORM_PROTOCOL_SYMBOLS_H_
#define MOCKTAIL_RUNTIME_ROBLOX_PLATFORM_PROTOCOL_SYMBOLS_H_

#include "runtime/roblox_experience_composition.h"
#include "runtime/roblox_message_bus_request_bridge.h"
#include "runtime/roblox_permissions_bridge.h"

namespace mocktail {
namespace runtime {

// Exported platform-protocol entrypoints resolved as one capability. The
// WebView and BrowserService resolvers are gone: those protocols need a real
// Android WebView widget, and the engine's native WebLoginProtocol redeems
// `.ROBLOSECURITY` on its own once CookieProtocol is bound. What remains is
// the shared MessageBus plumbing the surviving bridges agree on.
struct RobloxPlatformProtocolSymbols {
  RobloxSystemThemeSymbols system_theme;
  RobloxPermissionsMessageBusSymbols permissions;

  bool complete() const {
    return system_theme.complete() && permissions.complete();
  }
};

RobloxPlatformProtocolSymbols ResolveRobloxPlatformProtocolSymbols(
    void* roblox_library, DeleteMessageBusConnectionFn delete_connection);

}  // namespace runtime
}  // namespace mocktail

#endif  // MOCKTAIL_RUNTIME_ROBLOX_PLATFORM_PROTOCOL_SYMBOLS_H_