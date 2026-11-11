# blutmond
Blutmond is an upcoming engine for Metroidvania-style GBA games

The research notes and the plan are in [docs/PLAN.md](docs/PLAN.md), the specification is in
[docs/spec/](docs/spec/README.md).

## Layout

| Path | Content |
|---|---|
| `src/core/` | platform-independent core: devices and memory heaps, pools, types |
| `src/graphics/` | graphics API: video (modes, backgrounds, palettes, tiles, maps, sprites, line tables), backdrop, tilemaps |
| `src/pack/` | packs: reader (ROM/RAM, in place), writer for tools, CRC-32 |
| `src/world/` | rooms from packs (graphics layers, map streaming), collision, platformer physics, the game loop for the engine classes (player, camera, kill zones, doors, save points), the state schema and save states |
| `src/link/` | live link: protocol (both sides), the runtime's live reload module, the PC side's pack push |
| `src/ppu/` | software PPU: renders the GBA video state in C; reference for tests, host back end, PPU snapshots |
| `src/hal/public/hal.h` | hardware abstraction layer: the only interface to a platform |
| `src/hal/host/` | host back end: the GBA's memories and video state as plain data (tests, editor) |
| `src/hal/gba/` | GBA back end: registers, start code, linker scripts for cartridge and multiboot |
| `editor/` | the editor (`bmeditor`, on [ose-ui](https://github.com/Fighter19/ose-ui) with SDL3 and GLES 2) |
| `apps/` | small programs: `backdrop` (smallest), `scene_test` (test scene for the video API), `room_viewer` (shows a cooked room), `game` (the test level, playable; `game_autoplay` plays itself) |
| `assets/` | `engine/` (Tiled tilesets for collision and materials), `test_room/` (own test room) |
| `third_party/` | `stb_image.h` (public domain), used by the cooker |
| `tests/` | host tests |
| `tools/` | `cooker/` (`bmcook`: Tiled rooms to packs), `gbafix.py` (GBA header), `check_hw_addresses.py`, `bm_mgba_run.c` (runs images in mGBA, takes screenshots and PPU snapshots), `bm_ppu_render.c` (renders a snapshot), `compare_images.py`, `bmlink/` (live link to a running game: Xboo cable, the PC game over TCP, or mGBA) |

Hardware addresses may only appear in `src/hal/gba/`; a test checks this.

## Build

Ubuntu 24.04 packages:

```
sudo apt install cmake ninja-build python3 gcc-arm-none-eabi libnewlib-arm-none-eabi libmgba-dev
```

`libmgba-dev` is optional; it enables `bm_mgba_run` and the tests that run the GBA images.

The editor (on by default, `-DBM_BUILD_EDITOR=OFF` to leave it out) fetches ose-ui at a pinned
commit and SDL3 (unless installed). SDL3 and the headless editor test need:

```
sudo apt install libegl-dev libgles-dev libegl-mesa0 libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev libxfixes-dev libxtst-dev libxinerama-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev
```

To work on ose-ui at the same time: `-DFETCHCONTENT_SOURCE_DIR_OSE_UI=<checkout>`.

GBA images (`backdrop.gba` for a cartridge, `backdrop_mb.gba` for multiboot, and so on). The
GBA build builds the host cooker on its own (in `build-gba/host-tools`) to cook the test room;
pass `-DBM_HOST_TOOLS_DIR=<host build>` to use an existing one:

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

Playing on the PC: `build/game` opens a window with the test level (or `build/game level.bmpk`).
Arrows move, X or Space jumps (A), Z is B, A and S are L and R, Enter is Start, Backspace is
Select, F11 toggles fullscreen and Esc quits; gamepads work too. The save game is kept in
`~/.local/share/blutmond/save.bin`. The window also listens for the live link on
127.0.0.1:7414 (`--listen N`, `--no-listen`). With `--frames N`, `--trace` or an output picture
the game runs headless as before (`--headless` forces it).

The editor:

```
build/bmeditor            # the test rooms
build/bmeditor room.tmx room2.tmx
```

It cooks the room, shows it as the runtime renders it (with collision and object overlays), the
GBA screen at the camera (click into the room), the palette banks, the tiles, the hardware
budget and the cooker's report, for the GBA or SNES profile. It cooks again when the file
changes, so it can run next to Tiled.

**Run** (F6) cooks all rooms into one pack and plays it on the target chosen next to the button:

- **PC**: starts the game in its own window; Run again restarts it with the new pack.
- **GBA (Xboo)**: multiboots the runtime if none answers on the cable, then pushes the pack.
  This needs the multiboot image: configure with `-DBM_GBA_BUILD_DIR=...`, or set `gba-image`.

With **Live** on, every cook while a target runs (a saved room, F5) is pushed in as a hot
reload: only the changed chunks are sent and the player stays where they are. Stop ends the PC
game. Settings go in `~/.config/blutmond/editor.conf` (`key = value` lines) or on the command
line as `--key value`: `xboo` (default `/dev/parport0`), `reset` (`none` or `xboo`),
`gba-image`, `port` (7414), `game` and `bmlink` (program paths).

Cooking rooms by hand:

```
build/bmcook --profile gba --report report.txt -o room.bmpk assets/test_room/room.tmx
build/room_viewer room.bmpk --frames 200 out.ppm
build/bmcook -o level.bmpk assets/test_room/room.tmx assets/test_room/room2.tmx
build/game level.bmpk --autoplay --trace --frames 700
```

Save states: `build/game level.bmpk --autoplay --frames 400 --snapshot-at 300 snap.bmst` writes
one, `--load snap.bmst` continues from it (also from a snapshot taken in mGBA; the format is
the same on both).

On the GBA: `build-gba/game.gba` (D-pad, A to jump, up/down on ladders, down + A to drop
through platforms) and `build-gba/game_autoplay.gba`.

Live link: the game listens on the serial port. `bmlink` pushes a changed pack into it while
it runs (only the chunks that changed are sent), and reads or loads state:

```
build/bmlink --mgba build-gba/game.gba run 60 push level.bmpk hot run 600 screenshot out.ppm
build/bmlink --xboo /dev/parport0 --reset xboo boot build-gba/game_mb.gba hello push level.bmpk hot
build/bmlink --xboo /dev/parport0 ping 50 stats
build/bmlink --tcp 7414 push level.bmpk hot      # the PC game in its window
```

The comment at the top of `tools/bmlink/bmlink.c` lists all commands. `--adaptive` tunes the
per-word delay and remembers it per device.

`-DUSE_SANITIZER=ON` adds AddressSanitizer and UBSan to the host build.

With `BM_GBA_BUILD_DIR` set, the tests also check that the test scene rendered by the software
PPU on the host matches the GBA image running in mGBA pixel for pixel, and that a PPU snapshot
taken in mGBA renders the same picture. The images land in `build/test-output/`.

The Nintendo logo in the GBA header is not part of this repository. Emulators and multiboot
tools such as xcomms do not need it (xcomms inserts it during upload); for a cartridge on real
hardware pass it to `tools/gbafix.py --logo`.
