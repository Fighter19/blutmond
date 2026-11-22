# blutmond
Blutmond is an upcoming engine for Metroidvania-style GBA games

The research notes and the plan are in [docs/PLAN.md](docs/PLAN.md), the specification is in
[docs/spec/](docs/spec/README.md).

## Documentation

- [docs/USAGE.md](docs/USAGE.md): building, the editor, exporting a `.gba` file, a real GBA
  over the Xboo cable, the sync between GBA and PC, the browser build, what to do when
  something does not work.
- [docs/HARDWARE.md](docs/HARDWARE.md): what the first runs on a real GBA showed that the
  emulator had not, and what the sync needed.
- [docs/PLAN.md](docs/PLAN.md): the plan and the state of each milestone.
- [docs/spec/](docs/spec/README.md): formats and protocols.

## Layout

| Path | Content |
|---|---|
| `src/core/` | platform-independent core: devices and memory heaps, pools, types |
| `src/graphics/` | graphics API: video (modes, backgrounds, palettes, tiles, maps, sprites, line tables), backdrop, tilemaps |
| `src/pack/` | packs: reader (ROM/RAM, in place), writer for tools, CRC-32 |
| `src/world/` | rooms from packs (graphics layers, map streaming), collision, platformer physics, sprites and animations, the game loop for the engine classes (player, camera, kill zones, doors, save points), scripted objects, the state schema and save states |
| `src/sound/` | the sound engine: sequencer and stereo mixer, the same code on the GBA, the PC and in the editor's preview |
| `src/script/` | the bytecode interpreter for Mondskript with its verifier, and the table of what scripts can call |
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
| `tools/` | `cooker/` (`bmcook`: rooms, sprites and scripts to packs), `mondskript/` (the script compiler), `gbafix.py` (GBA header), `check_hw_addresses.py`, `bm_mgba_run.c` (runs images in mGBA, takes screenshots and PPU snapshots), `bm_mgba_pair.c` (two images in mGBA joined by a link cable), `bm_ppu_render.c` (renders a snapshot), `compare_images.py`, `bmlink/` (live link to a running game: Xboo cable, the PC game over TCP, or mGBA) |

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
build/bmeditor                       # assets/test.bmproj
build/bmeditor game.bmproj
build/bmeditor room.tmx room2.tmx    # a project that is not saved yet
```

**Export** (Ctrl+E) writes the whole game as one `.gba` file for an emulator or a flash
cartridge; `build/bmexport` does the same on the command line. The editor and the game also run
in the browser; see [docs/USAGE.md](docs/USAGE.md) sections 3 and 10.

The editor has three themes: classic, light and dark. Choose one in the View menu (it is kept
for the next start) or with `--theme classic|light|dark`.

It cooks the room, shows it as the runtime renders it (with collision and object overlays), the
GBA screen at the camera (click into the room), the palette banks, the tiles, the hardware
budget and the cooker's report, for the GBA or SNES profile. It cooks again when the file
changes, so it can run next to Tiled.

- **Project** (left): the project's rooms and the files in its asset folders
  ([docs/spec/project-format.md](docs/spec/project-format.md)), with how many rooms use each
  asset and which are unused. New room, Add room, Remove room, Import (or drop files onto the
  editor), Open (in the program the system uses for it, e.g. Tiled), Delete (refused while a
  room uses the file).
- **Palette** (Room tab, right): click a colour of the cooked palette to see which source
  colours of which images became it, and change it with R, G and B (0-31). The change is saved
  as an override next to the image (`tileset.png.bmasset`, for this profile or for all;
  [docs/spec/asset-model.md](docs/spec/asset-model.md)), never in the image itself; the room
  cooks again at once and, with Live on, the running game shows the new colour. Reset removes
  the override.
- **Animation** (centre tab): sprites ([docs/spec/sprite-format.md](docs/spec/sprite-format.md)).
  New sprite makes one from an image of the project; the sheet shows its frames (Slice cuts it
  by a grid), with rectangle, anchor and hitbox per frame; animations are lists of steps (a
  frame shown for a number of game frames). The preview plays from the cooked sprite and shows
  the hardware sprite pieces and what the sprite costs. Boxes (hurt, hit, guard): the list
  shows the frame's own and those it gets from the sprite; Scope moves one between the frame
  and the whole sprite. In the preview a click selects a box, a drag inside moves it, a drag at
  an edge or corner resizes it, a drag where there is none draws a new one (pressing in the
  preview stops the animation at the frame being edited). Edits are saved to the `.bmsprite`
  file and, with Live on, pushed into the running game.
- **Script** (centre tab): the project's Mondskript files
  ([docs/spec/mondskript.md](docs/spec/mondskript.md)) in a code editor. All scripts are
  compiled while typing; problems are marked on their lines and listed (click to go there),
  the outline shows the class as the runtime gets it, and a list shows what scripts can call.
  Files are saved shortly after an edit; when everything compiles and Live is on, the running
  game gets the new scripts. Objects in a room whose class has a script run it; New script
  starts one from a template.
- **Sound** (centre tab): songs and sound effects
  ([docs/spec/sound-format.md](docs/spec/sound-format.md)). The piano roll shows the selected
  track's notes (click to add, drag to move, drag the right end for the length, right click to
  remove); each track has an instrument (a wave or a WAV sample, envelope, volume, pan); the
  toolbar sets tempo, length, loop and echo. Play previews through the engine's own mixer, so
  it is what the device plays. "What it costs" shows voices, an estimate of the GBA's processor
  time and the size. Edits are saved and, with Live on, pushed into the running game. A room's
  `music` property names its song (`none` fades the music away); an instrument's `layer` makes
  dynamic music that scripts bring in with `music_intensity` (the moon bat's song grows in four
  layers as the fight does). The track list shows each track's layer and whether it is heard
  at the intensity chosen to preview; Mute and Solo pick tracks to hear, Fade fades the song
  out as a room's `none` does, and Speak plays a text as a dialogue voice would (a sound
  effect per letter). Effects called `jump` and `hurt` play by themselves, others
  from scripts with `play_sound(#name)`.
- **Graphics** (centre tab): what is loaded in video memory and how much of the hardware it
  takes: all 96 KB of VRAM as tiles with the maps of each background, overlaps in red and data
  nothing shows; the backgrounds, the sprites in OAM, both palettes, sprite time per line, and
  budget bars for background VRAM, screen blocks, sprite VRAM, OAM, palette banks and line
  tables. It shows the editor's preview, or the running game: Capture (F8) reads the video
  state over the live link (PC or GBA), Follow repeats that every second.
- **Data** (centre tab): the records (items, enemies, speakers and lines of dialogue), a table
  at a time, as a list and a form; every change is saved to the `.bmdata` file and cooked at
  once. A preview shows a record's sprite, for a line of dialogue the speaker's portrait, name
  and words. Right click a record for new, duplicate and delete.
- **Right click** opens a context menu: on the Project panel's entries (where to edit them),
  on an object in the Room view (its script, the records and dialogue lines it uses) and on a
  record.

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
build/bmcook -o level.bmpk assets/test_room/room.tmx assets/test_room/room2.tmx \
  assets/test_room/*.bmsprite assets/test_room/scripts/*.ms assets/test_room/music/*.bmsong
build/game level.bmpk --autoplay --trace --frames 700
```

For the GBA the scripts can also be compiled to machine code, packed next to the bytecode
(`--native-elf` keeps the module with debug information for GDB):

```
build/bmcook --native arm-none-eabi-gcc --native-elf scripts.elf -o level.bmpk ...
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
build/bmlink --tcp 7414 ppu-snapshot vram.bmps   # its video state (build/bm_ppu_render renders it)
```

The PC game can play along with the GBA: it takes the GBA's state and then the GBA's keys for
every frame, so its window shows what happens on the device, a moment later
([docs/spec/link-protocol.md](docs/spec/link-protocol.md) §10):

```
build/game level.bmpk --follow &                       # waits for the GBA
build/bmlink --xboo /dev/parport0 mirror from 7414     # until Ctrl-C; "to": the GBA follows the PC
```

In the editor: Run on the GBA, then the Sync buttons next to it (Off, GBA > PC, PC > GBA).

GBA to GBA: with two GBAs on a link cable, Select + Start in the cartridge game brings the
other one to the same room and position. A GBA without a cartridge is booted first, with the
game's own multiboot image ([docs/spec/link-protocol.md](docs/spec/link-protocol.md) §9; so far
only run between two emulated GBAs: `build/bm_mgba_pair build-gba/game_send.gba
build-gba/bootstrap_mb.gba`).

GDB on the running GBA game, with breakpoints on script lines (the scripts built as a native
module, see above; [docs/spec/link-protocol.md](docs/spec/link-protocol.md) §8):

```
build/bmlink --xboo /dev/parport0 gdb 2345
arm-none-eabi-gdb build-gba/game.elf -ex "target remote :2345" \
  -ex "add-symbol-file scripts.elf &bm_native_slot" -ex "break bat.ms:14"
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
