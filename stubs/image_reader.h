// Internal interface shared by libmediandk's AImageReader and libcamera2ndk.
#pragma once

#include <cstdint>

struct AImageReader;

extern "C" {

// Returns the reader that owns |window|, or nullptr if it is not an
// AImageReader window.
AImageReader* Roblox_ImageReaderFromWindow(void* window);

int32_t Roblox_ImageReaderWidth(AImageReader* reader);
int32_t Roblox_ImageReaderHeight(AImageReader* reader);
int32_t Roblox_ImageReaderFormat(AImageReader* reader);

// Delivers one planar YUV_420_888 frame. Returns 0 when the frame was queued.
int Roblox_ImageReaderDeliverI420(AImageReader* reader, const uint8_t* y,
                                  const uint8_t* u, const uint8_t* v,
                                  int32_t yStride, int32_t uvStride,
                                  int32_t width, int32_t height,
                                  int64_t timestampNs);

// Delivers one RGBA_8888 frame.
int Roblox_ImageReaderDeliverRGBA(AImageReader* reader, const uint8_t* rgba,
                                  int32_t stride, int32_t width, int32_t height,
                                  int64_t timestampNs);

}  // extern "C"
