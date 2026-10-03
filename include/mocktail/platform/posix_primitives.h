#ifndef MOCKTAIL_PLATFORM_POSIX_PRIMITIVES_H_
#define MOCKTAIL_PLATFORM_POSIX_PRIMITIVES_H_

#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mocktail {
namespace platform {

// Owns an open descriptor for the duration of a scope. Closing -1 is skipped,
// so a default-constructed instance is inert.
class ScopedFileDescriptor final {
 public:
  explicit ScopedFileDescriptor(int descriptor = -1)
      : descriptor_(descriptor) {}
  ~ScopedFileDescriptor() {
    if (descriptor_ >= 0) {
      close(descriptor_);
    }
  }

  ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
  ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;

  int get() const { return descriptor_; }

 private:
  int descriptor_ = -1;
};

// Writes every byte or reports failure, retrying on EINTR. A short write is
// resumed from where it stopped; write() returning 0 with bytes outstanding
// means no progress is possible and is treated as failure.
inline bool WriteAll(int descriptor, std::string_view bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t written =
        write(descriptor, bytes.data() + offset, bytes.size() - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (written == 0) {
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

// Positional WriteAll, for patching bytes in place inside an existing file.
inline bool WriteAllAt(int descriptor, const char* data, std::size_t size,
                       off_t offset) {
  std::size_t written = 0;
  while (written < size) {
    const ssize_t result = pwrite(descriptor, data + written, size - written,
                                  offset + static_cast<off_t>(written));
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (result == 0) {
      return false;
    }
    written += static_cast<std::size_t>(result);
  }
  return true;
}

// Builds a unique sibling path for an atomic write: "<parent>/<prefix><pid>.<n>".
// The caller is expected to rename it over the target once it is complete. The
// sequence number is shared process-wide so the prefixes need not be unique.
inline std::filesystem::path MakeTemporaryPath(
    const std::filesystem::path& parent, std::string_view prefix) {
  static std::atomic<std::uint64_t> sequence{0};
  return parent / (std::string(prefix) + std::to_string(getpid()) + "." +
                   std::to_string(sequence.fetch_add(1)));
}

// Overwrites a secret before releasing it. The volatile pointer keeps the
// compiler from eliding the stores.
inline void SecureClear(std::string* value) {
  if (value == nullptr) {
    return;
  }
  volatile char* bytes = value->empty() ? nullptr : value->data();
  for (std::size_t index = 0; index < value->size(); ++index) {
    bytes[index] = 0;
  }
  value->clear();
}

// Overwrites a secret byte buffer before releasing it.
inline void SecureClear(std::vector<std::uint8_t>* value) {
  if (value == nullptr) {
    return;
  }
  volatile std::uint8_t* bytes = value->empty() ? nullptr : value->data();
  for (std::size_t index = 0; index < value->size(); ++index) {
    bytes[index] = 0;
  }
  value->clear();
}

// Overwrites a secret in a buffer the caller does not own.
inline void SecureErase(char* data, std::size_t size) {
  volatile char* bytes = data;
  for (std::size_t index = 0; bytes != nullptr && index < size; ++index) {
    bytes[index] = '\0';
  }
}

}  // namespace platform
}  // namespace mocktail

#endif  // MOCKTAIL_PLATFORM_POSIX_PRIMITIVES_H_