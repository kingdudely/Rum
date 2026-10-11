#ifndef ROBLOX_PLATFORM_SDL_APPLICATION_METADATA_H_
#define ROBLOX_PLATFORM_SDL_APPLICATION_METADATA_H_

#include "roblox/status.h"

namespace roblox {
namespace platform {

inline constexpr char kRobloxApplicationName[] = "Roblox";
inline constexpr char kRobloxApplicationIdentifier[] =
    "space.bigrat.roblox";

// Configures the stable compositor identity before SDL initializes. The
// identifier matches the installed desktop file and icon name so Wayland and
// X11 compositors can group the runtime window with its launcher.
Status ConfigureSdlApplicationMetadata();

}  // namespace platform
}  // namespace roblox

#endif  // ROBLOX_PLATFORM_SDL_APPLICATION_METADATA_H_
