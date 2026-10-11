#ifndef MOCKTAIL_AUDIO_OPENSL_PLAYBACK_RUNTIME_H_
#define MOCKTAIL_AUDIO_OPENSL_PLAYBACK_RUNTIME_H_

#include <cstdint>

#include "mocktail/audio/opensl_runtime_abi.h"

#if defined(__GNUC__) || defined(__clang__)
#define MOCKTAIL_OPENSL_EXPORT __attribute__((visibility("default")))
#else
#define MOCKTAIL_OPENSL_EXPORT
#endif

extern "C" {

#if !defined(MOCKTAIL_USE_SYSTEM_OPENSL_HEADERS)
extern MOCKTAIL_OPENSL_EXPORT const
    mocktail::audio::opensl_abi::InterfaceId SL_IID_ANDROIDCONFIGURATION;
extern MOCKTAIL_OPENSL_EXPORT const
    mocktail::audio::opensl_abi::InterfaceId SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
extern MOCKTAIL_OPENSL_EXPORT const
    mocktail::audio::opensl_abi::InterfaceId SL_IID_BUFFERQUEUE;
extern MOCKTAIL_OPENSL_EXPORT const
    mocktail::audio::opensl_abi::InterfaceId SL_IID_ENGINE;
extern MOCKTAIL_OPENSL_EXPORT const
    mocktail::audio::opensl_abi::InterfaceId SL_IID_PLAY;
extern MOCKTAIL_OPENSL_EXPORT const
    mocktail::audio::opensl_abi::InterfaceId SL_IID_RECORD;
extern MOCKTAIL_OPENSL_EXPORT const
    mocktail::audio::opensl_abi::InterfaceId SL_IID_VOLUME;

MOCKTAIL_OPENSL_EXPORT mocktail::audio::opensl_abi::Result
MOCKTAIL_OPENSL_API_ENTRY slCreateEngine(
    mocktail::audio::opensl_abi::Object* engine,
    mocktail::audio::opensl_abi::Uint32 num_options,
    const mocktail::audio::opensl_abi::EngineOption* options,
    mocktail::audio::opensl_abi::Uint32 num_interfaces,
    const mocktail::audio::opensl_abi::InterfaceId* interface_ids,
    const mocktail::audio::opensl_abi::Boolean* interface_required);
#endif

}  // extern "C"

#endif  // MOCKTAIL_AUDIO_OPENSL_PLAYBACK_RUNTIME_H_
