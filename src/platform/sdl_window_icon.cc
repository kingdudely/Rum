#include "mocktail/platform/sdl_window_icon.h"

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace mocktail {
namespace platform {

Status ApplySdlWindowIcon(SDL_Window* window) {
  (void)window;
  // Window icon is cosmetic; SDL3 3.4 PNG loading is not available on all
  // toolchains, so no icon is set.
  return Status::Ok();
}

}  // namespace platform
}  // namespace mocktail
