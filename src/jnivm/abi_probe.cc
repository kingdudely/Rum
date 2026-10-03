#include "abi_probe.h"

#include "compat/guest_abi.h"
#include "jni_helpers.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace jnivm {
namespace {

std::string EnvOr(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? value : fallback;
}

}  // namespace

void WriteAbiProbe() {
  const char* path = std::getenv("MOCKTAIL_ABI_PROBE");
  if (path == nullptr || *path == '\0') {
    return;
  }

  std::vector<internal::ProbeMethod> methods;
  internal::CollectRequestedMethods(&methods);
  std::vector<std::string> classes;
  internal::CollectRequestedClasses(&classes);
  std::vector<std::string> fields;
  internal::CollectRequestedFields(&fields);
  std::sort(methods.begin(), methods.end(),
            [](const internal::ProbeMethod& a, const internal::ProbeMethod& b) {
              return a.name != b.name ? a.name < b.name
                                      : a.signature < b.signature;
            });
  std::sort(classes.begin(), classes.end());
  std::sort(fields.begin(), fields.end());

  nlohmann::json document;
  document["roblox_version"] = EnvOr("MOCKTAIL_ROBLOX_VERSION", "unknown");
  document["roblox_build_id"] = EnvOr("MOCKTAIL_ROBLOX_VERSION_CODE", "unknown");
  document["host_abi"] = mocktail::compat::kGuestAbi;
  document["classes"] = classes;
  document["fields"] = fields;

  nlohmann::json method_array = nlohmann::json::array();
  std::size_t recognized = 0;
  for (const internal::ProbeMethod& method : methods) {
    if (method.recognized) {
      ++recognized;
    }
    method_array.push_back({{"name", method.name},
                            {"signature", method.signature},
                            {"recognized", method.recognized}});
  }
  document["methods"] = std::move(method_array);
  document["counts"] = {{"classes", classes.size()},
                        {"fields", fields.size()},
                        {"methods", methods.size()},
                        {"methods_recognized", recognized}};

  std::FILE* file = std::fopen(path, "wb");
  if (file == nullptr) {
    std::fprintf(stderr, "[abi-probe] could not write %s\n", path);
    return;
  }
  std::fprintf(file, "%s\n", document.dump(2).c_str());
  std::fclose(file);
  std::fprintf(stderr,
               "[abi-probe] wrote %zu classes, %zu fields, %zu methods "
               "(%zu recognized) to %s\n",
               classes.size(), fields.size(), methods.size(), recognized,
               path);
}

}  // namespace jnivm