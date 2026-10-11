#ifndef ROBLOX_PLATFORM_SDL_PLATFORM_RUNTIME_H_
#define ROBLOX_PLATFORM_SDL_PLATFORM_RUNTIME_H_

#include <memory>

#include "roblox/platform/platform_runtime.h"

namespace roblox {
namespace platform {

// Creates an SDL3-backed runtime without exposing SDL types to consumers.
std::unique_ptr<PlatformRuntime> CreateSdlPlatformRuntime();

}  // namespace platform
}  // namespace roblox

#endif  // ROBLOX_PLATFORM_SDL_PLATFORM_RUNTIME_H_
