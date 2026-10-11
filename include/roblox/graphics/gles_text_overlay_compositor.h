#ifndef ROBLOX_GRAPHICS_GLES_TEXT_OVERLAY_COMPOSITOR_H_
#define ROBLOX_GRAPHICS_GLES_TEXT_OVERLAY_COMPOSITOR_H_

#include <memory>

#include "roblox/graphics/text_overlay_frame.h"

struct SDL_Window;

namespace roblox::graphics {

struct GlesTextOverlaySource {
  bool (*may_present)() = nullptr;
  bool (*query)(RobloxTextOverlayFrameInfo *) = nullptr;
  bool (*copy)(std::uint64_t, void *, std::size_t) = nullptr;
};

// Composites the host text editor before SDL swaps the game's GLES backbuffer.
// Draw and destruction use the owning GL context when it is current. If that
// context is no longer current at destruction, its destruction frees the GL
// resources. Create a new compositor when creating a new context.
class GlesTextOverlayCompositor {
public:
  explicit GlesTextOverlayCompositor(GlesTextOverlaySource source);
  ~GlesTextOverlayCompositor();

  bool Draw(SDL_Window *window);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace roblox::graphics

#endif // ROBLOX_GRAPHICS_GLES_TEXT_OVERLAY_COMPOSITOR_H_
