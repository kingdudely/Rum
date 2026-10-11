#ifndef ROBLOX_PLATFORM_TEXT_CLIPBOARD_H_
#define ROBLOX_PLATFORM_TEXT_CLIPBOARD_H_

#include <memory>
#include <string>

#include "roblox/status.h"

namespace roblox {
namespace platform {

// Main-thread platform boundary for UTF-8 clipboard text. Implementations and
// consumers must not log clipboard contents.
class TextClipboard {
 public:
  virtual ~TextClipboard() = default;

  virtual Status ReadText(std::string* text) = 0;
  virtual Status WriteText(const std::string& text) = 0;
};

// Creates the SDL3 clipboard adapter used by both XWayland and native Wayland.
std::unique_ptr<TextClipboard> CreateSdlTextClipboard();

}  // namespace platform
}  // namespace roblox

#endif  // ROBLOX_PLATFORM_TEXT_CLIPBOARD_H_
