#include "jnivm/jnivm.h"

#include "jni_helpers.h"
#include "jni_references.h"
#include "jni_fields.h"
#include "jni_strings.h"

#include "mocktail/audio/fmod_thread_floating_point.h"
#include "mocktail/platform/posix_primitives.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <list>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/display_size.h"


namespace jnivm {

using namespace internal;

// FMOD and WebRTC audio callbacks.

void VM::SetFmodAudioDeviceCallbacks(
    std::shared_ptr<void> context,
    const FmodAudioDeviceCallbacks& callbacks) {
  FmodAudioDeviceBinding old_binding;
  {
    std::lock_guard<std::mutex> lock(fmod_audio_device_mutex_);
    old_binding = std::move(fmod_audio_device_binding_);
    fmod_audio_device_binding_.context = std::move(context);
    fmod_audio_device_binding_.callbacks = callbacks;
  }
  if (old_binding.context != nullptr &&
      old_binding.callbacks.shutdown != nullptr) {
    old_binding.callbacks.shutdown(old_binding.context.get());
  }
}

void VM::ClearFmodAudioDeviceCallbacks() {
  FmodAudioDeviceBinding old_binding;
  {
    std::lock_guard<std::mutex> lock(fmod_audio_device_mutex_);
    old_binding = std::move(fmod_audio_device_binding_);
    fmod_audio_device_binding_ = {};
  }
  if (old_binding.context != nullptr &&
      old_binding.callbacks.shutdown != nullptr) {
    old_binding.callbacks.shutdown(old_binding.context.get());
  }
}

bool VM::DispatchFmodAudioDeviceInit(const void* identity, int channels,
                                     int sample_rate_hz,
                                     int block_size_frames,
                                     int block_count) {
  FmodAudioDeviceBinding binding;
  {
    std::lock_guard<std::mutex> lock(fmod_audio_device_mutex_);
    binding = fmod_audio_device_binding_;
  }
  return binding.context != nullptr && binding.callbacks.init != nullptr &&
         binding.callbacks.init(binding.context.get(), identity, channels,
                                sample_rate_hz, block_size_frames,
                                block_count);
}

bool VM::DispatchFmodAudioDeviceWrite(const void* identity,
                                      const std::uint8_t* data,
                                      std::size_t size) {
  FmodAudioDeviceBinding binding;
  {
    std::lock_guard<std::mutex> lock(fmod_audio_device_mutex_);
    binding = fmod_audio_device_binding_;
  }
  return binding.context != nullptr && binding.callbacks.write != nullptr &&
         binding.callbacks.write(binding.context.get(), identity, data, size);
}

bool VM::DispatchFmodAudioDeviceClose(const void* identity) {
  FmodAudioDeviceBinding binding;
  {
    std::lock_guard<std::mutex> lock(fmod_audio_device_mutex_);
    binding = fmod_audio_device_binding_;
  }
  return binding.context != nullptr && binding.callbacks.close != nullptr &&
         binding.callbacks.close(binding.context.get(), identity);
}

namespace {

void OnWebRtcAudioRecordData(void *context, const void *identity,
                             std::size_t size_bytes) {
  auto *vm = static_cast<VM *>(context);
  if (vm != nullptr) {
    vm->DispatchWebRtcAudioRecordData(identity, size_bytes);
  }
}

} // namespace

void VM::SetWebRtcAudioManagerCallbacks(
    std::shared_ptr<void> context,
    const WebRtcAudioManagerCallbacks& callbacks) {
  WebRtcAudioManagerBinding previous;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_manager_mutex_);
    previous = std::move(webrtc_audio_manager_binding_);
    webrtc_audio_manager_binding_ = {std::move(context), callbacks};
  }
}

void VM::ClearWebRtcAudioManagerCallbacks() {
  WebRtcAudioManagerBinding previous;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_manager_mutex_);
    previous = std::move(webrtc_audio_manager_binding_);
    webrtc_audio_manager_binding_ = {};
  }
}

void VM::RegisterWebRtcAudioManagerNative(const char* name,
                                          const char* signature,
                                          void* function) {
  if (name == nullptr || signature == nullptr || function == nullptr ||
      std::strcmp(name, "nativeCacheAudioParameters") != 0 ||
      std::strcmp(signature, "(IIIZZZZZZZIIJ)V") != 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(webrtc_audio_manager_mutex_);
  webrtc_cache_audio_parameters_native_ = function;
}

bool VM::DispatchWebRtcAudioManagerConstruct(jobject manager,
                                             jlong native_audio_manager) {
  const auto fail = [](const char* reason) {
    std::cerr << "  [mocktail][audio] WebRTC audio manager parameters_failed "
              << "reason=" << reason << '\n';
    return false;
  };
  if (ObjectClassName(manager) != "org/webrtc/voiceengine/WebRtcAudioManager") {
    return fail("invalid receiver");
  }
  SetLongFieldRaw(manager, "nativeAudioManager", native_audio_manager);
  SetBooleanFieldRaw(manager, "initialized", JNI_FALSE);
  SetBooleanFieldRaw(manager, "mocktailAudioParametersCached", JNI_FALSE);
  WebRtcAudioManagerBinding binding;
  void* native = nullptr;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_manager_mutex_);
    binding = webrtc_audio_manager_binding_;
    native = webrtc_cache_audio_parameters_native_;
  }
  if (native == nullptr || native_audio_manager == 0) {
    return fail("missing nativeCacheAudioParameters or native handle");
  }
  WebRtcAudioManagerParameters parameters;
  if (binding.context == nullptr ||
      binding.callbacks.get_parameters == nullptr ||
      !binding.callbacks.get_parameters(binding.context.get(), &parameters)) {
    return fail("host audio manager unavailable");
  }
  if (parameters.sample_rate_hz <= 0 || parameters.output_channels < 1 ||
      parameters.output_channels > 2 || parameters.input_channels < 1 ||
      parameters.input_channels > 2 ||
      parameters.output_buffer_size_frames <= 0 ||
      parameters.input_buffer_size_frames <= 0) {
    return fail("invalid host PCM parameters");
  }
  SetIntFieldRaw(manager, "sampleRate", parameters.sample_rate_hz);
  SetIntFieldRaw(manager, "outputChannels", parameters.output_channels);
  SetIntFieldRaw(manager, "inputChannels", parameters.input_channels);
  SetIntFieldRaw(manager, "outputBufferSize",
                 parameters.output_buffer_size_frames);
  SetIntFieldRaw(manager, "inputBufferSize",
                 parameters.input_buffer_size_frames);
  SetBooleanFieldRaw(manager, "lowLatencyOutput",
                     parameters.low_latency_output);
  SetBooleanFieldRaw(manager, "lowLatencyInput", parameters.low_latency_input);
  SetBooleanFieldRaw(manager, "proAudio", parameters.pro_audio);
  SetBooleanFieldRaw(manager, "aAudio", parameters.aaudio);

  // Execute the constructor's Java -> native callback before returning the
  // object. The guest needs these formats even before it calls init(). No VM
  // locks are held here: the native callback may make nested JNI calls.
  using CacheAudioParameters = void(JNICALL*)(
      JNIEnv*, jobject, jint, jint, jint, jboolean, jboolean, jboolean,
      jboolean, jboolean, jboolean, jboolean, jint, jint, jlong);
  reinterpret_cast<CacheAudioParameters>(native)(
      GetJNIEnv(), manager, parameters.sample_rate_hz,
      parameters.output_channels, parameters.input_channels,
      parameters.hardware_aec, parameters.hardware_agc, parameters.hardware_ns,
      parameters.low_latency_output, parameters.low_latency_input,
      parameters.pro_audio, parameters.aaudio,
      parameters.output_buffer_size_frames, parameters.input_buffer_size_frames,
      native_audio_manager);
  SetBooleanFieldRaw(manager, "mocktailAudioParametersCached", JNI_TRUE);
  std::cout << "  [mocktail][audio] WebRTC audio manager parameters "
            << "sample_rate_hz=" << parameters.sample_rate_hz
            << " output_channels=" << parameters.output_channels
            << " input_channels=" << parameters.input_channels << '\n'
            << std::flush;
  return true;
}

bool VM::DispatchWebRtcAudioManagerInit(jobject manager) {
  WebRtcAudioManagerBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_manager_mutex_);
    binding = webrtc_audio_manager_binding_;
  }
  const bool initialized =
      ObjectClassName(manager) == "org/webrtc/voiceengine/WebRtcAudioManager" &&
      BooleanFieldValue(manager, "mocktailAudioParametersCached") == JNI_TRUE &&
      binding.context != nullptr && binding.callbacks.init != nullptr &&
      (BooleanFieldValue(manager, "initialized") == JNI_TRUE ||
       binding.callbacks.init(binding.context.get(), manager));
  SetBooleanFieldRaw(manager, "initialized",
                     initialized ? JNI_TRUE : JNI_FALSE);
  if (!initialized) {
    std::cerr << "  [mocktail][audio] WebRTC audio manager init_failed "
                 "reason=parameters or host manager unavailable\n";
  }
  return initialized;
}

void VM::DispatchWebRtcAudioManagerDispose(jobject manager) {
  if (ObjectClassName(manager) != "org/webrtc/voiceengine/WebRtcAudioManager" ||
      BooleanFieldValue(manager, "initialized") != JNI_TRUE) {
    return;
  }
  SetBooleanFieldRaw(manager, "initialized", JNI_FALSE);
  WebRtcAudioManagerBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_manager_mutex_);
    binding = webrtc_audio_manager_binding_;
  }
  if (binding.context != nullptr && binding.callbacks.dispose != nullptr) {
    binding.callbacks.dispose(binding.context.get(), manager);
  }
}

void VM::DispatchWebRtcAudioManagerMicrophoneMute(jobject manager, bool muted) {
  if (ObjectClassName(manager) != "org/webrtc/voiceengine/WebRtcAudioManager" ||
      BooleanFieldValue(manager, "mocktailAudioParametersCached") != JNI_TRUE) {
    return;
  }
  WebRtcAudioManagerBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_manager_mutex_);
    binding = webrtc_audio_manager_binding_;
  }
  if (binding.context != nullptr &&
      binding.callbacks.set_microphone_mute != nullptr) {
    binding.callbacks.set_microphone_mute(binding.context.get(), muted);
  }
}

void VM::SetWebRtcAudioRecordCallbacks(
    std::shared_ptr<void> context,
    const WebRtcAudioRecordCallbacks &callbacks) {
  WebRtcAudioRecordBinding old_binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_record_mutex_);
    old_binding = std::move(webrtc_audio_record_binding_);
    webrtc_audio_record_binding_.context = std::move(context);
    webrtc_audio_record_binding_.callbacks = callbacks;
  }
  if (old_binding.context != nullptr &&
      old_binding.callbacks.shutdown != nullptr) {
    old_binding.callbacks.shutdown(old_binding.context.get());
  }
}

void VM::ClearWebRtcAudioRecordCallbacks() {
  WebRtcAudioRecordBinding old_binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_record_mutex_);
    old_binding = std::move(webrtc_audio_record_binding_);
    webrtc_audio_record_binding_ = {};
  }
  if (old_binding.context != nullptr &&
      old_binding.callbacks.shutdown != nullptr) {
    old_binding.callbacks.shutdown(old_binding.context.get());
  }
}

int VM::DispatchWebRtcAudioRecordInit(const void *identity, int sample_rate_hz,
                                      int channels, void **direct_buffer,
                                      std::size_t *direct_buffer_capacity) {
  WebRtcAudioRecordBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_record_mutex_);
    binding = webrtc_audio_record_binding_;
  }
  if (binding.context == nullptr || binding.callbacks.init == nullptr) {
    return -1;
  }
  const int frames = binding.callbacks.init(
      binding.context.get(), identity, sample_rate_hz, channels,
      &OnWebRtcAudioRecordData, this, direct_buffer, direct_buffer_capacity);
  if (frames < 0 || direct_buffer == nullptr || *direct_buffer == nullptr ||
      direct_buffer_capacity == nullptr || *direct_buffer_capacity == 0) {
    return -1;
  }

  void *native = nullptr;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_record_native_mutex_);
    native = webrtc_cache_direct_buffer_native_;
  }
  const jlong native_audio_record =
      LongFieldValue(reinterpret_cast<jobject>(const_cast<void *>(identity)),
                     "nativeAudioRecord");
  if (native == nullptr || native_audio_record == 0 ||
      *direct_buffer_capacity >
          static_cast<std::size_t>(std::numeric_limits<jlong>::max())) {
    if (binding.callbacks.close != nullptr) {
      binding.callbacks.close(binding.context.get(), identity);
    }
    return -1;
  }
  JNIEnv *env = GetJNIEnv();
  jobject byte_buffer = env->NewDirectByteBuffer(
      *direct_buffer, static_cast<jlong>(*direct_buffer_capacity));
  if (byte_buffer == nullptr) {
    if (binding.callbacks.close != nullptr) {
      binding.callbacks.close(binding.context.get(), identity);
    }
    return -1;
  }
  using CacheDirectBuffer = void(JNICALL *)(JNIEnv *, jobject, jobject, jlong);
  reinterpret_cast<CacheDirectBuffer>(native)(
      env, reinterpret_cast<jobject>(const_cast<void *>(identity)), byte_buffer,
      native_audio_record);
  return frames;
}

bool VM::DispatchWebRtcAudioRecordStart(const void *identity) {
  WebRtcAudioRecordBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_record_mutex_);
    binding = webrtc_audio_record_binding_;
  }
  return binding.context != nullptr && binding.callbacks.start != nullptr &&
         binding.callbacks.start(binding.context.get(), identity);
}

bool VM::DispatchWebRtcAudioRecordStop(const void *identity) {
  WebRtcAudioRecordBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_record_mutex_);
    binding = webrtc_audio_record_binding_;
  }
  return binding.context != nullptr && binding.callbacks.stop != nullptr &&
         binding.callbacks.stop(binding.context.get(), identity);
}

void VM::DispatchWebRtcAudioRecordClose(const void *identity) {
  WebRtcAudioRecordBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_record_mutex_);
    binding = webrtc_audio_record_binding_;
  }
  if (binding.context != nullptr && binding.callbacks.close != nullptr) {
    binding.callbacks.close(binding.context.get(), identity);
  }
}

void VM::RegisterWebRtcAudioRecordNative(const char *name,
                                         const char *signature,
                                         void *function) {
  if (name == nullptr || signature == nullptr || function == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(webrtc_audio_record_native_mutex_);
  if (std::strcmp(name, "nativeCacheDirectBufferAddress") == 0 &&
      std::strcmp(signature, "(Ljava/nio/ByteBuffer;J)V") == 0) {
    webrtc_cache_direct_buffer_native_ = function;
  } else if (std::strcmp(name, "nativeDataIsRecorded") == 0 &&
             std::strcmp(signature, "(IJ)V") == 0) {
    webrtc_data_is_recorded_native_ = function;
  }
}

void VM::DispatchWebRtcAudioRecordData(const void *identity,
                                       std::size_t size_bytes) {
  void *native = nullptr;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_record_native_mutex_);
    native = webrtc_data_is_recorded_native_;
  }
  if (native == nullptr || identity == nullptr ||
      size_bytes > static_cast<std::size_t>(std::numeric_limits<jint>::max())) {
    return;
  }
  const jlong native_audio_record =
      LongFieldValue(reinterpret_cast<jobject>(const_cast<void *>(identity)),
                     "nativeAudioRecord");
  if (native_audio_record == 0) {
    return;
  }
  using DataIsRecorded = void(JNICALL *)(JNIEnv *, jobject, jint, jlong);
  reinterpret_cast<DataIsRecorded>(native)(
      GetJNIEnv(), reinterpret_cast<jobject>(const_cast<void *>(identity)),
      static_cast<jint>(size_bytes), native_audio_record);
}

namespace {

void OnWebRtcAudioTrackData(void *context, const void *identity,
                            std::size_t size_bytes) {
  auto *vm = static_cast<VM *>(context);
  if (vm != nullptr) {
    vm->DispatchWebRtcAudioTrackData(identity, size_bytes);
  }
}

} // namespace

void VM::SetWebRtcAudioTrackCallbacks(
    std::shared_ptr<void> context, const WebRtcAudioTrackCallbacks &callbacks) {
  std::lock_guard<std::mutex> lock(webrtc_audio_track_mutex_);
  webrtc_audio_track_binding_.context = std::move(context);
  webrtc_audio_track_binding_.callbacks = callbacks;
}

void VM::ClearWebRtcAudioTrackCallbacks() {
  std::lock_guard<std::mutex> lock(webrtc_audio_track_mutex_);
  webrtc_audio_track_binding_ = {};
}

int VM::DispatchWebRtcAudioTrackInit(const void *identity, int sample_rate_hz,
                                     int channels, double buffer_size_factor,
                                     void **direct_buffer,
                                     std::size_t *direct_buffer_capacity) {
  WebRtcAudioTrackBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_track_mutex_);
    binding = webrtc_audio_track_binding_;
  }
  if (binding.context == nullptr || binding.callbacks.init == nullptr ||
      direct_buffer == nullptr || direct_buffer_capacity == nullptr) {
    return -1;
  }
  const int buffer_size_bytes = binding.callbacks.init(
      binding.context.get(), identity, sample_rate_hz, channels,
      buffer_size_factor, &OnWebRtcAudioTrackData, this, direct_buffer,
      direct_buffer_capacity);
  if (buffer_size_bytes < 0 || *direct_buffer == nullptr ||
      *direct_buffer_capacity == 0 ||
      *direct_buffer_capacity >
          static_cast<std::size_t>(std::numeric_limits<jlong>::max())) {
    if (binding.callbacks.close != nullptr) {
      binding.callbacks.close(binding.context.get(), identity);
    }
    return -1;
  }

  void *native = nullptr;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_track_native_mutex_);
    native = webrtc_track_cache_direct_buffer_native_;
  }
  const jlong native_audio_track =
      LongFieldValue(reinterpret_cast<jobject>(const_cast<void *>(identity)),
                     "nativeAudioTrack");
  if (native == nullptr || native_audio_track == 0) {
    if (binding.callbacks.close != nullptr) {
      binding.callbacks.close(binding.context.get(), identity);
    }
    return -1;
  }

  JNIEnv *env = GetJNIEnv();
  jobject byte_buffer = env->NewDirectByteBuffer(
      *direct_buffer, static_cast<jlong>(*direct_buffer_capacity));
  if (byte_buffer == nullptr) {
    if (binding.callbacks.close != nullptr) {
      binding.callbacks.close(binding.context.get(), identity);
    }
    return -1;
  }
  using CacheDirectBuffer = void(JNICALL *)(JNIEnv *, jobject, jobject, jlong);
  reinterpret_cast<CacheDirectBuffer>(native)(
      env, reinterpret_cast<jobject>(const_cast<void *>(identity)), byte_buffer,
      native_audio_track);
  return buffer_size_bytes;
}

int VM::DispatchWebRtcAudioTrackBufferSizeFrames(const void *identity) {
  WebRtcAudioTrackBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_track_mutex_);
    binding = webrtc_audio_track_binding_;
  }
  return binding.context != nullptr &&
                 binding.callbacks.buffer_size_frames != nullptr
             ? binding.callbacks.buffer_size_frames(binding.context.get(),
                                                    identity)
             : 0;
}

bool VM::DispatchWebRtcAudioTrackStart(const void *identity) {
  WebRtcAudioTrackBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_track_mutex_);
    binding = webrtc_audio_track_binding_;
  }
  return binding.context != nullptr && binding.callbacks.start != nullptr &&
         binding.callbacks.start(binding.context.get(), identity);
}

bool VM::DispatchWebRtcAudioTrackStop(const void *identity) {
  WebRtcAudioTrackBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_track_mutex_);
    binding = webrtc_audio_track_binding_;
  }
  return binding.context != nullptr && binding.callbacks.stop != nullptr &&
         binding.callbacks.stop(binding.context.get(), identity);
}

void VM::DispatchWebRtcAudioTrackClose(const void *identity) {
  WebRtcAudioTrackBinding binding;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_track_mutex_);
    binding = webrtc_audio_track_binding_;
  }
  if (binding.context != nullptr && binding.callbacks.close != nullptr) {
    binding.callbacks.close(binding.context.get(), identity);
  }
}

void VM::RegisterWebRtcAudioTrackNative(const char *name, const char *signature,
                                        void *function) {
  if (name == nullptr || signature == nullptr || function == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(webrtc_audio_track_native_mutex_);
  if (std::strcmp(name, "nativeCacheDirectBufferAddress") == 0 &&
      std::strcmp(signature, "(Ljava/nio/ByteBuffer;J)V") == 0) {
    webrtc_track_cache_direct_buffer_native_ = function;
  } else if (std::strcmp(name, "nativeGetPlayoutData") == 0 &&
             std::strcmp(signature, "(IJ)V") == 0) {
    webrtc_get_playout_data_native_ = function;
  }
}

void VM::DispatchWebRtcAudioTrackData(const void *identity,
                                      std::size_t size_bytes) {
  void *native = nullptr;
  {
    std::lock_guard<std::mutex> lock(webrtc_audio_track_native_mutex_);
    native = webrtc_get_playout_data_native_;
  }
  if (native == nullptr || identity == nullptr ||
      size_bytes > static_cast<std::size_t>(std::numeric_limits<jint>::max())) {
    return;
  }
  const jlong native_audio_track =
      LongFieldValue(reinterpret_cast<jobject>(const_cast<void *>(identity)),
                     "nativeAudioTrack");
  if (native_audio_track == 0) {
    return;
  }
  using GetPlayoutData = void(JNICALL *)(JNIEnv *, jobject, jint, jlong);
  reinterpret_cast<GetPlayoutData>(native)(
      GetJNIEnv(), reinterpret_cast<jobject>(const_cast<void *>(identity)),
      static_cast<jint>(size_bytes), native_audio_track);
}

}  // namespace jnivm
