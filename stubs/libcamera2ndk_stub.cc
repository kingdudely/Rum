// Android camera2 NDK shim backed by V4L2.
//
// Roblox dlopens libcamera2ndk.so and resolves the ACamera* API dynamically.
// This implementation exposes a single camera per V4L2 capture device and
// delivers frames through AImageReader windows from the media NDK shim.

#include "image_reader.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using camera_status_t = int32_t;
constexpr camera_status_t ACAMERA_OK = 0;
constexpr camera_status_t ACAMERA_ERROR_INVALID_PARAMETER = -10001;
constexpr camera_status_t ACAMERA_ERROR_CAMERA_DISCONNECTED = -10002;
constexpr camera_status_t ACAMERA_ERROR_METADATA_NOT_FOUND = -10004;
constexpr camera_status_t ACAMERA_ERROR_STREAM_CONFIGURE_FAIL = -10009;

constexpr int TEMPLATE_PREVIEW = 1;

constexpr uint8_t ACAMERA_TYPE_BYTE = 0;
constexpr uint8_t ACAMERA_TYPE_INT32 = 1;
constexpr uint8_t ACAMERA_TYPE_FLOAT = 2;
constexpr uint8_t ACAMERA_TYPE_INT64 = 3;
constexpr uint8_t ACAMERA_TYPE_RATIONAL = 5;

constexpr uint32_t TAG_LENS_FACING = 0x00080005;
constexpr uint32_t TAG_SENSOR_ORIENTATION = 0x000e000e;
constexpr uint32_t TAG_SENSOR_ACTIVE_ARRAY = 0x000f0000;
constexpr uint32_t TAG_SENSOR_PIXEL_ARRAY = 0x000f0006;
constexpr uint32_t TAG_STREAM_CONFIGURATIONS = 0x000d000a;
constexpr uint32_t TAG_AE_TARGET_FPS_RANGES = 0x00010014;
constexpr uint32_t TAG_AF_AVAILABLE_MODES = 0x00010017;
constexpr uint32_t TAG_HARDWARE_LEVEL = 0x00150000;
constexpr uint32_t TAG_AE_AVAILABLE_MODES = 0x00010013;
constexpr uint32_t TAG_AWB_AVAILABLE_MODES = 0x0001001b;
constexpr uint32_t TAG_REQUEST_CAPABILITIES = 0x000c000c;
constexpr uint32_t TAG_FOCAL_LENGTHS = 0x00090002;
constexpr uint32_t TAG_PHYSICAL_SIZE = 0x000f0005;
constexpr uint32_t TAG_APERTURES = 0x00090000;
constexpr uint32_t TAG_FLASH_AVAILABLE = 0x00050000;
constexpr uint32_t TAG_AE_COMPENSATION_RANGE = 0x00010015;
constexpr uint32_t TAG_AE_COMPENSATION_STEP = 0x00010016;
constexpr uint32_t TAG_MAX_DIGITAL_ZOOM = 0x000d0004;
constexpr uint32_t TAG_MIN_FOCUS_DISTANCE = 0x00090005;
constexpr uint32_t TAG_SENSITIVITY_RANGE = 0x000f0001;
constexpr uint32_t TAG_EXPOSURE_TIME_RANGE = 0x000f0003;
constexpr uint32_t TAG_MAX_FRAME_DURATION = 0x000f0004;
constexpr uint32_t TAG_VIDEO_STABILIZATION_MODES = 0x0001001a;
constexpr uint32_t TAG_AVAILABLE_EFFECTS = 0x00010018;
constexpr uint32_t TAG_AVAILABLE_SCENE_MODES = 0x00010019;
constexpr uint32_t TAG_FACE_DETECT_MODES = 0x00120000;
constexpr uint32_t TAG_OPTICAL_STABILIZATION = 0x00090003;
constexpr uint32_t TAG_COLOR_FILTER_ARRANGEMENT = 0x000f0002;
constexpr uint32_t TAG_TIMESTAMP_SOURCE = 0x000f0008;
constexpr uint32_t TAG_LENS_POSE_REFERENCE = 0x0008000c;
constexpr uint32_t TAG_CROPPING_TYPE = 0x000d000d;
constexpr uint32_t TAG_FOCUS_CALIBRATION = 0x00090007;
constexpr uint32_t TAG_AE_LOCK_AVAILABLE = 0x00010024;
constexpr uint32_t TAG_AWB_LOCK_AVAILABLE = 0x00010025;
constexpr uint32_t TAG_ANTIBANDING_MODES = 0x0001001d;
constexpr uint32_t TAG_MAX_FACE_COUNT = 0x00120002;

struct Rational {
  int32_t numerator;
  int32_t denominator;
};

}  // namespace

struct ANativeWindow;
struct ACameraDevice;
struct ACaptureRequest;
struct ACaptureSessionOutput;
struct ACaptureSessionOutputContainer;
struct ACameraOutputTarget;
struct ACameraCaptureSession;
struct ACameraCaptureSession_captureCallbacks;
struct ACameraMetadata;

struct ACameraManager_AvailabilityCallbacks {
  void* context;
  void (*onCameraAvailable)(void* context, const char* cameraId);
  void (*onCameraUnavailable)(void* context, const char* cameraId);
};

struct ACameraDevice_StateCallbacks {
  void* context;
  void (*onDisconnected)(void* context, ACameraDevice* device);
  void (*onError)(void* context, ACameraDevice* device, int error);
};

struct ACameraCaptureSession_stateCallbacks {
  void* context;
  void (*onClosed)(void* context, ACameraCaptureSession* session);
  void (*onReady)(void* context, ACameraCaptureSession* session);
  void (*onActive)(void* context, ACameraCaptureSession* session);
};

struct ACameraIdList {
  int numCameras;
  const char** cameraIds;
};

struct ACameraMetadata_const_entry {
  uint32_t tag;
  uint8_t type;
  uint32_t count;
  union {
    const uint8_t* u8;
    const int32_t* i32;
    const float* f;
    const int64_t* i64;
    const double* d;
    const void* r;
  } data;
};

struct MetadataEntry {
  uint8_t type = 0;
  uint32_t count = 0;
  std::vector<uint8_t> bytes;
};

struct ACameraMetadata {
  std::map<uint32_t, MetadataEntry> entries;
  std::vector<uint32_t> tags;
};

void AddEntry(ACameraMetadata* metadata, uint32_t tag, uint8_t type,
              uint32_t count, const void* data, size_t bytes) {
  MetadataEntry entry;
  entry.type = type;
  entry.count = count;
  entry.bytes.assign(static_cast<const uint8_t*>(data),
                     static_cast<const uint8_t*>(data) + bytes);
  metadata->entries[tag] = std::move(entry);
  metadata->tags.push_back(tag);
}

void AddU8(ACameraMetadata* m, uint32_t tag, uint8_t value) {
  AddEntry(m, tag, ACAMERA_TYPE_BYTE, 1, &value, sizeof(value));
}

void AddI32(ACameraMetadata* m, uint32_t tag,
            std::initializer_list<int32_t> values) {
  AddEntry(m, tag, ACAMERA_TYPE_INT32, static_cast<uint32_t>(values.size()),
           values.begin(), values.size() * sizeof(int32_t));
}

void AddI64(ACameraMetadata* m, uint32_t tag,
            std::initializer_list<int64_t> values) {
  AddEntry(m, tag, ACAMERA_TYPE_INT64, static_cast<uint32_t>(values.size()),
           values.begin(), values.size() * sizeof(int64_t));
}

void AddFloat(ACameraMetadata* m, uint32_t tag,
              std::initializer_list<float> values) {
  AddEntry(m, tag, ACAMERA_TYPE_FLOAT, static_cast<uint32_t>(values.size()),
           values.begin(), values.size() * sizeof(float));
}

void AddRational(ACameraMetadata* m, uint32_t tag, Rational value) {
  AddEntry(m, tag, ACAMERA_TYPE_RATIONAL, 1, &value, sizeof(value));
}

struct CameraInfo {
  std::string id;
  std::string path;
  uint8_t facing = 0;  // ACAMERA_LENS_FACING_FRONT
};

struct ManagerState {
  std::mutex mutex;
  std::vector<CameraInfo> cameras;
  bool hasCallbacks = false;
  ACameraManager_AvailabilityCallbacks callbacks{};
  bool alive = true;
};

struct ACameraManager {
  std::shared_ptr<ManagerState> state;
};

bool ProbeCamera(const std::string& path, CameraInfo* info) {
  const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) return false;

  v4l2_capability cap{};
  bool ok = ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
  if (ok) {
    const uint32_t caps = cap.capabilities | cap.device_caps;
    ok = (caps & V4L2_CAP_VIDEO_CAPTURE) != 0 &&
         (caps & V4L2_CAP_STREAMING) != 0;
  }
  if (ok) {
    // Only keep nodes that actually expose a video capture format.
    v4l2_fmtdesc desc{};
    desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    desc.index = 0;
    ok = ioctl(fd, VIDIOC_ENUM_FMT, &desc) == 0;
  }
  close(fd);
  if (!ok) return false;

  info->path = path;
  return true;
}

ACameraMetadata* BuildCharacteristics(const CameraInfo& camera) {
  auto* m = new ACameraMetadata();
  AddU8(m, TAG_LENS_FACING, camera.facing);
  AddI32(m, TAG_SENSOR_ORIENTATION, {0});
  AddI32(m, TAG_SENSOR_ACTIVE_ARRAY, {0, 0, 640, 480});
  AddI32(m, TAG_SENSOR_PIXEL_ARRAY, {640, 480});
  AddI32(m, TAG_STREAM_CONFIGURATIONS,
         {0x23, 640, 480, 0, 0x23, 1280, 720, 0, 0x23, 1920, 1080, 0,
          0x1, 640, 480, 0, 0x1, 1280, 720, 0});
  AddI32(m, TAG_AE_TARGET_FPS_RANGES, {15, 30, 30, 30});
  AddI32(m, TAG_AE_AVAILABLE_MODES, {0, 1});
  AddI32(m, TAG_AWB_AVAILABLE_MODES, {0, 1});
  AddI32(m, TAG_AF_AVAILABLE_MODES, {0});
  AddI32(m, TAG_HARDWARE_LEVEL, {0});
  AddI32(m, TAG_REQUEST_CAPABILITIES, {0, 1});
  AddFloat(m, TAG_FOCAL_LENGTHS, {4.0f});
  AddFloat(m, TAG_PHYSICAL_SIZE, {3.6f, 2.7f});
  AddFloat(m, TAG_APERTURES, {2.0f});
  AddU8(m, TAG_FLASH_AVAILABLE, 0);
  AddI32(m, TAG_AE_COMPENSATION_RANGE, {-2, 2});
  AddRational(m, TAG_AE_COMPENSATION_STEP, {1, 3});
  AddFloat(m, TAG_MAX_DIGITAL_ZOOM, {4.0f});
  AddFloat(m, TAG_MIN_FOCUS_DISTANCE, {0.0f});
  AddI32(m, TAG_SENSITIVITY_RANGE, {100, 3200});
  AddI64(m, TAG_EXPOSURE_TIME_RANGE, {100000, 1000000000LL});
  AddI64(m, TAG_MAX_FRAME_DURATION, {1000000000LL});
  AddI32(m, TAG_VIDEO_STABILIZATION_MODES, {0});
  AddI32(m, TAG_AVAILABLE_EFFECTS, {0});
  AddI32(m, TAG_AVAILABLE_SCENE_MODES, {0});
  AddI32(m, TAG_FACE_DETECT_MODES, {0});
  AddI32(m, TAG_MAX_FACE_COUNT, {0});
  AddI32(m, TAG_OPTICAL_STABILIZATION, {0});
  AddU8(m, TAG_COLOR_FILTER_ARRANGEMENT, 1);
  AddU8(m, TAG_TIMESTAMP_SOURCE, 1);
  AddU8(m, TAG_LENS_POSE_REFERENCE, 1);
  AddU8(m, TAG_CROPPING_TYPE, 0);
  AddU8(m, TAG_FOCUS_CALIBRATION, 0);
  AddU8(m, TAG_AE_LOCK_AVAILABLE, 0);
  AddU8(m, TAG_AWB_LOCK_AVAILABLE, 0);
  AddI32(m, TAG_ANTIBANDING_MODES, {0, 1, 2, 3});
  return m;
}

struct V4L2Buffer {
  void* start = nullptr;
  size_t length = 0;
};

struct DeviceState {
  int fd = -1;
  ACameraDevice_StateCallbacks callbacks{};
  std::mutex mutex;
  std::thread captureThread;
  std::atomic<bool> running{false};
  uint32_t pixelFormat = V4L2_PIX_FMT_YUYV;
  int srcWidth = 0;
  int srcHeight = 0;
  int srcStride = 0;
  std::vector<V4L2Buffer> buffers;
};

struct ACameraDevice {
  std::shared_ptr<DeviceState> state;
};

struct ACaptureSessionOutput {
  ANativeWindow* window = nullptr;
};

struct ACaptureSessionOutputContainer {
  std::vector<const ACaptureSessionOutput*> outputs;
};

struct ACameraOutputTarget {
  ANativeWindow* window = nullptr;
};

struct ACaptureRequest {
  int templateId = TEMPLATE_PREVIEW;
  std::vector<ACameraOutputTarget*> targets;
  std::map<uint32_t, std::vector<int32_t>> entries;
};

struct ACameraCaptureSession {
  std::shared_ptr<DeviceState> device;
  ACameraCaptureSession_stateCallbacks callbacks{};
  AImageReader* reader = nullptr;
  std::shared_ptr<std::atomic<bool>> alive;
};

uint8_t Clamp(int value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return static_cast<uint8_t>(value);
}

void YuyvToI420(const uint8_t* src, int srcStride, int srcW, int srcH,
                int outW, int outH, std::vector<uint8_t>* y,
                std::vector<uint8_t>* u, std::vector<uint8_t>* v) {
  const int uvW = (outW + 1) / 2;
  const int uvH = (outH + 1) / 2;
  y->resize(static_cast<size_t>(outW) * outH);
  u->resize(static_cast<size_t>(uvW) * uvH);
  v->resize(static_cast<size_t>(uvW) * uvH);

  for (int row = 0; row < outH; ++row) {
    const int sy = row * srcH / outH;
    const uint8_t* srow = src + static_cast<size_t>(sy) * srcStride;
    for (int col = 0; col < outW; ++col) {
      const int sx = col * srcW / outW;
      const uint8_t* p = srow + static_cast<size_t>(sx & ~1) * 2;
      (*y)[static_cast<size_t>(row) * outW + col] = p[(sx & 1) * 2];
      if ((col & 1) == 0 && (row & 1) == 0) {
        const int uvRow = row / 2;
        (*u)[static_cast<size_t>(uvRow) * uvW + col / 2] = p[1];
        (*v)[static_cast<size_t>(uvRow) * uvW + col / 2] = p[3];
      }
    }
  }
}

void Nv12ToI420(const uint8_t* src, int srcStride, int srcW, int srcH,
                int outW, int outH, std::vector<uint8_t>* y,
                std::vector<uint8_t>* u, std::vector<uint8_t>* v) {
  const int uvW = (outW + 1) / 2;
  const int uvH = (outH + 1) / 2;
  y->resize(static_cast<size_t>(outW) * outH);
  u->resize(static_cast<size_t>(uvW) * uvH);
  v->resize(static_cast<size_t>(uvW) * uvH);
  const uint8_t* uvPlane = src + static_cast<size_t>(srcStride) * srcH;

  for (int row = 0; row < outH; ++row) {
    const int sy = row * srcH / outH;
    const uint8_t* srow = src + static_cast<size_t>(sy) * srcStride;
    for (int col = 0; col < outW; ++col) {
      const int sx = col * srcW / outW;
      (*y)[static_cast<size_t>(row) * outW + col] = srow[sx];
      if ((col & 1) == 0 && (row & 1) == 0) {
        const uint8_t* uv =
            uvPlane + static_cast<size_t>(sy / 2) * srcStride + (sx & ~1);
        const int uvRow = row / 2;
        (*u)[static_cast<size_t>(uvRow) * uvW + col / 2] = uv[0];
        (*v)[static_cast<size_t>(uvRow) * uvW + col / 2] = uv[1];
      }
    }
  }
}

void I420ToRgba(const std::vector<uint8_t>& y, const std::vector<uint8_t>& u,
                const std::vector<uint8_t>& v, int width, int height,
                std::vector<uint8_t>* rgba) {
  const int uvW = (width + 1) / 2;
  rgba->resize(static_cast<size_t>(width) * height * 4);
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      const int yy = y[static_cast<size_t>(row) * width + col];
      const int uu = u[static_cast<size_t>(row / 2) * uvW + col / 2] - 128;
      const int vv = v[static_cast<size_t>(row / 2) * uvW + col / 2] - 128;
      const int r = yy + (359 * vv) / 256;
      const int g = yy - (88 * uu + 183 * vv) / 256;
      const int b = yy + (454 * uu) / 256;
      uint8_t* p = rgba->data() + (static_cast<size_t>(row) * width + col) * 4;
      p[0] = Clamp(r);
      p[1] = Clamp(g);
      p[2] = Clamp(b);
      p[3] = 255;
    }
  }
}

void CaptureLoop(std::shared_ptr<DeviceState> state, AImageReader* reader,
                 int outWidth, int outHeight) {
  std::vector<uint8_t> y;
  std::vector<uint8_t> u;
  std::vector<uint8_t> v;
  std::vector<uint8_t> rgba;

  while (state->running) {
    pollfd pfd{state->fd, POLLIN, 0};
    if (poll(&pfd, 1, 100) <= 0) continue;

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(state->fd, VIDIOC_DQBUF, &buf) < 0) continue;
    if (buf.index >= state->buffers.size()) {
      ioctl(state->fd, VIDIOC_QBUF, &buf);
      continue;
    }

    const uint8_t* src =
        static_cast<const uint8_t*>(state->buffers[buf.index].start);
    if (state->pixelFormat == V4L2_PIX_FMT_YUYV) {
      YuyvToI420(src, state->srcStride, state->srcWidth, state->srcHeight,
                 outWidth, outHeight, &y, &u, &v);
    } else {
      Nv12ToI420(src, state->srcStride, state->srcWidth, state->srcHeight,
                 outWidth, outHeight, &y, &u, &v);
    }

    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t timestamp =
        static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;

    if (Roblox_ImageReaderFormat(reader) == 0x23) {
      (void)Roblox_ImageReaderDeliverI420(
          reader, y.data(), u.data(), v.data(), outWidth, (outWidth + 1) / 2,
          outWidth, outHeight, timestamp);
    } else {
      I420ToRgba(y, u, v, outWidth, outHeight, &rgba);
      Roblox_ImageReaderDeliverRGBA(reader, rgba.data(), outWidth * 4, outWidth,
                                    outHeight, timestamp);
    }
    ioctl(state->fd, VIDIOC_QBUF, &buf);
  }
}

void StopCapture(DeviceState* state) {
  state->running = false;
  if (state->captureThread.joinable()) state->captureThread.join();

  if (state->fd >= 0 && !state->buffers.empty()) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(state->fd, VIDIOC_STREAMOFF, &type);
    for (const auto& buffer : state->buffers) {
      if (buffer.start) munmap(buffer.start, buffer.length);
    }
    state->buffers.clear();
    v4l2_requestbuffers req{};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(state->fd, VIDIOC_REQBUFS, &req);
  }
}

bool StartCapture(DeviceState* state, AImageReader* reader) {
  StopCapture(state);
  if (state->fd < 0 || !reader) return false;

  const int width = Roblox_ImageReaderWidth(reader);
  const int height = Roblox_ImageReaderHeight(reader);
  if (width <= 0 || height <= 0) return false;

  v4l2_format format{};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  format.fmt.pix.width = static_cast<uint32_t>(width);
  format.fmt.pix.height = static_cast<uint32_t>(height);
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  format.fmt.pix.field = V4L2_FIELD_ANY;
  if (ioctl(state->fd, VIDIOC_S_FMT, &format) < 0) {
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
    if (ioctl(state->fd, VIDIOC_S_FMT, &format) < 0) {
      std::fprintf(stderr, "[camera2ndk] no supported V4L2 pixel format\n");
      return false;
    }
  }

  if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV &&
      format.fmt.pix.pixelformat != V4L2_PIX_FMT_NV12) {
    std::fprintf(stderr, "[camera2ndk] unsupported format 0x%x\n",
                 format.fmt.pix.pixelformat);
    return false;
  }

  state->pixelFormat = format.fmt.pix.pixelformat;
  state->srcWidth = static_cast<int>(format.fmt.pix.width);
  state->srcHeight = static_cast<int>(format.fmt.pix.height);
  state->srcStride = static_cast<int>(format.fmt.pix.bytesperline);
  if (state->srcStride <= 0) state->srcStride = state->srcWidth * 2;
  v4l2_requestbuffers request{};
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  request.count = 4;
  if (ioctl(state->fd, VIDIOC_REQBUFS, &request) < 0 || request.count < 2) {
    std::fprintf(stderr, "[camera2ndk] VIDIOC_REQBUFS failed\n");
    return false;
  }

  state->buffers.resize(request.count);
  for (uint32_t i = 0; i < request.count; ++i) {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = i;
    if (ioctl(state->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
      StopCapture(state);
      return false;
    }
    void* start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, state->fd, buffer.m.offset);
    if (start == MAP_FAILED) {
      StopCapture(state);
      return false;
    }
    state->buffers[i].start = start;
    state->buffers[i].length = buffer.length;
    if (ioctl(state->fd, VIDIOC_QBUF, &buffer) < 0) {
      StopCapture(state);
      return false;
    }
  }

  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(state->fd, VIDIOC_STREAMON, &type) < 0) {
    std::fprintf(stderr, "[camera2ndk] VIDIOC_STREAMON failed\n");
    StopCapture(state);
    return false;
  }

  state->running = true;
  auto shared = std::shared_ptr<DeviceState>(state, [](DeviceState*) {});
  // The capture thread must outlive the session but not the device; the
  // device keeps the state alive through ACameraDevice.
  state->captureThread =
      std::thread([shared, reader, width, height]() {
        CaptureLoop(shared, reader, width, height);
      });
  return true;
}

extern "C" {

ACameraManager* ACameraManager_create() {
  auto state = std::make_shared<ManagerState>();
  for (int index = 0; index < 16; ++index) {
    char path[32];
    std::snprintf(path, sizeof(path), "/dev/video%d", index);
    CameraInfo info;
    if (!ProbeCamera(path, &info)) continue;
    info.id = std::to_string(index);
    info.facing = state->cameras.empty() ? 0 : 1;
    state->cameras.push_back(std::move(info));
  }
  if (state->cameras.empty()) {
    std::fprintf(stderr, "[camera2ndk] no V4L2 capture devices found\n");
  }

  auto* manager = new ACameraManager();
  manager->state = std::move(state);
  return manager;
}

void ACameraManager_delete(ACameraManager* manager) {
  if (!manager) return;
  {
    std::lock_guard<std::mutex> lock(manager->state->mutex);
    manager->state->alive = false;
    manager->state->hasCallbacks = false;
  }
  delete manager;
}

camera_status_t ACameraManager_getCameraIdList(ACameraManager* manager,
                                               ACameraIdList** list) {
  if (!manager || !list) return ACAMERA_ERROR_INVALID_PARAMETER;
  auto& cameras = manager->state->cameras;
  auto* result = static_cast<ACameraIdList*>(
      std::calloc(1, sizeof(ACameraIdList)));
  auto** ids = static_cast<const char**>(
      std::calloc(cameras.size() + 1, sizeof(const char*)));
  if (!result || !ids) {
    std::free(result);
    std::free(ids);
    return ACAMERA_ERROR_INVALID_PARAMETER;
  }
  for (size_t i = 0; i < cameras.size(); ++i) {
    ids[i] = strdup(cameras[i].id.c_str());
  }
  result->numCameras = static_cast<int>(cameras.size());
  result->cameraIds = ids;
  *list = result;
  return ACAMERA_OK;
}

void ACameraManager_deleteCameraIdList(ACameraIdList* list) {
  if (!list) return;
  for (int i = 0; i < list->numCameras; ++i) {
    std::free(const_cast<char*>(list->cameraIds[i]));
  }
  std::free(const_cast<char**>(list->cameraIds));
  std::free(list);
}

camera_status_t ACameraManager_registerAvailabilityCallback(
    ACameraManager* manager,
    const ACameraManager_AvailabilityCallbacks* callbacks) {
  if (!manager || !callbacks || !callbacks->onCameraAvailable ||
      !callbacks->onCameraUnavailable) {
    return ACAMERA_ERROR_INVALID_PARAMETER;
  }
  {
    std::lock_guard<std::mutex> lock(manager->state->mutex);
    manager->state->callbacks = *callbacks;
    manager->state->hasCallbacks = true;
  }

  auto state = manager->state;
  std::thread([state]() {
    usleep(50000);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->alive || !state->hasCallbacks) return;
    for (const auto& camera : state->cameras) {
      state->callbacks.onCameraAvailable(state->callbacks.context,
                                         camera.id.c_str());
    }
  }).detach();
  return ACAMERA_OK;
}

camera_status_t ACameraManager_unregisterAvailabilityCallback(
    ACameraManager* manager,
    const ACameraManager_AvailabilityCallbacks* callbacks) {
  if (!manager || !callbacks) return ACAMERA_ERROR_INVALID_PARAMETER;
  std::lock_guard<std::mutex> lock(manager->state->mutex);
  manager->state->hasCallbacks = false;
  return ACAMERA_OK;
}

camera_status_t ACameraManager_getCameraCharacteristics(
    ACameraManager* manager, const char* cameraId,
    ACameraMetadata** characteristics) {
  if (!manager || !cameraId || !characteristics) {
    return ACAMERA_ERROR_INVALID_PARAMETER;
  }
  for (const auto& camera : manager->state->cameras) {
    if (camera.id == cameraId) {
      *characteristics = BuildCharacteristics(camera);
      return ACAMERA_OK;
    }
  }
  return ACAMERA_ERROR_INVALID_PARAMETER;
}

camera_status_t ACameraManager_openCamera(
    ACameraManager* manager, const char* cameraId,
    ACameraDevice_StateCallbacks* callbacks, ACameraDevice** device) {
  if (!manager || !cameraId || !device) {
    return ACAMERA_ERROR_INVALID_PARAMETER;
  }
  const CameraInfo* found = nullptr;
  for (const auto& camera : manager->state->cameras) {
    if (camera.id == cameraId) {
      found = &camera;
      break;
    }
  }
  if (!found) return ACAMERA_ERROR_INVALID_PARAMETER;

  const int fd = open(found->path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    std::fprintf(stderr, "[camera2ndk] open %s failed\n", found->path.c_str());
    return ACAMERA_ERROR_CAMERA_DISCONNECTED;
  }

  auto state = std::make_shared<DeviceState>();
  state->fd = fd;
  if (callbacks) state->callbacks = *callbacks;

  auto* result = new ACameraDevice();
  result->state = state;
  *device = result;
  return ACAMERA_OK;
}

camera_status_t ACameraDevice_close(ACameraDevice* device) {
  if (!device) return ACAMERA_ERROR_INVALID_PARAMETER;
  StopCapture(device->state.get());
  if (device->state->fd >= 0) close(device->state->fd);
  device->state->fd = -1;
  delete device;
  return ACAMERA_OK;
}

camera_status_t ACameraDevice_createCaptureRequest(
    const ACameraDevice* device, int templateId, ACaptureRequest** request) {
  if (!device || !request) return ACAMERA_ERROR_INVALID_PARAMETER;
  auto* result = new ACaptureRequest();
  result->templateId = templateId;
  *request = result;
  return ACAMERA_OK;
}

void ACaptureRequest_addTarget(ACaptureRequest* request,
                               const ACameraOutputTarget* target) {
  if (request && target) request->targets.push_back(
      const_cast<ACameraOutputTarget*>(target));
}

void ACaptureRequest_removeTarget(ACaptureRequest* request,
                                  const ACameraOutputTarget* target) {
  if (!request) return;
  request->targets.erase(
      std::remove(request->targets.begin(), request->targets.end(), target),
      request->targets.end());
}

camera_status_t ACaptureRequest_setEntry_i32(ACaptureRequest* request,
                                             uint32_t tag, uint32_t count,
                                             const int32_t* data) {
  if (!request || (count && !data)) return ACAMERA_ERROR_INVALID_PARAMETER;
  request->entries[tag].assign(data, data + count);
  return ACAMERA_OK;
}

void ACaptureRequest_free(ACaptureRequest* request) { delete request; }

camera_status_t ACaptureSessionOutputContainer_create(
    ACaptureSessionOutputContainer** container) {
  if (!container) return ACAMERA_ERROR_INVALID_PARAMETER;
  *container = new ACaptureSessionOutputContainer();
  return ACAMERA_OK;
}

void ACaptureSessionOutputContainer_free(
    ACaptureSessionOutputContainer* container) {
  delete container;
}

camera_status_t ACaptureSessionOutputContainer_add(
    ACaptureSessionOutputContainer* container,
    const ACaptureSessionOutput* output) {
  if (!container || !output) return ACAMERA_ERROR_INVALID_PARAMETER;
  container->outputs.push_back(output);
  return ACAMERA_OK;
}

camera_status_t ACaptureSessionOutputContainer_remove(
    ACaptureSessionOutputContainer* container,
    const ACaptureSessionOutput* output) {
  if (!container || !output) return ACAMERA_ERROR_INVALID_PARAMETER;
  container->outputs.erase(
      std::remove(container->outputs.begin(), container->outputs.end(),
                  output),
      container->outputs.end());
  return ACAMERA_OK;
}

camera_status_t ACaptureSessionOutput_create(ANativeWindow* window,
                                             ACaptureSessionOutput** output) {
  if (!window || !output) return ACAMERA_ERROR_INVALID_PARAMETER;
  auto* result = new ACaptureSessionOutput();
  result->window = window;
  *output = result;
  return ACAMERA_OK;
}

void ACaptureSessionOutput_free(ACaptureSessionOutput* output) {
  delete output;
}

camera_status_t ACameraOutputTarget_create(ANativeWindow* window,
                                           ACameraOutputTarget** target) {
  if (!window || !target) return ACAMERA_ERROR_INVALID_PARAMETER;
  auto* result = new ACameraOutputTarget();
  result->window = window;
  *target = result;
  return ACAMERA_OK;
}

void ACameraOutputTarget_free(ACameraOutputTarget* target) { delete target; }

camera_status_t ACameraDevice_createCaptureSession(
    ACameraDevice* device, const ACaptureSessionOutputContainer* outputs,
    const ACameraCaptureSession_stateCallbacks* callbacks,
    ACameraCaptureSession** session) {
  if (!device || !outputs || !callbacks || !session || !device->state) {
    return ACAMERA_ERROR_INVALID_PARAMETER;
  }
  auto* result = new ACameraCaptureSession();
  result->device = device->state;
  result->callbacks = *callbacks;
  for (const ACaptureSessionOutput* output : outputs->outputs) {
    if (!output) continue;
    result->reader = Roblox_ImageReaderFromWindow(output->window);
    if (result->reader) break;
  }
  *session = result;
  result->alive = std::make_shared<std::atomic<bool>>(true);

  // Some clients wait for onReady before submitting a repeating request. Start
  // with the session output if one is already an AImageReader so preview can
  // proceed even if that request is delayed or omitted.
  if (result->reader != nullptr &&
      !StartCapture(result->device.get(), result->reader)) {
    std::fprintf(stderr, "[camera2ndk] session capture failed\n");
  }

  // Camera2 NDK reports session state asynchronously. Calling these from
  // inside createCaptureSession lets the callback race the client's assignment
  // of the returned session pointer, so defer them until the call has returned
  // and the client has installed its handler.
  auto session_callbacks = result->callbacks;
  auto alive = result->alive;
  std::thread([session_callbacks, result, alive]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!alive->load()) return;
    if (session_callbacks.onReady) {
      session_callbacks.onReady(session_callbacks.context, result);
    }
    if (session_callbacks.onActive) {
      session_callbacks.onActive(session_callbacks.context, result);
    }
  }).detach();
  return ACAMERA_OK;
}

camera_status_t ACameraCaptureSession_setRepeatingRequest(
    ACameraCaptureSession* session,
    ACameraCaptureSession_captureCallbacks* callbacks, int numRequests,
    ACaptureRequest** requests, int* captureSequenceId) {
  (void)callbacks;
  if (!session || !requests || numRequests < 1) {
    return ACAMERA_ERROR_INVALID_PARAMETER;
  }

  ANativeWindow* window = nullptr;
  for (ACameraOutputTarget* target : requests[0]->targets) {
    if (target && target->window) {
      window = target->window;
      break;
    }
  }
  AImageReader* reader = Roblox_ImageReaderFromWindow(window);
  if (!reader) reader = session->reader;
  if (!reader) {
    std::fprintf(stderr,
                 "[camera2ndk] setRepeatingRequest: no AImageReader target\n");
    return ACAMERA_ERROR_INVALID_PARAMETER;
  }

  if (!(session->reader == reader && session->device->running)) {
    if (!StartCapture(session->device.get(), reader)) {
      return ACAMERA_ERROR_STREAM_CONFIGURE_FAIL;
    }
    session->reader = reader;
  }
  if (session->callbacks.onActive) {
    session->callbacks.onActive(session->callbacks.context, session);
  }
  if (captureSequenceId) *captureSequenceId = 1;
  return ACAMERA_OK;
}

camera_status_t ACameraCaptureSession_capture(
    ACameraCaptureSession* session,
    ACameraCaptureSession_captureCallbacks* callbacks, int numRequests,
    ACaptureRequest** requests, int* captureSequenceId) {
  (void)callbacks;
  if (!session || !requests || numRequests < 1) {
    return ACAMERA_ERROR_INVALID_PARAMETER;
  }

  ANativeWindow* window = nullptr;
  for (ACameraOutputTarget* target : requests[0]->targets) {
    if (target && target->window) {
      window = target->window;
      break;
    }
  }
  AImageReader* reader = Roblox_ImageReaderFromWindow(window);
  if (!reader) reader = session->reader;
  if (!reader) return ACAMERA_ERROR_INVALID_PARAMETER;

  if (!(session->reader == reader && session->device->running)) {
    if (!StartCapture(session->device.get(), reader)) {
      return ACAMERA_ERROR_STREAM_CONFIGURE_FAIL;
    }
    session->reader = reader;
  }
  if (session->callbacks.onActive) {
    session->callbacks.onActive(session->callbacks.context, session);
  }
  if (captureSequenceId) *captureSequenceId = 1;
  return ACAMERA_OK;
}

camera_status_t ACameraCaptureSession_stopRepeating(
    ACameraCaptureSession* session) {
  if (!session) return ACAMERA_ERROR_INVALID_PARAMETER;
  StopCapture(session->device.get());
  return ACAMERA_OK;
}

camera_status_t ACameraCaptureSession_abortCaptures(
    ACameraCaptureSession* session) {
  if (!session) return ACAMERA_ERROR_INVALID_PARAMETER;
  StopCapture(session->device.get());
  return ACAMERA_OK;
}

void ACameraCaptureSession_close(ACameraCaptureSession* session) {
  if (!session) return;
  if (session->alive) session->alive->store(false);
  StopCapture(session->device.get());
  if (session->callbacks.onClosed) {
    session->callbacks.onClosed(session->callbacks.context, session);
  }
  delete session;
}

camera_status_t ACameraMetadata_getConstEntry(
    const ACameraMetadata* metadata, uint32_t tag,
    ACameraMetadata_const_entry* entry) {
  if (!metadata || !entry) return ACAMERA_ERROR_INVALID_PARAMETER;
  auto it = metadata->entries.find(tag);
  if (it == metadata->entries.end()) {
    return ACAMERA_ERROR_METADATA_NOT_FOUND;
  }
  entry->tag = tag;
  entry->type = it->second.type;
  entry->count = it->second.count;
  entry->data.u8 = it->second.bytes.data();
  return ACAMERA_OK;
}

camera_status_t ACameraMetadata_getAllTags(const ACameraMetadata* metadata,
                                           int32_t* numEntries,
                                           const uint32_t** tags) {
  if (!metadata || !numEntries || !tags) {
    return ACAMERA_ERROR_INVALID_PARAMETER;
  }
  *numEntries = static_cast<int32_t>(metadata->tags.size());
  *tags = metadata->tags.data();
  return ACAMERA_OK;
}

void ACameraMetadata_free(ACameraMetadata* metadata) { delete metadata; }

}  // extern "C"
