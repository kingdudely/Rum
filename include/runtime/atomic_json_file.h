#ifndef MOCKTAIL_RUNTIME_ATOMIC_JSON_FILE_H_
#define MOCKTAIL_RUNTIME_ATOMIC_JSON_FILE_H_

#include <filesystem>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace mocktail {
namespace runtime {

// Distinguishes one caller's temporaries and error text from another's. Both
// strings are reported verbatim, so they are part of the observable output.
struct AtomicJsonWriteNames {
  // Used in every error message, e.g. "platform cache metadata".
  const char* subject;
  // Temporary file prefix, e.g. ".mocktail-platform-cache.tmp.".
  const char* temporary_prefix;
};

// Writes value to path via a unique sibling temporary that is fsynced and then
// renamed into place, so a reader never observes a half-written file. The
// target must not be a symlink. On failure the temporary is removed and error
// describes what went wrong.
bool AtomicWriteJson(const std::filesystem::path& path,
                     const nlohmann::json& value,
                     const AtomicJsonWriteNames& names, std::string* error);

}  // namespace runtime
}  // namespace mocktail

#endif  // MOCKTAIL_RUNTIME_ATOMIC_JSON_FILE_H_