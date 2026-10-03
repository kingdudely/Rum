#include "runtime/guest_abi_check.h"

#include "compat/guest_abi.h"

#include <elf.h>

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace mocktail {
namespace runtime {
namespace {

// Enough of the ELF header for e_ident plus e_type and e_machine.
constexpr std::size_t kHeaderBytes = 64;

}  // namespace

std::string_view ElfMachineAbiName(unsigned machine) {
  switch (machine) {
    case EM_X86_64:
      return compat::kGuestAbi;
    case EM_AARCH64:
      return "arm64-v8a";
    case EM_386:
      return "x86";
    case EM_ARM:
      return "armeabi-v7a";
    default:
      return "unknown";
  }
}

GuestAbiReport InspectGuestLibraryAbi(const std::filesystem::path& library) {
  std::array<unsigned char, kHeaderBytes> header{};
  std::FILE* file = std::fopen(library.c_str(), "rb");
  if (file == nullptr) {
    return {GuestAbiVerdict::kUnreadable,
            "Could not open libroblox.so: " + library.string()};
  }
  const std::size_t read = std::fread(header.data(), 1, header.size(), file);
  std::fclose(file);

  // Magic first: a short file with the wrong magic is simply not an ELF
  // library, and reporting "too short" would point the reader at the wrong
  // problem.
  if (read < SELFMAG || std::memcmp(header.data(), ELFMAG, SELFMAG) != 0) {
    return {GuestAbiVerdict::kNotElf,
            "That file is not an ELF shared library. Pass the native "
            "libroblox.so, not an APK or a zip."};
  }
  if (read < kHeaderBytes) {
    return {GuestAbiVerdict::kUnreadable,
            "libroblox.so is truncated, so its ELF header is incomplete: " +
                library.string()};
  }

  const unsigned char elf_class = header[EI_CLASS];
  const unsigned char host_class =
      sizeof(void*) == 8 ? ELFCLASS64 : ELFCLASS32;
  if (elf_class != host_class) {
    return {GuestAbiVerdict::kWrongElfClass,
            std::string("libroblox.so is ") +
                (elf_class == ELFCLASS64 ? "64-bit" : "32-bit") +
                ", but this Mocktail build is " +
                (host_class == ELFCLASS64 ? "64-bit" : "32-bit") + "."};
  }

  // e_ident is EI_NIDENT bytes, then a 2-byte e_type, then e_machine at the same
  // offset in Elf32 and Elf64. Read the bytes rather than casting so this does
  // not depend on the host's alignment rules.
  constexpr std::size_t kMachineOffset = EI_NIDENT + 2;
  static_assert(kMachineOffset + sizeof(std::uint16_t) <= kHeaderBytes);
  std::uint16_t machine = 0;
  std::memcpy(&machine, header.data() + kMachineOffset, sizeof(machine));
  if (static_cast<int>(machine) != compat::kGuestElfMachine) {
    const std::string guest_abi(ElfMachineAbiName(machine));
    const std::string host_abi(compat::kGuestAbi);
    return {GuestAbiVerdict::kWrongMachine,
            "That libroblox.so is " + guest_abi + ", but this Mocktail build "
            "is " + host_abi + ". Build Mocktail for " + guest_abi +
                ", or supply a " + host_abi + " libroblox.so."};
  }

  return {GuestAbiVerdict::kOk, {}};
}

}  // namespace runtime
}  // namespace mocktail