#ifndef ROBLOX_AUDIO_OPENSL_PLAYBACK_RUNTIME_H_
#define ROBLOX_AUDIO_OPENSL_PLAYBACK_RUNTIME_H_

#include <cstdint>

#include "roblox/audio/opensl_runtime_abi.h"

#if defined(__GNUC__) || defined(__clang__)
#define ROBLOX_OPENSL_EXPORT __attribute__((visibility("default")))
#else
#define ROBLOX_OPENSL_EXPORT
#endif

extern "C" {

#if !defined(ROBLOX_USE_SYSTEM_OPENSL_HEADERS)
extern ROBLOX_OPENSL_EXPORT const
    roblox::audio::opensl_abi::InterfaceId SL_IID_ANDROIDCONFIGURATION;
extern ROBLOX_OPENSL_EXPORT const
    roblox::audio::opensl_abi::InterfaceId SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
extern ROBLOX_OPENSL_EXPORT const
    roblox::audio::opensl_abi::InterfaceId SL_IID_BUFFERQUEUE;
extern ROBLOX_OPENSL_EXPORT const
    roblox::audio::opensl_abi::InterfaceId SL_IID_ENGINE;
extern ROBLOX_OPENSL_EXPORT const
    roblox::audio::opensl_abi::InterfaceId SL_IID_PLAY;
extern ROBLOX_OPENSL_EXPORT const
    roblox::audio::opensl_abi::InterfaceId SL_IID_RECORD;
extern ROBLOX_OPENSL_EXPORT const
    roblox::audio::opensl_abi::InterfaceId SL_IID_VOLUME;

ROBLOX_OPENSL_EXPORT roblox::audio::opensl_abi::Result
ROBLOX_OPENSL_API_ENTRY slCreateEngine(
    roblox::audio::opensl_abi::Object* engine,
    roblox::audio::opensl_abi::Uint32 num_options,
    const roblox::audio::opensl_abi::EngineOption* options,
    roblox::audio::opensl_abi::Uint32 num_interfaces,
    const roblox::audio::opensl_abi::InterfaceId* interface_ids,
    const roblox::audio::opensl_abi::Boolean* interface_required);
#endif

}  // extern "C"

#endif  // ROBLOX_AUDIO_OPENSL_PLAYBACK_RUNTIME_H_
