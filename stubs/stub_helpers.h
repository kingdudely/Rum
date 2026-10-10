// Shared helpers for the synthetic Android .so stubs. Header-only on
// purpose: every stub is its own shared object, so everything here is static
// and no stub links another. Keeps the getenv/dlsym-cache helpers that used
// to be copied into each stub in one place.
#ifndef MOCKTAIL_STUBS_STUB_HELPERS_H_
#define MOCKTAIL_STUBS_STUB_HELPERS_H_

#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <initializer_list>

static inline const char* StubGetEnvNonEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' ? value : nullptr;
}

// True when any of the named environment variables is present. Each stub
// passes its own trace switches plus the global ones.
static inline bool StubAnyEnvSet(std::initializer_list<const char*> names) {
  for (const char* name : names) {
    if (name != nullptr && std::getenv(name) != nullptr) {
      return true;
    }
  }
  return false;
}

static inline bool StubTestGraphicsStubsEnabled() {
  const char* value = std::getenv("MOCKTAIL_ENABLE_TEST_GRAPHICS_STUBS");
  return value != nullptr && std::strcmp(value, "1") == 0;
}

// Thread-safe dlsym cache. A null slot races harmlessly: concurrent callers
// may resolve twice, but every caller observes a valid pointer.
template <typename Fn>
static inline Fn StubResolveCached(Fn* slot, const char* name);

template <typename Fn>
static inline Fn StubResolveCached(Fn* slot, const char* name) {
  Fn fn = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
  if (__builtin_expect(fn == nullptr, 0)) {
    void* symbol = dlsym(RTLD_DEFAULT, name);
    fn = reinterpret_cast<Fn>(symbol);
    if (fn != nullptr) {
      __atomic_store_n(slot, fn, __ATOMIC_RELEASE);
    }
  }
  return fn;
}

#endif  // MOCKTAIL_STUBS_STUB_HELPERS_H_
