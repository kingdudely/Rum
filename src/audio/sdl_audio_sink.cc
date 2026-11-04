#include "roblox/audio/sdl_audio_sink.h"

#include "roblox/audio/sdl_audio_capture.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// SDL 3.4 added borrowed audio buffers. Older SDL copies instead; the caller
// still releases its buffer immediately because the copy already owns the data.

namespace roblox::audio {
namespace {

class SdlAudioSink;

struct SdlAudioSubsystemState {
  std::mutex mutex;
  std::vector<SdlAudioSink*> live_sinks;
  std::uint32_t configured_playback_device_id = 0;
  std::string configured_playback_device_name = "default";
  bool initialized = false;
};

SdlAudioSubsystemState& AudioSubsystemState() {
  static SdlAudioSubsystemState state;
  return state;
}

Status SdlError(const char* operation) {
  std::string message(operation);
  message.append(": ");
  const char* detail = SDL_GetError();
  message.append(detail != nullptr && detail[0] != '\0' ? detail
                                                        : "unknown SDL error");
  return Status::Error(StatusCode::kPlatformError, std::move(message));
}

Status InvalidArgument(std::string message) {
  return Status::Error(StatusCode::kInvalidArgument, std::move(message));
}

Status FailedPrecondition(std::string message) {
  return Status::Error(StatusCode::kFailedPrecondition, std::move(message));
}

Status ResolveSdlAudioSinkDevice(std::uint32_t requested_device_id,
                                 std::uint32_t* resolved_device_id) {
  if (resolved_device_id == nullptr) {
    return InvalidArgument("resolved SDL playback device pointer is null");
  }
  SdlAudioSubsystemState& state = AudioSubsystemState();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.initialized) {
    return FailedPrecondition(
        "SDL audio subsystem must be initialized by the process owner");
  }
  *resolved_device_id = requested_device_id == 0
                            ? state.configured_playback_device_id
                            : requested_device_id;
  return Status::Ok();
}

Status RegisterSdlAudioSink(SdlAudioSink* sink) {
  if (sink == nullptr) {
    return InvalidArgument("SDL audio sink registration is null");
  }
  SdlAudioSubsystemState& state = AudioSubsystemState();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.initialized) {
    return FailedPrecondition(
        "SDL audio subsystem stopped while opening a sink");
  }
  if (std::find(state.live_sinks.begin(), state.live_sinks.end(), sink) !=
      state.live_sinks.end()) {
    return FailedPrecondition("SDL audio sink is already registered");
  }
  state.live_sinks.push_back(sink);
  return Status::Ok();
}

void ReleaseSdlAudioSink(SdlAudioSink* sink) {
  SdlAudioSubsystemState& state = AudioSubsystemState();
  std::lock_guard<std::mutex> lock(state.mutex);
  const auto found =
      std::find(state.live_sinks.begin(), state.live_sinks.end(), sink);
  if (found != state.live_sinks.end()) {
    state.live_sinks.erase(found);
  }
}

SDL_AudioFormat ToSdlFormat(PcmSampleFormat format) {
  switch (format) {
    case PcmSampleFormat::kUnsigned8:
      return SDL_AUDIO_U8;
    case PcmSampleFormat::kSigned16LittleEndian:
      return SDL_AUDIO_S16LE;
    case PcmSampleFormat::kSigned32LittleEndian:
      return SDL_AUDIO_S32LE;
    case PcmSampleFormat::kFloat32LittleEndian:
      return SDL_AUDIO_F32LE;
  }
  return SDL_AUDIO_UNKNOWN;
}

#if SDL_VERSION_ATLEAST(3, 4, 0)
struct SdlReleaseContext {
  AudioBufferReleaseCallback callback = nullptr;
  void* callback_context = nullptr;
};

void SDLCALL OnSdlBufferReleased(void* userdata, const void* data, int size) {
  auto* release = static_cast<SdlReleaseContext*>(userdata);
  if (release == nullptr) {
    return;
  }
  if (release->callback != nullptr) {
    release->callback(release->callback_context, data,
                      size > 0 ? static_cast<std::size_t>(size) : 0);
  }
  delete release;
}
#endif  // SDL_VERSION_ATLEAST(3, 4, 0)

class SdlAudioSink final : public AudioSink {
 public:
  SdlAudioSink(PcmSpec spec, SDL_AudioStream* stream,
               SDL_AudioDeviceID logical_device_id,
               std::uint32_t physical_device_id,
               bool (*data_needed_callback)(void*, const void**,
                                            std::size_t*),
               void* data_needed_context)
      : source_spec_(spec),
        stream_(stream),
        logical_device_id_(logical_device_id),
        physical_device_id_(physical_device_id),
        data_needed_callback_(data_needed_callback),
        data_needed_context_(data_needed_context) {}

  ~SdlAudioSink() override { Shutdown(); }

  const PcmSpec& source_spec() const override { return source_spec_; }

  Status Enqueue(const PcmBuffer& buffer) override {
    const std::size_t frame_size = BytesPerFrame(source_spec_);
    if (buffer.data == nullptr || buffer.size_bytes == 0) {
      return Status::Error(StatusCode::kInvalidArgument,
                           "PCM buffer must contain data");
    }
    if (frame_size == 0 || buffer.size_bytes % frame_size != 0) {
      return Status::Error(StatusCode::kInvalidArgument,
                           "PCM buffer size must contain complete frames");
    }
    if (buffer.size_bytes >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      return Status::Error(StatusCode::kInvalidArgument,
                           "PCM buffer exceeds SDL's per-call byte limit");
    }

    SDL_AudioStream* stream = BeginCall();
    if (stream == nullptr) {
      return Status::Error(StatusCode::kFailedPrecondition,
                           "audio sink is shut down");
    }

    // The producer runs at mixer speed, not wall-clock speed. If the device
    // stalls - a backend that opened but never pulls, or an output that was
    // switched away - SDL keeps every queued buffer and grows its internal
    // stream by multi-megabyte reallocations. A real-time sink drops the oldest
    // audio instead of buffering forever: two seconds is more than enough to
    // absorb a hiccup and small enough to bound the footprint.
    Throttle(stream, frame_size);

    bool queued = false;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    if (buffer.release_callback == nullptr) {
      queued = SDL_PutAudioStreamData(stream, buffer.data,
                                      static_cast<int>(buffer.size_bytes));
    } else {
      auto* release = new (std::nothrow)
          SdlReleaseContext{buffer.release_callback, buffer.release_context};
      if (release == nullptr) {
        EndCall();
        return Status::Error(StatusCode::kUnavailable,
                             "unable to allocate SDL buffer release context");
      }
      queued = SDL_PutAudioStreamDataNoCopy(stream, buffer.data,
                                            static_cast<int>(buffer.size_bytes),
                                            OnSdlBufferReleased, release);
      if (!queued) {
        delete release;
      }
    }
#else
    // Without borrowed buffers the sink copies the PCM data, so the producer
    // can release its buffer as soon as the copy is queued.
    queued = SDL_PutAudioStreamData(stream, buffer.data,
                                    static_cast<int>(buffer.size_bytes));
    if (buffer.release_callback != nullptr) {
      buffer.release_callback(buffer.release_context, buffer.data,
                              buffer.size_bytes);
    }
#endif  // SDL_VERSION_ATLEAST(3, 4, 0)
    const Status status = queued
                              ? Status::Ok()
                              : SdlError(buffer.release_callback == nullptr
                                             ? "SDL_PutAudioStreamData"
                                             : "SDL_PutAudioStreamDataNoCopy");
    EndCall();
    return status;
  }

  // Blocks until the device has room. On SDL older than 3.4 there is no
  // borrowed-buffer API, so the runtime releases every buffer to the mixer as
  // soon as the copy is queued and the mixer never throttles itself: it
  // produces at hundreds of times realtime and SDL buffers all of it. Blocking
  // here is what AudioTrack.write does on Android, and it both bounds the
  // footprint and restores real-time pacing.
  void Throttle(SDL_AudioStream* stream, std::size_t frame_bytes) {
    constexpr int kMaximumWaitMs = 40;
    for (int waited = 0; waited < kMaximumWaitMs; ++waited) {
      const int queued = SDL_GetAudioStreamQueued(stream);
      if (queued <= static_cast<int>(max_queued_bytes_)) {
        return;
      }
      SDL_Delay(1);
    }
    // Still full: the device is not draining at all. Drop the oldest audio
    // rather than let the queue grow without bound.
    TrimBacklog(stream, frame_bytes);
  }

  // Drops the oldest audio until at most `max_queued_bytes` remain queued.
  // Frame-aligned, so a discard cannot split a sample frame.
  void TrimBacklog(SDL_AudioStream* stream, std::size_t frame_bytes) {
    const std::size_t queued_bytes = static_cast<std::size_t>(
        std::max(0, SDL_GetAudioStreamQueued(stream)));
    if (queued_bytes <= max_queued_bytes_) {
      return;
    }
    std::size_t excess = queued_bytes - max_queued_bytes_;
    if (frame_bytes != 0) {
      excess -= excess % frame_bytes;
    }
    if (excess == 0) {
      return;
    }
    scratch_.resize(excess);
    const int read = SDL_GetAudioStreamData(
        stream, scratch_.data(), static_cast<int>(excess));
    if (read > 0) {
      dropped_bytes_.fetch_add(static_cast<std::uint64_t>(read),
                               std::memory_order_relaxed);
      if (ShouldLogDrop()) {
        std::fprintf(stderr,
                     "  [roblox][audio] dropped %d queued bytes; outstanding=%zu "
                     "total_dropped=%llu\n",
                     read, excess,
                     static_cast<unsigned long long>(
                         dropped_bytes_.load(std::memory_order_relaxed)));
      }
    }
  }

  bool ShouldLogDrop() const {
    const std::uint64_t drops = drop_logs_.fetch_add(1, std::memory_order_relaxed);
    return drops % 256 == 0;
  }

  Status Pause() override {
    return WithStream("SDL_PauseAudioStreamDevice",
                      [](SDL_AudioStream* stream) {
                        return SDL_PauseAudioStreamDevice(stream);
                      });
  }

  Status Resume() override {
    return WithStream("SDL_ResumeAudioStreamDevice",
                      [](SDL_AudioStream* stream) {
                        return SDL_ResumeAudioStreamDevice(stream);
                      });
  }

  Status SetGain(float linear_gain) override {
    if (!std::isfinite(linear_gain) || linear_gain < 0.0F) {
      return Status::Error(StatusCode::kInvalidArgument,
                           "audio gain must be finite and non-negative");
    }
    return WithStream("SDL_SetAudioStreamGain",
                      [linear_gain](SDL_AudioStream* stream) {
                        return SDL_SetAudioStreamGain(stream, linear_gain);
                      });
  }

  Status Flush() override {
    return WithStream("SDL_FlushAudioStream", [](SDL_AudioStream* stream) {
      return SDL_FlushAudioStream(stream);
    });
  }

  Status Clear() override {
    return WithStream("SDL_ClearAudioStream", [](SDL_AudioStream* stream) {
      return SDL_ClearAudioStream(stream);
    });
  }

  Status GetQueuedBytes(std::size_t* size_bytes) const override {
    if (size_bytes == nullptr) {
      return Status::Error(StatusCode::kInvalidArgument,
                           "queued-byte output pointer is null");
    }
    *size_bytes = 0;
    SDL_AudioStream* stream = BeginCall();
    if (stream == nullptr) {
      return Status::Error(StatusCode::kFailedPrecondition,
                           "audio sink is shut down");
    }
    const int queued = SDL_GetAudioStreamQueued(stream);
    const Status status =
        queued >= 0 ? Status::Ok() : SdlError("SDL_GetAudioStreamQueued");
    if (queued >= 0) {
      *size_bytes = static_cast<std::size_t>(queued);
    }
    EndCall();
    return status;
  }

  Status SwitchPlaybackDevice(std::uint32_t physical_device_id) {
    const SDL_AudioDeviceID requested_device =
        physical_device_id == 0
            ? SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK
            : static_cast<SDL_AudioDeviceID>(physical_device_id);

    std::unique_lock<std::mutex> lock(mutex_);
    if (shutting_down_ || stream_ == nullptr || logical_device_id_ == 0) {
      return FailedPrecondition("audio sink is shut down");
    }
    if (physical_device_id_ == physical_device_id) {
      return Status::Ok();
    }
    switching_device_ = true;
    shutdown_cv_.wait(lock,
                      [this] { return active_calls_ == 0 || shutting_down_; });
    if (shutting_down_ || stream_ == nullptr || logical_device_id_ == 0) {
      switching_device_ = false;
      lock.unlock();
      shutdown_cv_.notify_all();
      return FailedPrecondition("audio sink is shut down");
    }

    const bool was_paused = SDL_AudioDevicePaused(logical_device_id_);
    const SDL_AudioDeviceID replacement =
        SDL_OpenAudioDevice(requested_device, nullptr);
    if (replacement == 0) {
      switching_device_ = false;
      lock.unlock();
      shutdown_cv_.notify_all();
      return SdlError("SDL_OpenAudioDevice during playback migration");
    }
    if (was_paused && !SDL_PauseAudioDevice(replacement)) {
      const Status status =
          SdlError("SDL_PauseAudioDevice during playback migration");
      SDL_CloseAudioDevice(replacement);
      switching_device_ = false;
      lock.unlock();
      shutdown_cv_.notify_all();
      return status;
    }

    SDL_UnbindAudioStream(stream_);
    if (!SDL_BindAudioStream(replacement, stream_)) {
      const Status status =
          SdlError("SDL_BindAudioStream during playback migration");
      (void)SDL_BindAudioStream(logical_device_id_, stream_);
      SDL_CloseAudioDevice(replacement);
      switching_device_ = false;
      lock.unlock();
      shutdown_cv_.notify_all();
      return status;
    }

    const SDL_AudioDeviceID previous = logical_device_id_;
    logical_device_id_ = replacement;
    physical_device_id_ = physical_device_id;
    SDL_CloseAudioDevice(previous);
    switching_device_ = false;
    lock.unlock();
    shutdown_cv_.notify_all();
    return Status::Ok();
  }

  std::uint32_t playback_device_id() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return physical_device_id_;
  }

  void Shutdown() override {
    SDL_AudioStream* stream = nullptr;
    SDL_AudioDeviceID logical_device_id = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (shutting_down_) {
        shutdown_cv_.wait(lock, [this] { return shutdown_complete_; });
        return;
      }
      shutting_down_ = true;
      shutdown_cv_.notify_all();
      shutdown_cv_.wait(
          lock, [this] { return active_calls_ == 0 && !switching_device_; });
      stream = std::exchange(stream_, nullptr);
      logical_device_id = std::exchange(logical_device_id_, 0);
    }

    if (stream != nullptr) {
      SDL_PauseAudioStreamDevice(stream);
      // SDL releases borrowed buffers (and runs their release callbacks)
      // before the stream is destroyed.
      SDL_DestroyAudioStream(stream);
      if (logical_device_id != 0) {
        SDL_CloseAudioDevice(logical_device_id);
      }
      ReleaseSdlAudioSink(this);
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      shutdown_complete_ = true;
    }
    shutdown_cv_.notify_all();
  }

  static void SDLCALL OnDataNeeded(void* userdata, SDL_AudioStream* stream,
                                   int additional_amount, int) {
    auto* sink = static_cast<SdlAudioSink*>(userdata);
    if (sink == nullptr || sink->data_needed_callback_ == nullptr ||
        additional_amount <= 0) {
      return;
    }
    constexpr std::size_t kMaximumBuffersPerCallback = 32;
    std::size_t supplied = 0;
    for (std::size_t index = 0;
         supplied < static_cast<std::size_t>(additional_amount) &&
         index < kMaximumBuffersPerCallback;
         ++index) {
      const void* data = nullptr;
      std::size_t size_bytes = 0;
      if (!sink->data_needed_callback_(sink->data_needed_context_, &data,
                                       &size_bytes) ||
          data == nullptr || size_bytes == 0 ||
          size_bytes >
              static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
          !SDL_PutAudioStreamData(stream, data, static_cast<int>(size_bytes))) {
        break;
      }
      supplied += size_bytes;
    }
  }

 private:
  std::size_t max_queued_bytes_ = 28800;

  std::vector<std::uint8_t> scratch_;
  std::atomic<std::uint64_t> dropped_bytes_{0};
  mutable std::atomic<std::uint64_t> drop_logs_{0};

  template <typename Operation>
  Status WithStream(const char* name, Operation operation) {
    SDL_AudioStream* stream = BeginCall();
    if (stream == nullptr) {
      return Status::Error(StatusCode::kFailedPrecondition,
                           "audio sink is shut down");
    }
    const bool succeeded = operation(stream);
    const Status status = succeeded ? Status::Ok() : SdlError(name);
    EndCall();
    return status;
  }

 public:
  // The queue is the pacing ring, so its depth is the playback latency. 150 ms
  // survives a scheduling hiccup without turning every sound into a delayed
  // echo; the device buffer and A2DP add their own few hundred ms on top.
  static constexpr std::size_t kQueueTargetMs = 150;

  // Sized from the device format in CreateSdlAudioSink, so the target is a
  // duration rather than a byte count that means something else at another
  // sample rate or channel count.
  void SetQueueTarget(std::size_t bytes) {
    if (bytes != 0) {
      max_queued_bytes_ = bytes;
    }
  }

 private:

  SDL_AudioStream* BeginCall() const {
    std::unique_lock<std::mutex> lock(mutex_);
    shutdown_cv_.wait(lock,
                      [this] { return !switching_device_ || shutting_down_; });
    if (shutting_down_ || stream_ == nullptr) {
      return nullptr;
    }
    ++active_calls_;
    return stream_;
  }

  void EndCall() const {
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      --active_calls_;
      if (__builtin_expect(shutting_down_ || switching_device_, 0)) {
        notify = true;
      }
    }
    if (notify) {
      shutdown_cv_.notify_all();
    }
  }

  const PcmSpec source_spec_;
  mutable std::mutex mutex_;
  mutable std::condition_variable shutdown_cv_;
  mutable std::size_t active_calls_ = 0;
  SDL_AudioStream* stream_ = nullptr;
  SDL_AudioDeviceID logical_device_id_ = 0;
  std::uint32_t physical_device_id_ = 0;
  bool (*data_needed_callback_)(void*, const void**, std::size_t*) = nullptr;
  void* data_needed_context_ = nullptr;
  mutable bool switching_device_ = false;
  bool shutting_down_ = false;
  bool shutdown_complete_ = false;
};

}  // namespace

Status InitializeSdlAudioSubsystem() {
  if (!SDL_IsMainThread()) {
    return Status::Error(StatusCode::kFailedPrecondition,
                         "SDL audio initialization requires the main thread");
  }
  SdlAudioSubsystemState& state = AudioSubsystemState();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (state.initialized) {
    return Status::Ok();
  }
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    return SdlError("SDL_InitSubSystem(SDL_INIT_AUDIO)");
  }
  state.initialized = true;
  return Status::Ok();
}

Status ListSdlPlaybackDevices(std::vector<SdlPlaybackDevice>* devices) {
  if (devices == nullptr) {
    return InvalidArgument("SDL playback device output pointer is null");
  }
  devices->clear();
  {
    SdlAudioSubsystemState& state = AudioSubsystemState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.initialized) {
      return FailedPrecondition(
          "SDL audio subsystem must be initialized before device enumeration");
    }
  }

  int count = 0;
  SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
  if (ids == nullptr) {
    return SdlError("SDL_GetAudioPlaybackDevices");
  }
  devices->reserve(count > 0 ? static_cast<std::size_t>(count) : 0U);
  for (int index = 0; index < count; ++index) {
    const char* name = SDL_GetAudioDeviceName(ids[index]);
    if (name == nullptr || name[0] == '\0') {
      SDL_free(ids);
      devices->clear();
      return SdlError("SDL_GetAudioDeviceName");
    }
    devices->push_back(
        {static_cast<std::uint32_t>(ids[index]), std::string(name)});
  }
  SDL_free(ids);
  return Status::Ok();
}

Status ResolveSdlPlaybackDevice(
    std::string_view requested,
    const std::vector<SdlPlaybackDevice>& available_devices,
    std::uint32_t* playback_device_id, std::string* resolved_name) {
  if (playback_device_id == nullptr || resolved_name == nullptr) {
    return InvalidArgument("SDL playback selection output pointer is null");
  }
  *playback_device_id = 0;
  resolved_name->clear();
  if (requested.empty() || requested.size() > 512U) {
    return InvalidArgument(
        "audio output device must be `default` or a bounded device name");
  }
  for (const unsigned char character : requested) {
    if (character < 0x20U || character == 0x7fU) {
      return InvalidArgument(
          "audio output device name must not contain control bytes");
    }
  }
  if (requested == "default") {
    *resolved_name = "default";
    return Status::Ok();
  }

  const SdlPlaybackDevice* match = nullptr;
  for (const SdlPlaybackDevice& device : available_devices) {
    if (device.name != requested) {
      continue;
    }
    if (match != nullptr) {
      return InvalidArgument(
          "audio output device name is ambiguous; use `default`");
    }
    match = &device;
  }
  if (match == nullptr) {
    return Status::Error(StatusCode::kUnavailable,
                         "configured audio output device is unavailable: " +
                             std::string(requested));
  }
  *playback_device_id = match->id;
  *resolved_name = match->name;
  return Status::Ok();
}

Status ConfigureSdlPlaybackDevice(
    std::string_view requested,
    std::vector<SdlPlaybackDevice>* available_devices,
    std::string* resolved_name) {
  if (available_devices == nullptr || resolved_name == nullptr) {
    return InvalidArgument("SDL playback configuration output pointer is null");
  }
  const std::string requested_copy(requested);
  Status status = ListSdlPlaybackDevices(available_devices);
  if (!status.ok() && requested_copy != "default") {
    return status;
  }
  if (!status.ok()) {
    available_devices->clear();
  }
  std::uint32_t playback_device_id = 0;
  status = ResolveSdlPlaybackDevice(requested_copy, *available_devices,
                                    &playback_device_id, resolved_name);
  if (!status.ok()) {
    return status;
  }

  SdlAudioSubsystemState& state = AudioSubsystemState();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.initialized) {
    return FailedPrecondition(
        "SDL audio subsystem stopped during playback configuration");
  }
  if (!state.live_sinks.empty()) {
    return FailedPrecondition(
        "SDL playback device must be configured before opening a sink");
  }
  state.configured_playback_device_id = playback_device_id;
  state.configured_playback_device_name = *resolved_name;
  return Status::Ok();
}

Status GetConfiguredSdlPlaybackDevice(std::uint32_t* playback_device_id,
                                      std::string* resolved_name) {
  if (playback_device_id == nullptr || resolved_name == nullptr) {
    return InvalidArgument(
        "configured SDL playback device output pointer is null");
  }
  SdlAudioSubsystemState& state = AudioSubsystemState();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.initialized) {
    return FailedPrecondition(
        "SDL audio subsystem must be initialized before reading its route");
  }
  *playback_device_id = state.configured_playback_device_id;
  *resolved_name = state.configured_playback_device_name;
  return Status::Ok();
}

Status SwitchSdlPlaybackDevice(std::uint32_t playback_device_id,
                               std::string* resolved_name) {
  if (resolved_name == nullptr) {
    return InvalidArgument("SDL playback switch output pointer is null");
  }
  resolved_name->clear();

  std::string target_name = "default";
  if (playback_device_id != 0) {
    std::vector<SdlPlaybackDevice> devices;
    Status status = ListSdlPlaybackDevices(&devices);
    if (!status.ok()) {
      return status;
    }
    const auto found =
        std::find_if(devices.begin(), devices.end(),
                     [playback_device_id](const SdlPlaybackDevice& device) {
                       return device.id == playback_device_id;
                     });
    if (found == devices.end()) {
      return Status::Error(StatusCode::kUnavailable,
                           "selected SDL playback device is unavailable");
    }
    target_name = found->name;
  }

  SdlAudioSubsystemState& state = AudioSubsystemState();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.initialized) {
    return FailedPrecondition(
        "SDL audio subsystem stopped during playback migration");
  }
  struct MigratedSink {
    SdlAudioSink* sink = nullptr;
    std::uint32_t previous_device_id = 0;
  };
  std::vector<MigratedSink> migrated;
  migrated.reserve(state.live_sinks.size());
  for (SdlAudioSink* sink : state.live_sinks) {
    const std::uint32_t previous_device_id = sink->playback_device_id();
    Status status = sink->SwitchPlaybackDevice(playback_device_id);
    if (!status.ok()) {
      for (auto rollback = migrated.rbegin(); rollback != migrated.rend();
           ++rollback) {
        (void)rollback->sink->SwitchPlaybackDevice(
            rollback->previous_device_id);
      }
      return status;
    }
    migrated.push_back({sink, previous_device_id});
  }

  state.configured_playback_device_id = playback_device_id;
  state.configured_playback_device_name = target_name;
  *resolved_name = target_name;
  return Status::Ok();
}

Status ShutdownSdlAudioSubsystem() {
  if (!SDL_IsMainThread()) {
    return Status::Error(StatusCode::kFailedPrecondition,
                         "SDL audio shutdown requires the main thread");
  }
  SdlAudioSubsystemState& state = AudioSubsystemState();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.initialized) {
    return Status::Ok();
  }
  if (!state.live_sinks.empty()) {
    return Status::Error(StatusCode::kFailedPrecondition,
                         "SDL audio shutdown requires every sink to close");
  }
  const Status capture_status = PrepareSdlAudioCaptureSubsystemShutdown();
  if (!capture_status.ok()) {
    return capture_status;
  }
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  state.configured_playback_device_id = 0;
  state.configured_playback_device_name = "default";
  state.initialized = false;
  return Status::Ok();
}

Status CreateSdlAudioSink(const SdlAudioSinkOptions& options,
                          std::unique_ptr<AudioSink>* sink) {
  if (sink == nullptr) {
    return Status::Error(StatusCode::kInvalidArgument,
                         "audio sink output pointer is null");
  }
  sink->reset();
  Status status = ValidatePcmSpec(options.source_spec);
  if (!status.ok()) {
    return status;
  }

  std::uint32_t playback_device_id = 0;
  status = ResolveSdlAudioSinkDevice(options.playback_device_id,
                                     &playback_device_id);
  if (!status.ok()) {
    return status;
  }

  SDL_AudioSpec source{};
  source.format = ToSdlFormat(options.source_spec.format);
  source.channels = options.source_spec.channels;
  source.freq = options.source_spec.sample_rate_hz;
  const SDL_AudioDeviceID device =
      playback_device_id == 0
          ? SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK
          : static_cast<SDL_AudioDeviceID>(playback_device_id);
  const SDL_AudioDeviceID logical_device = SDL_OpenAudioDevice(device, nullptr);
  if (logical_device == 0) {
    return SdlError("SDL_OpenAudioDevice");
  }
  {
    SDL_AudioSpec obtained{};
    int sample_frames = 0;
    const bool have_format = SDL_GetAudioDeviceFormat(logical_device, &obtained,
                                                      &sample_frames);
    std::fprintf(stderr,
                 "  [roblox][audio] sink opened driver=%s format=%d channels=%d "
                 "freq=%d frames=%d paused=%d bound_stream=%u\n",
                 SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
                 have_format ? static_cast<int>(obtained.format) : -1,
                 have_format ? obtained.channels : -1,
                 have_format ? obtained.freq : -1, sample_frames,
                 SDL_AudioDevicePaused(logical_device) ? 1 : 0,
                 static_cast<unsigned>(logical_device));
  }
  if (options.start_paused && !SDL_PauseAudioDevice(logical_device)) {
    status = SdlError("SDL_PauseAudioDevice");
    SDL_CloseAudioDevice(logical_device);
    return status;
  }

  SDL_AudioStream* stream = SDL_CreateAudioStream(&source, nullptr);
  if (stream == nullptr) {
    status = SdlError("SDL_CreateAudioStream");
    SDL_CloseAudioDevice(logical_device);
    return status;
  }
  if (!SDL_BindAudioStream(logical_device, stream)) {
    status = SdlError("SDL_BindAudioStream");
    SDL_DestroyAudioStream(stream);
    SDL_CloseAudioDevice(logical_device);
    return status;
  }

  auto* implementation = new (std::nothrow) SdlAudioSink(
      options.source_spec, stream, logical_device, playback_device_id,
      options.data_needed_callback, options.data_needed_context);
  if (implementation == nullptr) {
    SDL_DestroyAudioStream(stream);
    SDL_CloseAudioDevice(logical_device);
    return Status::Error(StatusCode::kUnavailable,
                         "unable to allocate SDL audio sink");
  }
  if (options.data_needed_callback != nullptr &&
      !SDL_SetAudioStreamGetCallback(stream, &SdlAudioSink::OnDataNeeded,
                                     implementation)) {
    status = SdlError("SDL_SetAudioStreamGetCallback");
    delete implementation;
    return status;
  }
  status = RegisterSdlAudioSink(implementation);
  if (!status.ok()) {
    delete implementation;
    return status;
  }
  // 150 ms of the device's own format. Falls back to the default when the
  // device reports nothing usable.
  {
    SDL_AudioSpec obtained{};
    int sample_frames = 0;
    if (SDL_GetAudioDeviceFormat(logical_device, &obtained, &sample_frames) &&
        obtained.freq > 0 && obtained.channels > 0) {
      const std::size_t device_frame_bytes =
          static_cast<std::size_t>(SDL_AUDIO_BYTESIZE(obtained.format)) *
          static_cast<std::size_t>(obtained.channels);
      if (device_frame_bytes != 0) {
        implementation->SetQueueTarget(
            device_frame_bytes * static_cast<std::size_t>(obtained.freq) *
            SdlAudioSink::kQueueTargetMs / 1000);
      }
    }
  }
  sink->reset(implementation);
  return Status::Ok();
}

}  // namespace roblox::audio
