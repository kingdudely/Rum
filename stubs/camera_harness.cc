// Standalone smoke test for the V4L2 camera2 shim.
//
// Opens the first camera, starts a repeating capture into an AImageReader,
// grabs one frame and writes its Y plane to /tmp/rum-camera.yuv.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

using camera_status_t = int32_t;

struct ACameraManager;
struct ACameraDevice;
struct ACaptureRequest;
struct ACaptureSessionOutput;
struct ACaptureSessionOutputContainer;
struct ACameraOutputTarget;
struct ACameraCaptureSession;
struct AImageReader;
struct AImage;

struct ACameraIdList {
  int numCameras;
  const char** cameraIds;
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

struct AImageCropRect {
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;
};

extern "C" {
ACameraManager* ACameraManager_create();
void ACameraManager_delete(ACameraManager*);
camera_status_t ACameraManager_getCameraIdList(ACameraManager*, ACameraIdList**);
void ACameraManager_deleteCameraIdList(ACameraIdList*);
camera_status_t ACameraManager_openCamera(ACameraManager*, const char*,
                                          ACameraDevice_StateCallbacks*,
                                          ACameraDevice**);
camera_status_t ACameraDevice_close(ACameraDevice*);
camera_status_t ACameraDevice_createCaptureRequest(const ACameraDevice*, int,
                                                   ACaptureRequest**);
void ACaptureRequest_addTarget(ACaptureRequest*, const ACameraOutputTarget*);
void ACaptureRequest_free(ACaptureRequest*);
camera_status_t ACaptureSessionOutputContainer_create(
    ACaptureSessionOutputContainer**);
void ACaptureSessionOutputContainer_free(ACaptureSessionOutputContainer*);
camera_status_t ACaptureSessionOutputContainer_add(
    ACaptureSessionOutputContainer*, const ACaptureSessionOutput*);
camera_status_t ACaptureSessionOutput_create(void*, ACaptureSessionOutput**);
void ACaptureSessionOutput_free(ACaptureSessionOutput*);
camera_status_t ACameraOutputTarget_create(void*, ACameraOutputTarget**);
void ACameraOutputTarget_free(ACameraOutputTarget*);
camera_status_t ACameraDevice_createCaptureSession(
    ACameraDevice*, const ACaptureSessionOutputContainer*,
    const ACameraCaptureSession_stateCallbacks*, ACameraCaptureSession**);
camera_status_t ACameraCaptureSession_setRepeatingRequest(
    ACameraCaptureSession*, void*, int, ACaptureRequest**, int*);
camera_status_t ACameraCaptureSession_stopRepeating(ACameraCaptureSession*);
void ACameraCaptureSession_close(ACameraCaptureSession*);

int32_t AImageReader_new(int32_t width, int32_t height, int32_t format,
                         int32_t maxImages, AImageReader**);
void AImageReader_delete(AImageReader*);
int32_t AImageReader_getWindow(AImageReader*, void**);
int32_t AImageReader_acquireLatestImage(AImageReader*, AImage**);
void AImage_delete(AImage*);
int32_t AImage_getWidth(const AImage*, int32_t*);
int32_t AImage_getHeight(const AImage*, int32_t*);
int32_t AImage_getFormat(const AImage*, int32_t*);
int32_t AImage_getNumberOfPlanes(const AImage*, int32_t*);
int32_t AImage_getPlaneData(const AImage*, int, uint8_t**, int*);
int32_t AImage_getPlaneRowStride(const AImage*, int, int32_t*);
int32_t AImage_getPlanePixelStride(const AImage*, int, int32_t*);
int32_t AImage_getTimestamp(const AImage*, int64_t*);
int32_t AImage_getCropRect(const AImage*, AImageCropRect*);
}

int main() {
  ACameraManager* manager = ACameraManager_create();
  if (!manager) {
    std::fprintf(stderr, "harness: manager create failed\n");
    return 1;
  }

  ACameraIdList* ids = nullptr;
  if (ACameraManager_getCameraIdList(manager, &ids) != 0 || !ids ||
      ids->numCameras == 0) {
    std::fprintf(stderr, "harness: no cameras\n");
    return 2;
  }
  std::printf("harness: %d camera(s), first id=%s\n", ids->numCameras,
              ids->cameraIds[0]);

  ACameraDevice_StateCallbacks deviceCallbacks{};
  ACameraDevice* device = nullptr;
  if (ACameraManager_openCamera(manager, ids->cameraIds[0], &deviceCallbacks,
                                &device) != 0) {
    std::fprintf(stderr, "harness: open failed\n");
    return 3;
  }

  constexpr int32_t kWidth = 640;
  constexpr int32_t kHeight = 480;
  constexpr int32_t kYuv420 = 0x23;
  AImageReader* reader = nullptr;
  if (AImageReader_new(kWidth, kHeight, kYuv420, 4, &reader) != 0) {
    std::fprintf(stderr, "harness: reader failed\n");
    return 4;
  }
  void* window = nullptr;
  if (AImageReader_getWindow(reader, &window) != 0) {
    std::fprintf(stderr, "harness: window failed\n");
    return 5;
  }

  ACaptureSessionOutputContainer* container = nullptr;
  ACaptureSessionOutput* output = nullptr;
  ACameraOutputTarget* target = nullptr;
  ACaptureRequest* request = nullptr;
  ACameraCaptureSession* session = nullptr;
  ACameraCaptureSession_stateCallbacks sessionCallbacks{};

  ACaptureSessionOutputContainer_create(&container);
  ACaptureSessionOutput_create(window, &output);
  ACaptureSessionOutputContainer_add(container, output);
  ACameraOutputTarget_create(window, &target);
  ACameraDevice_createCaptureRequest(device, 1, &request);
  ACaptureRequest_addTarget(request, target);
  if (ACameraDevice_createCaptureSession(device, container, &sessionCallbacks,
                                         &session) != 0) {
    std::fprintf(stderr, "harness: session failed\n");
    return 6;
  }
  if (ACameraCaptureSession_setRepeatingRequest(session, nullptr, 1, &request,
                                                nullptr) != 0) {
    std::fprintf(stderr, "harness: repeating request failed\n");
    return 7;
  }

  timespec start{};
  clock_gettime(CLOCK_MONOTONIC, &start);
  AImage* image = nullptr;
  while (!image) {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec - start.tv_sec > 3) break;
    if (AImageReader_acquireLatestImage(reader, &image) != 0) {
      timespec pause{0, 20000000};
      nanosleep(&pause, nullptr);
    }
  }
  if (!image) {
    std::fprintf(stderr, "harness: no frame after 3s\n");
    return 8;
  }

  int32_t width = 0, height = 0, format = 0, planes = 0;
  int64_t timestamp = 0;
  AImage_getWidth(image, &width);
  AImage_getHeight(image, &height);
  AImage_getFormat(image, &format);
  AImage_getNumberOfPlanes(image, &planes);
  AImage_getTimestamp(image, &timestamp);
  std::printf("harness: frame %dx%d format=0x%x planes=%d ts=%lld\n", width,
              height, format, planes, static_cast<long long>(timestamp));

  uint8_t* data = nullptr;
  int length = 0;
  int32_t rowStride = 0, pixelStride = 0;
  if (AImage_getPlaneData(image, 0, &data, &length) == 0) {
    AImage_getPlaneRowStride(image, 0, &rowStride);
    AImage_getPlanePixelStride(image, 0, &pixelStride);
    std::printf("harness: plane0 len=%d stride=%d pixel=%d\n", length,
                rowStride, pixelStride);
    FILE* file = std::fopen("/tmp/rum-camera.yuv", "wb");
    if (file) {
      std::fwrite(data, 1, static_cast<size_t>(length), file);
      std::fclose(file);
    }
  }

  AImage_delete(image);
  ACameraCaptureSession_stopRepeating(session);
  ACameraCaptureSession_close(session);
  ACaptureRequest_free(request);
  ACameraOutputTarget_free(target);
  ACaptureSessionOutput_free(output);
  ACaptureSessionOutputContainer_free(container);
  AImageReader_delete(reader);
  ACameraDevice_close(device);
  ACameraManager_deleteCameraIdList(ids);
  ACameraManager_delete(manager);
  std::printf("harness: ok\n");
  return 0;
}
