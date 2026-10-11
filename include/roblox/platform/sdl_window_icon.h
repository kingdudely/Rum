#ifndef ROBLOX_PLATFORM_SDL_WINDOW_ICON_H_
#define ROBLOX_PLATFORM_SDL_WINDOW_ICON_H_

#include "roblox/status.h"

struct SDL_Window;

namespace roblox {
namespace platform {

// Applies the embedded desktop artwork to an SDL window. X11 compositors use
// this to populate _NET_WM_ICON; Wayland compositors use the desktop identity.
Status ApplySdlWindowIcon(SDL_Window* window);

}  // namespace platform
}  // namespace roblox

#endif  // ROBLOX_PLATFORM_SDL_WINDOW_ICON_H_
