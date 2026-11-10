# blutmond
Blutmond is an upcoming engine for Metroidvania-style GBA games

The research notes and the plan are in [docs/PLAN.md](docs/PLAN.md), the specification is in
[docs/spec/](docs/spec/README.md).

## Layout

| Path | Content |
|---|---|
| `src/core/` | platform-independent core: devices and memory heaps, pools, types |
| `src/graphics/` | graphics API (backdrop, tilemaps, ...) |
| `src/hal/public/hal.h` | hardware abstraction layer: the only interface to a platform |
| `src/hal/host/` | host back end: the GBA's memories and video state as plain data (tests, editor) |
| `src/hal/gba/` | GBA back end: registers, start code, linker scripts for cartridge and multiboot |
| `apps/` | small programs; `backdrop` is the smallest one |
| `tests/` | host tests |
| `tools/` | `gbafix.py` (GBA header), `check_hw_addresses.py`, `bm_mgba_run.c` (runs images in mGBA) |

Hardware addresses may only appear in `src/hal/gba/`; a test checks this.

## Build

Ubuntu 24.04 packages:

```
sudo apt install cmake ninja-build python3 gcc-arm-none-eabi libnewlib-arm-none-eabi libmgba-dev
```

`libmgba-dev` is optional; it enables `bm_mgba_run` and the tests that run the GBA images.

GBA images (`backdrop.gba` for a cartridge, `backdrop_mb.gba` for multiboot):

```
cmake -S . -B build-gba -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gba.cmake -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-gba
```

Host build and tests, including the GBA images in mGBA:

```
cmake -S . -B build -G Ninja -DBM_GBA_BUILD_DIR=$PWD/build-gba
cmake --build build
ctest --test-dir build --output-on-failure
```

`-DUSE_SANITIZER=ON` adds AddressSanitizer and UBSan to the host build.

The Nintendo logo in the GBA header is not part of this repository. Emulators and multiboot
tools such as xcomms do not need it (xcomms inserts it during upload); for a cartridge on real
hardware pass it to `tools/gbafix.py --logo`.
