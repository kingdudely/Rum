#include "runtime/atomic_json_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "mocktail/platform/posix_primitives.h"
#include "runtime/runtime_paths.h"

namespace mocktail {
namespace runtime {

bool AtomicWriteJson(const std::filesystem::path& path,
                     const nlohmann::json& value,
                     const AtomicJsonWriteNames& names, std::string* error) {
  const std::string subject = names.subject;

  std::error_code filesystem_error;
  if (!RuntimePaths::EnsureDirectory(path.parent_path(), &filesystem_error)) {
    *error = "cannot create " + subject + " directory";
    return false;
  }
  const std::filesystem::file_status target_status =
      std::filesystem::symlink_status(path, filesystem_error);
  if (!filesystem_error && std::filesystem::is_symlink(target_status)) {
    *error = subject + " target is a symlink";
    return false;
  }
  if (filesystem_error != std::errc::no_such_file_or_directory &&
      filesystem_error) {
    *error = "cannot inspect " + subject + " target";
    return false;
  }

  const std::filesystem::path temporary =
      platform::MakeTemporaryPath(path.parent_path(), names.temporary_prefix);
  const int descriptor =
      open(temporary.c_str(),
           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (descriptor < 0) {
    *error = "cannot create temporary " + subject;
    return false;
  }
  bool stored = false;
  {
    const platform::ScopedFileDescriptor file(descriptor);
    const std::string bytes = value.dump();
    stored = fchmod(file.get(), S_IRUSR | S_IWUSR) == 0 &&
             platform::WriteAll(file.get(), bytes) && fsync(file.get()) == 0;
  }
  if (!stored || rename(temporary.c_str(), path.c_str()) != 0) {
    const int saved_errno = errno;
    (void)unlink(temporary.c_str());
    errno = saved_errno;
    *error = "cannot atomically store " + subject;
    return false;
  }
  const int directory_descriptor =
      open(path.parent_path().c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
  if (directory_descriptor < 0) {
    *error = "cannot open " + subject + " directory for sync";
    return false;
  }
  const platform::ScopedFileDescriptor directory(directory_descriptor);
  if (fsync(directory.get()) != 0) {
    *error = "cannot sync " + subject + " directory";
    return false;
  }
  return true;
}

}  // namespace runtime
}  // namespace mocktail