// Stub-inert OpenMAX AL entry points for libOpenMAXAL.so. A full join+frame
// run never creates an engine object; audio flows through the FMOD/WebRTC
// JNI bridges. These exports exist so the guest's imports resolve, and
// report failure instead of faking success.

#include <cstdint>

using SLresult = uint32_t;
using SLInterfaceID = const void*;

static constexpr SLresult SL_RESULT_PARAMETER_INVALID = 2;
static constexpr SLresult SL_RESULT_FEATURE_UNSUPPORTED = 12;

extern "C" {

void* SL_IID_ANDROIDCONFIGURATION = reinterpret_cast<void*>(0x1001);
void* SL_IID_ANDROIDSIMPLEBUFFERQUEUE = reinterpret_cast<void*>(0x1002);
void* SL_IID_BUFFERQUEUE = reinterpret_cast<void*>(0x1003);
void* SL_IID_ENGINE = reinterpret_cast<void*>(0x1004);
void* SL_IID_PLAY = reinterpret_cast<void*>(0x1005);
void* SL_IID_RECORD = reinterpret_cast<void*>(0x1006);
void* SL_IID_VOLUME = reinterpret_cast<void*>(0x1007);

SLresult slCreateEngine(void** pEngine, uint32_t,
                         const void*,
                         uint32_t,
                         const void*,
                         const void*) {
  if (pEngine == nullptr) {
    return SL_RESULT_PARAMETER_INVALID;
  }
  *pEngine = nullptr;
  return SL_RESULT_FEATURE_UNSUPPORTED;
}

SLresult alCreateEngine(void** pEngine, uint32_t,
                         const void*,
                         uint32_t,
                         const void*,
                         const void*) {
  if (pEngine == nullptr) {
    return SL_RESULT_PARAMETER_INVALID;
  }
  *pEngine = nullptr;
  return SL_RESULT_FEATURE_UNSUPPORTED;
}

}  // extern "C"
