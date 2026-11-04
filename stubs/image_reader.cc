// AImageReader / AImage implementation for the Android media NDK shim.
//
// The host has no gralloc or hardware buffers, so images are backed by plain
// CPU memory. The camera2 shim fills them from V4L2 and hands them to the
// engine through the regular AImageReader API.

#include "image_reader.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace {

using media_status_t = int32_t;
constexpr media_status_t AMEDIA_OK = 0;
constexpr media_status_t AMEDIA_ERROR_UNKNOWN = -10000;
constexpr media_status_t AMEDIA_ERROR_INVALID_PARAMETER = -10001;
constexpr media_status_t AMEDIA_ERROR_INVALID_OBJECT = -10003;
constexpr media_status_t AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE = -10010;

constexpr int32_t AIMAGE_FORMAT_RGBA_8888 = 0x1;
constexpr int32_t AIMAGE_FORMAT_YUV_420_888 = 0x23;
constexpr int kVideoFrameConsumerStateOffset = 0x98;
constexpr int kVideoFrameConsumerRunning = 3;

uint64_t WindowMagic() { return 0x524442494d475744ULL; }  // "RDBIMGW"

bool IsSupported(int32_t format) {
  return format == AIMAGE_FORMAT_YUV_420_888 ||
         format == AIMAGE_FORMAT_RGBA_8888;
}

}  // namespace

struct ANativeWindow;

struct AImageReader_ImageListener {
  void* context;
  void (*onImageAvailable)(void* context, AImageReader* reader);
};

struct AImageReader_BufferRemovedListener {
  void* context;
  void (*onBufferRemoved)(void* context, AImageReader* reader,
                          void* buffer);
};

struct ReaderState {
  struct Buffer {
    std::vector<uint8_t> plane0;
    std::vector<uint8_t> plane1;
    std::vector<uint8_t> plane2;
    bool ready = false;
    bool acquired = false;
    int64_t timestamp = 0;
  };

  int32_t width = 0;
  int32_t height = 0;
  int32_t format = 0;
  int32_t maxImages = 0;
  std::vector<Buffer> buffers;
  std::vector<int> ready;
  std::mutex mutex;
  AImageReader_ImageListener listener{};
  bool hasListener = false;
};

struct ReaderWindow {
  uint64_t magic = 0;
  AImageReader* reader = nullptr;
};

struct AImageReader {
  std::shared_ptr<ReaderState> state;
  ReaderWindow window;
};

struct AImageCropRect {
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;
};

struct AImage {
  std::shared_ptr<ReaderState> state;
  int index = 0;
  int32_t width = 0;
  int32_t height = 0;
  int32_t format = 0;
  int32_t numPlanes = 0;
  uint8_t* plane[3] = {nullptr, nullptr, nullptr};
  int32_t rowStride[3] = {0, 0, 0};
  int32_t pixelStride[3] = {0, 0, 0};
  int64_t timestamp = 0;
};

namespace {

void NotifyImageAvailable(AImageReader* reader) {
  if (!reader) return;
  auto state = reader->state;
  AImageReader_ImageListener listener{};
  bool hasListener = false;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    listener = state->listener;
    hasListener = state->hasListener;
  }
  if (!hasListener || listener.onImageAvailable == nullptr) return;

  // The engine's VideoFrame consumer starts at 1 because the client does not
  // submit a repeating request. Treat the first delivered frame as active so
  // the consumer can acquire it.
  if (listener.context != nullptr) {
    auto* frameState = reinterpret_cast<int*>(
        static_cast<char*>(listener.context) +
        static_cast<std::ptrdiff_t>(kVideoFrameConsumerStateOffset));
    if (*frameState == 1) *frameState = kVideoFrameConsumerRunning;
  }
  listener.onImageAvailable(listener.context, reader);
}

int Deliver(AImageReader* reader, const uint8_t* y, const uint8_t* u,
            const uint8_t* v, int32_t yStride, int32_t uvStride,
            int32_t width, int32_t height, int64_t timestampNs) {
  if (!reader) return -1;
  auto state = reader->state;
  if (width != state->width || height != state->height) return -1;

  int idx = -1;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    for (size_t i = 0; i < state->buffers.size(); ++i) {
      if (!state->buffers[i].ready && !state->buffers[i].acquired) {
        idx = static_cast<int>(i);
        break;
      }
    }
    if (idx < 0 && !state->ready.empty()) {
      // Keep the newest frame available if the consumer has not drained the
      // reader yet; this mirrors a camera stream rather than wedging forever.
      idx = state->ready.front();
      state->ready.erase(state->ready.begin());
      state->buffers[idx].ready = false;
    }
    if (idx < 0) return -2;  // All buffers are held by the consumer.

    auto& buffer = state->buffers[idx];
    const size_t lumaSize = static_cast<size_t>(state->width) * state->height;
    const int32_t chromaWidth = (state->width + 1) / 2;
    const int32_t chromaHeight = (state->height + 1) / 2;
    const size_t chromaSize =
        static_cast<size_t>(chromaWidth) * chromaHeight;
    if (buffer.plane0.size() != lumaSize) buffer.plane0.resize(lumaSize);
    if (buffer.plane1.size() != chromaSize) buffer.plane1.resize(chromaSize);
    if (buffer.plane2.size() != chromaSize) buffer.plane2.resize(chromaSize);

    for (int32_t row = 0; row < state->height; ++row) {
      memcpy(buffer.plane0.data() + static_cast<size_t>(row) * state->width,
             y + static_cast<size_t>(row) * yStride, state->width);
    }
    for (int32_t row = 0; row < chromaHeight; ++row) {
      memcpy(buffer.plane1.data() + static_cast<size_t>(row) * chromaWidth,
             u + static_cast<size_t>(row) * uvStride, chromaWidth);
      memcpy(buffer.plane2.data() + static_cast<size_t>(row) * chromaWidth,
             v + static_cast<size_t>(row) * uvStride, chromaWidth);
    }
    buffer.timestamp = timestampNs;
    buffer.ready = true;
    state->ready.push_back(idx);
  }

  NotifyImageAvailable(reader);
  return 0;
}

}  // namespace

extern "C" {

AImageReader* Roblox_ImageReaderFromWindow(void* window) {
  auto* w = static_cast<ReaderWindow*>(window);
  if (!w || w->magic != WindowMagic()) return nullptr;
  return w->reader;
}

int32_t Roblox_ImageReaderWidth(AImageReader* reader) {
  return reader ? reader->state->width : 0;
}
int32_t Roblox_ImageReaderHeight(AImageReader* reader) {
  return reader ? reader->state->height : 0;
}
int32_t Roblox_ImageReaderFormat(AImageReader* reader) {
  return reader ? reader->state->format : 0;
}

int Roblox_ImageReaderDeliverI420(AImageReader* reader, const uint8_t* y,
                                  const uint8_t* u, const uint8_t* v,
                                  int32_t yStride, int32_t uvStride,
                                  int32_t width, int32_t height,
                                  int64_t timestampNs) {
  if (!reader) return -1;
  return Deliver(reader, y, u, v, yStride, uvStride, width, height,
                 timestampNs);
}

int Roblox_ImageReaderDeliverRGBA(AImageReader* reader, const uint8_t* rgba,
                                  int32_t stride, int32_t width, int32_t height,
                                  int64_t timestampNs) {
  if (!reader || width != reader->state->width ||
      height != reader->state->height) {
    return -1;
  }

  int idx = -1;
  {
    std::lock_guard<std::mutex> lock(reader->state->mutex);
    for (size_t i = 0; i < reader->state->buffers.size(); ++i) {
      if (!reader->state->buffers[i].ready &&
          !reader->state->buffers[i].acquired) {
        idx = static_cast<int>(i);
        break;
      }
    }
    if (idx < 0) return -2;
    auto& buffer = reader->state->buffers[idx];
    const size_t size = static_cast<size_t>(width) * height * 4;
    if (buffer.plane0.size() != size) buffer.plane0.resize(size);
    for (int32_t row = 0; row < height; ++row) {
      memcpy(buffer.plane0.data() + static_cast<size_t>(row) * width * 4,
             rgba + static_cast<size_t>(row) * stride,
             static_cast<size_t>(width) * 4);
    }
    buffer.timestamp = timestampNs;
    buffer.ready = true;
    reader->state->ready.push_back(idx);
  }

  NotifyImageAvailable(reader);
  return 0;
}

media_status_t AImageReader_new(int32_t width, int32_t height, int32_t format,
                                int32_t maxImages, AImageReader** out) {
  if (!out || width <= 0 || height <= 0 || maxImages <= 0 ||
      !IsSupported(format)) {
    return AMEDIA_ERROR_INVALID_PARAMETER;
  }

  auto state = std::make_shared<ReaderState>();
  state->width = width;
  state->height = height;
  state->format = format;
  state->maxImages = maxImages;
  state->buffers.resize(static_cast<size_t>(maxImages));

  auto* reader = new AImageReader();
  reader->state = state;
  reader->window.magic = WindowMagic();
  reader->window.reader = reader;
  *out = reader;
  return AMEDIA_OK;
}

media_status_t AImageReader_newWithUsage(int32_t width, int32_t height,
                                         int32_t format, uint64_t usage,
                                         int32_t maxImages,
                                         AImageReader** out) {
  (void)usage;
  return AImageReader_new(width, height, format, maxImages, out);
}

media_status_t AImageReader_newWithDataSpace(int32_t width, int32_t height,
                                             uint64_t usage, int32_t format,
                                             int32_t dataSpace,
                                             int32_t maxImages,
                                             AImageReader** out) {
  (void)usage;
  (void)dataSpace;
  return AImageReader_new(width, height, format, maxImages, out);
}

void AImageReader_delete(AImageReader* reader) {
  if (!reader) return;
  {
    std::lock_guard<std::mutex> lock(reader->state->mutex);
    reader->state->hasListener = false;
  }
  reader->window.reader = nullptr;
  delete reader;
}

media_status_t AImageReader_getWindow(AImageReader* reader,
                                      ANativeWindow** window) {
  if (!reader || !window) return AMEDIA_ERROR_INVALID_PARAMETER;
  *window = reinterpret_cast<ANativeWindow*>(&reader->window);
  return AMEDIA_OK;
}

media_status_t AImageReader_getWidth(const AImageReader* reader,
                                     int32_t* width) {
  if (!reader || !width) return AMEDIA_ERROR_INVALID_PARAMETER;
  *width = reader->state->width;
  return AMEDIA_OK;
}

media_status_t AImageReader_getHeight(const AImageReader* reader,
                                      int32_t* height) {
  if (!reader || !height) return AMEDIA_ERROR_INVALID_PARAMETER;
  *height = reader->state->height;
  return AMEDIA_OK;
}

media_status_t AImageReader_getFormat(const AImageReader* reader,
                                      int32_t* format) {
  if (!reader || !format) return AMEDIA_ERROR_INVALID_PARAMETER;
  *format = reader->state->format;
  return AMEDIA_OK;
}

media_status_t AImageReader_getMaxImages(const AImageReader* reader,
                                         int32_t* maxImages) {
  if (!reader || !maxImages) return AMEDIA_ERROR_INVALID_PARAMETER;
  *maxImages = reader->state->maxImages;
  return AMEDIA_OK;
}

media_status_t AImageReader_setImageListener(
    AImageReader* reader, AImageReader_ImageListener* listener) {
  if (!reader) return AMEDIA_ERROR_INVALID_PARAMETER;
  std::lock_guard<std::mutex> lock(reader->state->mutex);
  if (listener) {
    reader->state->listener = *listener;
    reader->state->hasListener = true;
  } else {
    reader->state->hasListener = false;
  }
  return AMEDIA_OK;
}

media_status_t AImageReader_setBufferRemovedListener(
    AImageReader* reader, AImageReader_BufferRemovedListener* listener) {
  (void)listener;
  return reader ? AMEDIA_OK : AMEDIA_ERROR_INVALID_PARAMETER;
}

namespace {

media_status_t Acquire(AImageReader* reader, bool latest, AImage** image) {
  if (!reader || !image) return AMEDIA_ERROR_INVALID_PARAMETER;
  auto state = reader->state;
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->ready.empty()) return AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;

  const int idx = latest ? state->ready.back() : state->ready.front();
  if (latest) {
    // A latest-image acquire discards every queued frame, including the ones
    // it did not return. Clear their ready flags so the buffers can be reused.
    for (int readyIndex : state->ready) {
      state->buffers[readyIndex].ready = false;
    }
    state->ready.clear();
  } else {
    state->ready.erase(state->ready.begin());
    state->buffers[idx].ready = false;
  }
  auto& buffer = state->buffers[idx];
  buffer.ready = false;
  buffer.acquired = true;

  auto* out = new AImage();
  out->state = state;
  out->index = idx;
  out->width = state->width;
  out->height = state->height;
  out->format = state->format;
  out->timestamp = buffer.timestamp;

  if (state->format == AIMAGE_FORMAT_YUV_420_888) {
    out->numPlanes = 3;
    out->plane[0] = buffer.plane0.data();
    out->plane[1] = buffer.plane1.data();
    out->plane[2] = buffer.plane2.data();
    out->rowStride[0] = state->width;
    out->rowStride[1] = (state->width + 1) / 2;
    out->rowStride[2] = (state->width + 1) / 2;
    out->pixelStride[0] = out->pixelStride[1] = out->pixelStride[2] = 1;
  } else {
    out->numPlanes = 1;
    out->plane[0] = buffer.plane0.data();
    out->rowStride[0] = state->width * 4;
    out->pixelStride[0] = 4;
  }
  *image = out;
  return AMEDIA_OK;
}

}  // namespace

media_status_t AImageReader_acquireNextImage(AImageReader* reader,
                                             AImage** image) {
  return Acquire(reader, false, image);
}

media_status_t AImageReader_acquireLatestImage(AImageReader* reader,
                                               AImage** image) {
  return Acquire(reader, true, image);
}

media_status_t AImageReader_acquireNextImageAsync(AImageReader* reader,
                                                  AImage** image,
                                                  int* fenceFd) {
  if (fenceFd) *fenceFd = -1;
  return Acquire(reader, false, image);
}

media_status_t AImageReader_acquireLatestImageAsync(AImageReader* reader,
                                                    AImage** image,
                                                    int* fenceFd) {
  if (fenceFd) *fenceFd = -1;
  return Acquire(reader, true, image);
}

void AImage_delete(AImage* image) {
  if (!image) return;
  if (image->state) {
    std::lock_guard<std::mutex> lock(image->state->mutex);
    if (image->index >= 0 &&
        static_cast<size_t>(image->index) < image->state->buffers.size()) {
      image->state->buffers[image->index].acquired = false;
    }
  }
  delete image;
}

void AImage_deleteAsync(AImage* image, int releaseFenceFd) {
  (void)releaseFenceFd;
  AImage_delete(image);
}

media_status_t AImage_getWidth(const AImage* image, int32_t* width) {
  if (!image || !width) return AMEDIA_ERROR_INVALID_PARAMETER;
  *width = image->width;
  return AMEDIA_OK;
}

media_status_t AImage_getHeight(const AImage* image, int32_t* height) {
  if (!image || !height) return AMEDIA_ERROR_INVALID_PARAMETER;
  *height = image->height;
  return AMEDIA_OK;
}

media_status_t AImage_getFormat(const AImage* image, int32_t* format) {
  if (!image || !format) return AMEDIA_ERROR_INVALID_PARAMETER;
  *format = image->format;
  return AMEDIA_OK;
}

media_status_t AImage_getCropRect(const AImage* image, AImageCropRect* rect) {
  if (!image || !rect) return AMEDIA_ERROR_INVALID_PARAMETER;
  rect->left = 0;
  rect->top = 0;
  rect->right = image->width;
  rect->bottom = image->height;
  return AMEDIA_OK;
}

media_status_t AImage_getTimestamp(const AImage* image, int64_t* timestamp) {
  if (!image || !timestamp) return AMEDIA_ERROR_INVALID_PARAMETER;
  *timestamp = image->timestamp;
  return AMEDIA_OK;
}

media_status_t AImage_getNumberOfPlanes(const AImage* image,
                                        int32_t* numPlanes) {
  if (!image || !numPlanes) return AMEDIA_ERROR_INVALID_PARAMETER;
  *numPlanes = image->numPlanes;
  return AMEDIA_OK;
}

media_status_t AImage_getPlanePixelStride(const AImage* image, int planeIdx,
                                          int32_t* pixelStride) {
  if (!image || !pixelStride || planeIdx < 0 ||
      planeIdx >= image->numPlanes) {
    return AMEDIA_ERROR_INVALID_PARAMETER;
  }
  *pixelStride = image->pixelStride[planeIdx];
  return AMEDIA_OK;
}

media_status_t AImage_getPlaneRowStride(const AImage* image, int planeIdx,
                                        int32_t* rowStride) {
  if (!image || !rowStride || planeIdx < 0 ||
      planeIdx >= image->numPlanes) {
    return AMEDIA_ERROR_INVALID_PARAMETER;
  }
  *rowStride = image->rowStride[planeIdx];
  return AMEDIA_OK;
}

media_status_t AImage_getPlaneData(const AImage* image, int planeIdx,
                                   uint8_t** data, int* dataLength) {
  if (!image || !data || !dataLength || planeIdx < 0 ||
      planeIdx >= image->numPlanes) {
    return AMEDIA_ERROR_INVALID_PARAMETER;
  }
  *data = image->plane[planeIdx];
  int rows = image->height;
  if (image->format == AIMAGE_FORMAT_YUV_420_888 && planeIdx > 0) {
    rows = (image->height + 1) / 2;
  }
  *dataLength = image->rowStride[planeIdx] * rows;
  return AMEDIA_OK;
}

media_status_t AImage_getHardwareBuffer(const AImage* image,
                                        void** buffer) {
  (void)image;
  if (buffer) *buffer = nullptr;
  return AMEDIA_ERROR_UNKNOWN;
}

}  // extern "C"
