#ifndef ROBLOX_COMPAT_BIONIC_STDIO_RUNTIME_H_
#define ROBLOX_COMPAT_BIONIC_STDIO_RUNTIME_H_

#include <sys/types.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace roblox::compat {

// Android's public LP64 FILE ABI is opaque but has a fixed 152-byte stride.
// Old NDK clients derive stdin/stdout/stderr by indexing the exported __sF
// array, so its size and alignment are part of the binary interface even when
// all actual I/O is delegated to host stdio.
constexpr size_t kBionicFileSize = 152;
constexpr size_t kBionicStandardStreamCount = 3;

}  // namespace roblox::compat

extern "C" {

alignas(sizeof(void*)) extern unsigned char
    __sF[roblox::compat::kBionicStandardStreamCount]
        [roblox::compat::kBionicFileSize];

}  // extern "C"

namespace roblox::compat {

inline FILE* TranslateBionicFile(FILE* stream) noexcept {
  const uintptr_t address = reinterpret_cast<uintptr_t>(stream);
  if (__builtin_expect(address == reinterpret_cast<uintptr_t>(&__sF[0]), 0)) {
    return stdin;
  }
  if (__builtin_expect(address == reinterpret_cast<uintptr_t>(&__sF[1]), 0)) {
    return stdout;
  }
  if (__builtin_expect(address == reinterpret_cast<uintptr_t>(&__sF[2]), 0)) {
    return stderr;
  }
  return stream;
}

void* BionicFileArraySymbolAddress() noexcept;
void* BionicStdinSymbolAddress() noexcept;
void* BionicStdoutSymbolAddress() noexcept;
void* BionicStderrSymbolAddress() noexcept;

}  // namespace roblox::compat

// FILE values returned by host fopen/fdopen pass through unchanged. Pointers
// into Bionic's ABI-sized __sF array are translated to the corresponding host
// standard stream at this single boundary.
extern "C" {

size_t roblox_fwrite(const void* buffer, size_t size, size_t count,
                       FILE* stream);
size_t roblox_fread(void* buffer, size_t size, size_t count, FILE* stream);
int roblox_fflush(FILE* stream);
int roblox_fclose(FILE* stream);
int roblox_feof(FILE* stream);
int roblox_ferror(FILE* stream);
void roblox_clearerr(FILE* stream);
int roblox_fileno(FILE* stream);
int roblox_fseek(FILE* stream, long offset, int whence);
long roblox_ftell(FILE* stream);
int roblox_fseeko(FILE* stream, off_t offset, int whence);
off_t roblox_ftello(FILE* stream);
char* roblox_fgets(char* string, int count, FILE* stream);
int roblox_fputc(int character, FILE* stream);
int roblox_fputs(const char* string, FILE* stream);
int roblox_getc(FILE* stream);
wint_t roblox_fputwc(wchar_t character, FILE* stream);
int roblox_setvbuf(FILE* stream, char* buffer, int mode, size_t size);
int roblox_ungetc(int character, FILE* stream);
int roblox_vfprintf(FILE* stream, const char* format, va_list arguments);
int roblox_fprintf(FILE* stream, const char* format, ...);
int roblox_vfscanf(FILE* stream, const char* format, va_list arguments);
int roblox_fscanf(FILE* stream, const char* format, ...);
size_t roblox___fread_chk(void* buffer, size_t buffer_size, size_t size,
                            size_t count, FILE* stream);
size_t roblox___fwrite_chk(const void* buffer, size_t buffer_size,
                             size_t size, size_t count, FILE* stream);

}  // extern "C"

#endif  // ROBLOX_COMPAT_BIONIC_STDIO_RUNTIME_H_
