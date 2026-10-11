// Stub-inert OpenSL ES entry points for libOpenSLES.so. The engine drives
// all audio through the FMOD/WebRTC JNI bridges; a full join+frame run
// never creates an OpenSL engine object (verified by probe). These exports
// exist so the guest's imports resolve, and report failure instead of
// faking a working audio device.
#include "roblox/audio/opensl_playback_runtime.h"

namespace roblox::audio {

#if !defined(ROBLOX_USE_SYSTEM_OPENSL_HEADERS)
namespace abi = opensl_abi;

constexpr abi::InterfaceIdValue kAndroidConfigurationId = {
    0x89f6a7e0, 0xbeac, 0x11df, 0x8b5c,
    {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
constexpr abi::InterfaceIdValue kAndroidSimpleBufferQueueId = {
    0x198e4940, 0xc5d7, 0x11df, 0xa2a6,
    {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
constexpr abi::InterfaceIdValue kBufferQueueId = {
    0x2bc99cc0, 0xddd4, 0x11db, 0x8d99,
    {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
constexpr abi::InterfaceIdValue kEngineId = {
    0x8d97c260, 0xddd4, 0x11db, 0x958f,
    {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
constexpr abi::InterfaceIdValue kPlayId = {
    0xef0bd9c0, 0xddd7, 0x11db, 0xbf49,
    {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
constexpr abi::InterfaceIdValue kRecordId = {
    0xc5657aa0, 0xdddb, 0x11db, 0x82f7,
    {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
constexpr abi::InterfaceIdValue kVolumeId = {
    0x09e8ede0, 0xddde, 0x11db, 0xb4f6,
    {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};

extern "C" {

ROBLOX_OPENSL_EXPORT extern const
    roblox::audio::opensl_abi::InterfaceId SL_IID_ANDROIDCONFIGURATION =
        &roblox::audio::kAndroidConfigurationId;
ROBLOX_OPENSL_EXPORT extern const
    roblox::audio::opensl_abi::InterfaceId SL_IID_ANDROIDSIMPLEBUFFERQUEUE =
        &roblox::audio::kAndroidSimpleBufferQueueId;
ROBLOX_OPENSL_EXPORT extern const
    roblox::audio::opensl_abi::InterfaceId SL_IID_BUFFERQUEUE =
        &roblox::audio::kBufferQueueId;
ROBLOX_OPENSL_EXPORT extern const
    roblox::audio::opensl_abi::InterfaceId SL_IID_ENGINE =
        &roblox::audio::kEngineId;
ROBLOX_OPENSL_EXPORT extern const
    roblox::audio::opensl_abi::InterfaceId SL_IID_PLAY =
        &roblox::audio::kPlayId;
ROBLOX_OPENSL_EXPORT extern const
    roblox::audio::opensl_abi::InterfaceId SL_IID_RECORD =
        &roblox::audio::kRecordId;
ROBLOX_OPENSL_EXPORT extern const
    roblox::audio::opensl_abi::InterfaceId SL_IID_VOLUME =
        &roblox::audio::kVolumeId;

#endif

ROBLOX_OPENSL_EXPORT roblox::audio::opensl_abi::Result
ROBLOX_OPENSL_API_ENTRY slCreateEngine(
    roblox::audio::opensl_abi::Object* engine,
    roblox::audio::opensl_abi::Uint32,
    const roblox::audio::opensl_abi::EngineOption*,
    roblox::audio::opensl_abi::Uint32,
    const roblox::audio::opensl_abi::InterfaceId*,
    const roblox::audio::opensl_abi::Boolean*) {
  namespace abi = roblox::audio::opensl_abi;
  if (engine == nullptr) {
    return abi::kResultParameterInvalid;
  }
  *engine = nullptr;
  return abi::kResultFeatureUnsupported;
}

ROBLOX_OPENSL_EXPORT roblox::audio::opensl_abi::Result
ROBLOX_OPENSL_API_ENTRY slQueryNumSupportedEngineInterfaces(
    roblox::audio::opensl_abi::Uint32* count) {
  if (count == nullptr) {
    return roblox::audio::opensl_abi::kResultParameterInvalid;
  }
  *count = 1;
  return roblox::audio::opensl_abi::kResultSuccess;
}

ROBLOX_OPENSL_EXPORT roblox::audio::opensl_abi::Result
ROBLOX_OPENSL_API_ENTRY slQuerySupportedEngineInterfaces(
    roblox::audio::opensl_abi::Uint32 index,
    roblox::audio::opensl_abi::InterfaceId* iid) {
  namespace abi = roblox::audio::opensl_abi;
  if (iid == nullptr) {
    return abi::kResultParameterInvalid;
  }
  *iid = index == 0 ? SL_IID_ENGINE : nullptr;
  return *iid != nullptr ? abi::kResultSuccess
                         : abi::kResultParameterInvalid;
}

}  // extern "C"

}  // namespace roblox::audio
