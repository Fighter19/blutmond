// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "native_module.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

extern "C" {
#include "native.h"
}

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace bm {

namespace {

uint16_t rd16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
void put16(std::vector<uint8_t> &b, uint32_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
void put32(std::vector<uint8_t> &b, uint32_t v) { put16(b, v); put16(b, v >> 16); }

[[noreturn]] void bad(const std::string &what)
{
  throw std::runtime_error("native module: " + what);
}

// ELF constants (ARM ELF specification).
enum { SHT_PROGBITS = 1, SHT_NOBITS = 8, SHT_REL = 9, SHT_RELA = 4, SHF_WRITE = 1, SHF_ALLOC = 2 };
enum {
  R_ARM_NONE = 0, R_ARM_PC24 = 1, R_ARM_ABS32 = 2, R_ARM_REL32 = 3, R_ARM_THM_CALL = 10, R_ARM_CALL = 28, R_ARM_JUMP24 = 29,
  R_ARM_THM_JUMP24 = 30, R_ARM_TARGET1 = 38, R_ARM_V4BX = 40, R_ARM_PREL31 = 42, R_ARM_THM_JUMP11 = 102, R_ARM_THM_JUMP8 = 103,
};

std::string quote(const std::string &s)
{
  return "\"" + s + "\"";
}

// Runs a command; returns its exit status and what it printed.
int runCommand(const std::string &command, std::string &output)
{
  FILE *pipe = popen((command + " 2>&1").c_str(), "r");
  if (!pipe) return -1;
  char buffer[512];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0) output.append(buffer, n);
  return pclose(pipe);
}

std::vector<uint8_t> readFile(const std::string &path)
{
  std::ifstream f(path, std::ios::binary);
  if (!f) bad("cannot read " + path);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Everything in one piece from address 0, the module's header first. Data that can be
// written and zero-initialised data get their own sections so they can be refused.
const char *kLinkerScript = R"(SECTIONS
{
  . = 0;
  .text : { KEEP(*(.bmhead)) *(.text .text.* .glue_7 .glue_7t .v4_bx .rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.* COMMON) }
  /DISCARD/ : { *(.ARM.exidx* .ARM.extab*) }
}
)";

}  // namespace

std::vector<uint8_t> packNativeElf(const std::vector<uint8_t> &elf)
{
  const uint8_t *p = elf.data();
  if (elf.size() < 52 || std::memcmp(p, "\x7f" "ELF", 4) != 0) bad("not an ELF file");
  if (p[4] != 1 || p[5] != 1 || rd16(p + 18) != 40) bad("not a 32-bit little-endian ARM file");
  uint32_t shoff = rd32(p + 32);
  uint16_t shentsize = rd16(p + 46), shnum = rd16(p + 48), shstrndx = rd16(p + 50);
  if (shentsize < 40 || shoff > elf.size() || size_t(shnum) * shentsize > elf.size() - shoff || shstrndx >= shnum) bad("damaged ELF file");
  auto section = [&](uint32_t i) { return p + shoff + size_t(i) * shentsize; };
  auto inFile = [&](uint32_t offset, uint32_t size) { return offset <= elf.size() && size <= elf.size() - offset; };
  const uint8_t *names = section(shstrndx);
  if (!inFile(rd32(names + 16), rd32(names + 20))) bad("damaged ELF file");
  auto nameOf = [&](const uint8_t *s) {
    uint32_t at = rd32(s), table = rd32(names + 16), size = rd32(names + 20);
    std::string n;
    while (at < size && p[table + at] != 0) n += char(p[table + at++]);
    return n;
  };

  std::vector<uint8_t> image;
  for (uint32_t i = 0; i < shnum; i++)
  {
    const uint8_t *s = section(i);
    uint32_t type = rd32(s + 4), flags = rd32(s + 8), addr = rd32(s + 12), offset = rd32(s + 16), size = rd32(s + 20);
    if (!(flags & SHF_ALLOC) || size == 0) continue;
    // Native code keeps everything in the object and in the context it is given.
    if (type == SHT_NOBITS || (flags & SHF_WRITE)) bad("the code has global variables (" + nameOf(s) + ", " + std::to_string(size) + " bytes)");
    if (type != SHT_PROGBITS) continue;
    if (!inFile(offset, size) || addr > (1u << 20) || size > (1u << 20)) bad("damaged ELF file");
    if (image.size() < addr + size) image.resize(addr + size, 0);
    std::memcpy(image.data() + addr, p + offset, size);
  }
  while (image.size() % 4) image.push_back(0);
  if (image.size() < sizeof(BmVmNativeHeader) || rd32(image.data()) != BM_NATIVE_MAGIC) bad("the module header is not at address 0");

  std::vector<uint32_t> relocs;
  for (uint32_t i = 0; i < shnum; i++)
  {
    const uint8_t *s = section(i);
    uint32_t type = rd32(s + 4), offset = rd32(s + 16), size = rd32(s + 20), target = rd32(s + 28), entsize = rd32(s + 36);
    if (type != SHT_REL && type != SHT_RELA) continue;
    if (target >= shnum || !(rd32(section(target) + 8) & SHF_ALLOC)) continue;   // debug information
    if (type == SHT_RELA || entsize < 8 || !inFile(offset, size)) bad("unexpected relocation table");
    for (uint32_t at = 0; at + entsize <= size; at += entsize)
    {
      uint32_t where = rd32(p + offset + at), kind = rd32(p + offset + at + 4) & 0xFF;
      switch (kind)
      {
      case R_ARM_ABS32: case R_ARM_TARGET1:
        if ((where & 3) != 0 || where + 4 > image.size()) bad("an address at an odd place (offset " + std::to_string(where) + ")");
        if (rd32(image.data() + where) >= image.size()) bad("an address outside the module (offset " + std::to_string(where) + ")");
        relocs.push_back(where);
        break;
      // Distances inside the module stay right wherever it is loaded.
      case R_ARM_NONE: case R_ARM_PC24: case R_ARM_REL32: case R_ARM_THM_CALL: case R_ARM_CALL: case R_ARM_JUMP24: case R_ARM_THM_JUMP24:
      case R_ARM_V4BX: case R_ARM_PREL31: case R_ARM_THM_JUMP11: case R_ARM_THM_JUMP8:
        break;
      default:
        bad("relocation type " + std::to_string(kind) + " cannot be handled when the module is loaded");
      }
    }
  }
  std::sort(relocs.begin(), relocs.end());
  relocs.erase(std::unique(relocs.begin(), relocs.end()), relocs.end());

  std::vector<uint8_t> chunk;
  put32(chunk, BM_NATIVE_MAGIC);
  put16(chunk, BM_NATIVE_VERSION);
  put16(chunk, BM_NATIVE_TARGET_ARMV4T);
  put32(chunk, uint32_t(image.size()));
  put32(chunk, uint32_t(relocs.size()));
  chunk.insert(chunk.end(), image.begin(), image.end());
  for (uint32_t r : relocs) put32(chunk, r);
  return chunk;
}

std::vector<uint8_t> buildNativeModule(const std::string &cSource, const NativeBuild &build)
{
  namespace fs = std::filesystem;
  std::string include = build.includeDir;
#ifdef BM_NATIVE_INCLUDE_DIR
  if (include.empty()) include = BM_NATIVE_INCLUDE_DIR;
#endif
  if (include.empty()) bad("no include folder for native.h given");

  std::random_device rd;
  fs::path dir = fs::temp_directory_path() / ("bmcook-native-" + std::to_string(rd()) + std::to_string(rd()));
  fs::create_directories(dir);
  struct Cleanup
  {
    fs::path dir;
    ~Cleanup() { std::error_code ec; fs::remove_all(dir, ec); }
  } cleanup{dir};
  std::string c = (dir / "module.c").string(), o = (dir / "module.o").string(), ld = (dir / "module.ld").string(), elf = (dir / "module.elf").string();
  std::ofstream(c) << cSource;
  std::ofstream(ld) << kLinkerScript;

  // ARM code (for the fast memory), able to call the runtime's Thumb code and be called by it.
  const std::string arch = " -marm -mcpu=arm7tdmi -mthumb-interwork";
  std::string output;
  std::string compile = quote(build.compiler) + arch + " -std=gnu11 -O2 -g -ffreestanding -fno-pic -fno-common -fno-builtin -fno-asynchronous-unwind-tables" +
                        " -Wall -DBM_PLATFORM_GBA -I" + quote(include) + " -c " + quote(c) + " -o " + quote(o);
  if (runCommand(compile, output) != 0) bad("the compiler failed:\n" + compile + "\n" + output);
  std::string link = quote(build.compiler) + arch + " -nostdlib -nostartfiles -Wl,--emit-relocs -Wl,--build-id=none -Wl,-T," + quote(ld) + " " + quote(o) + " -lgcc -o " + quote(elf);
  output.clear();
  if (runCommand(link, output) != 0) bad("linking failed:\n" + link + "\n" + output);

  std::vector<uint8_t> chunk = packNativeElf(readFile(elf));
  if (!build.keepElf.empty())
  {
    std::error_code ec;
    fs::copy_file(elf, build.keepElf, fs::copy_options::overwrite_existing, ec);
    if (ec) bad("cannot write " + build.keepElf);
  }
  return chunk;
}

}  // namespace bm
