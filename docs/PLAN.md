# Blutmond: research and plan

Status: 2026-11-10. This is the starting plan for Blutmond, an engine for Metroidvania games
on the Game Boy Advance, with an editor in the spirit of Lunar Magic, RPG Maker or
Point&Click DK. Later extensions (RPG) are kept in mind but are not part of the first
milestones.

Contents:

1. [Goals and constraints](#1-goals-and-constraints)
2. [State of the repository](#2-state-of-the-repository)
3. [Architecture overview](#3-architecture-overview)
4. [Memory budget on the GBA](#4-memory-budget-on-the-gba)
5. [Graphics: virtual PPU, HDMA as shaders](#5-graphics-virtual-ppu-hdma-as-shaders)
6. [Runtime, content packs and a stable ABI](#6-runtime-content-packs-and-a-stable-abi)
7. [Live content: load the runtime once, push content](#7-live-content-load-the-runtime-once-push-content)
8. [Game state, save states, link transfer](#8-game-state-save-states-link-transfer)
9. [Transports: Xboo, Raspberry Pi, GBA to GBA](#9-transports-xboo-raspberry-pi-gba-to-gba)
10. [Scripting language](#10-scripting-language)
11. [Emulation layer for the PC](#11-emulation-layer-for-the-pc)
12. [GDB stub](#12-gdb-stub)
13. [Sound: research, target and tools](#13-sound-research-target-and-tools)
14. [Assets: portable model and pipeline](#14-assets-portable-model-and-pipeline)
15. [Editor and UI toolkit](#15-editor-and-ui-toolkit)
16. [Showing hardware limits in the editor](#16-showing-hardware-limits-in-the-editor)
17. [Ports: GB, SNES, PC, browser](#17-ports-gb-snes-pc-browser)
18. [Build, tests, toolchain](#18-build-tests-toolchain)
19. [Milestones](#19-milestones)
20. [Risks, decisions, open questions](#20-risks-decisions-open-questions)
21. [Sources](#21-sources)

---

## 1. Goals and constraints

| Area | Requirement |
|---|---|
| Runtime | C (C11, freestanding subset), runs on the GBA; no hardware addresses in engine code |
| Portability | PC, browser; later GB, SNES (see [17](#17-ports-gb-snes-pc-browser) for what is realistic) |
| Editor | C++, classic look, toolkit taken out of OpenSE (`engine/ui`), no external UI libraries |
| Graphics on PC/web | OpenGL ES 2.0 / WebGL 1 only; HDMA effects done in shaders |
| Scripts | simple language, text and node views that can be toggled, bytecode, AOT through C to ARM, relocatable, can run from IWRAM |
| Emulation | game logic testable on the PC |
| Replaceability | runtime can be swapped, content pack can be moved (ROM or RAM) |
| Live content | the runtime is loaded once; after that the editor only pushes new content, no GBA reset |
| Link | state and content move quickly over the link cable / multiboot |
| Save states | the developer sees exactly which fields are saved and which are not |
| Debugging | GDB stub |
| Sound | feature parity with the strongest GBA sound engine; own "LMMS lite" editor |
| Hardware awareness | palettes, modes, VRAM, OAM and CPU time are visible, without restricting too much |

The idea behind the whole plan: **the hardware is described as data** (registers, palettes,
tile maps, per-scanline tables, sound channels). On the GBA that data goes into the registers;
on the PC and in the browser a software or shader PPU and the same C mixer play it back.
Editor, emulation, tests and ports all work on this data.

## 2. State of the repository

Present (about 1300 lines of C):

- A Vulkan-like API: `BmPhysicalDevice` → `BmDevice` with function pointers,
  `bmAllocateMemory`/`bmMapMemory`/`bmFreeMemory`, `sType`/`pNext` in create infos.
- Memory heaps EWRAM/IWRAM/VRAM with a block allocator (`device_gba_emu.c`), a `malloc`
  device, a type manager and a pool (slab) with integer handles.
- First graphics pieces: `BmColor` (BGR555), backdrop colour, `BmTilemap`.
- One test (`pool_test.c`).

Found while reading (not built in this session):

| Where | Finding | Proposal |
|---|---|---|
| `backdrop.c` | writes straight to `0x05000000` | go through the HAL (section 3) |
| `device_gba_emu.c` | on the PC, maps memory at the real GBA addresses with `mmap(MAP_FIXED)`; `g_memoryBlocks` sits at a fixed EWRAM address | does not work in WebAssembly, Windows, macOS, and often not with ASLR; replace with normal allocation plus **offsets/handles** |
| `tilemap.c` | calls `bmPoolInit` with 4 arguments, the function takes 5 | should not compile |
| `pool.c` | `bmMapMemory(..., pool->elementCount * size, ...)` with `elementCount == 0` | map the capacity, not the count |
| `pool.c` | `bmPoolFinalize` calls `free` on indices `0..elementCount`, not on the used slots | used bitmap or a generation per slot |
| general | pointers in engine structs | unsuitable for save states, link transfer and live reloads, see [7](#7-live-content-load-the-runtime-once-push-content) and [8](#8-game-state-save-states-link-transfer) |

The Vulkan shape (create infos, explicit memory binding) fits the goal "the developer sees the
hardware". I would keep it for **creation and resources**, but use lean functions without
`sType`/`pNext` and without function pointer indirection on the per-frame path (Thumb code
running from ROM is slow, and every indirect call costs).

## 3. Architecture overview

```
┌──────────────────────────── Editor (C++) ─────────────────────────────┐
│ Project · World map · Room · Tiles/palettes · Sprites · Scripts       │  ose-ui toolkit
│ (text ⇄ nodes) · Sound (piano roll, instruments) · State · Hardware · │  GLES2/WebGL1
│ Build & run · Live link · Debugger                                    │  SDL3
└──────────┬──────────────────────────────┬─────────────────────────────┘
           │ compiler: script → bytecode  │ packer: assets → pack
           │           script → C → ARM   │
┌──────────▼──────────────────────────────▼─────────────────────────────┐
│ Pack (.bmpk): tiles, palettes, rooms, songs, script modules, schema   │  position independent
└──────────┬────────────────────────────────────────────────────────────┘
┌──────────▼───────────── Runtime (C) ──────────────────────────────────┐
│ Game: rooms, entities, physics (fixed point), camera, animation       │
│ Services: script VM + module loader, state, save states, live link,   │
│           sound, stream reader                                        │
│ Graphics API: layers, sprites, palettes, raster effects (line tables) │
├───────────────────────── HAL (C interface) ───────────────────────────┤
│ video · audio · input · timer · sio · save · storage · debug          │
├────────────┬──────────────────────┬───────────────────────────────────┤
│ gba        │ host (PC/web)         │ later: snes, gb (reduced)        │
│ registers, │ virtual hardware:     │                                   │
│ DMA, IRQ   │ SW PPU / GLES2 PPU,   │                                   │
│            │ C mixer → SDL3 audio  │                                   │
└────────────┴──────────────────────┴───────────────────────────────────┘
```

- **HAL**: the only layer that knows the hardware. On the GBA one file per part
  (`hal_gba_video.c` and so on); register access wrapped in `static inline` functions so there
  is no overhead. Addresses live only there (and in the linker script).
- **Runtime**: platform independent C, fixed point only (deterministic on every target, see
  [11](#11-emulation-layer-for-the-pc)).
- **Pack**: all data that belongs to a game or level, referenced only by offsets.

## 4. Memory budget on the GBA

| Region | Size | Bus | Plan |
|---|---|---|---|
| BIOS | 16 KB | – | SWIs for division, LZ77/Huffman decompression, multiboot |
| IWRAM | 32 KB | 32 bit, 1 cycle | ARM code: mixer, script dispatch, collision, IRQ, live-link handler (≈ 10 KB); stack (≈ 3 KB); script overlays (≈ 8 KB); hot state (≈ 4 KB); mixer buffers (≈ 3 KB); rest free |
| EWRAM | 256 KB | 16 bit, 3 cycles | multiboot case: runtime (target ≤ 96 KB Thumb) + content of the current level + state; cartridge case: state, decompressed room data, stream buffers |
| VRAM | 96 KB | 16 bit | BG char blocks/screen blocks, OBJ tiles (16 or 32 KB depending on the mode) |
| Palette | 1 KB | 16 bit | 16×16 BG + 16×16 OBJ, BGR555 |
| OAM | 1 KB | 32 bit | 128 sprites, 32 affine matrices |
| ROM | up to 32 MB | 16 bit, wait states | runtime + all packs (cartridge case) |
| SRAM/flash | 32–128 KB | 8 bit | save games |

Multiboot loads at most 256 KB into EWRAM. "The runtime fits into RAM" therefore means:
runtime + one level of content + state ≤ 256 KB. The earlier SceneLoaderGBA project could
multiboot archives up to 215.7 KiB on a Movie Player v2, which shows the margin is real but
not large. I think ≤ 96 KB for the runtime is doable if it is built as Thumb and only the hot
paths are ARM in IWRAM. A size budget per module goes into CI (section 18) so it does not
grow unnoticed.

## 5. Graphics: virtual PPU, HDMA as shaders

### Model

The runtime describes every frame as a **PPU state**:

- Mode (0–5); per BG: char block, screen block, size, colour depth (4/8 bpp), priority, scroll
  or affine parameters, mosaic.
- OAM (sprites, affine matrices), palettes, windows (WIN0/WIN1/OBJ/outside), blending
  (BLDCNT/BLDALPHA/BLDY).
- **Line table**: for each of the 160 lines, optional new values for selected registers
  (scroll, affine reference point/matrix, window bounds, blend coefficients, backdrop colour).
  This is exactly what HDMA does on the GBA.

Raster effects in the editor (wave, heat shimmer, per-line parallax, backdrop gradient,
spotlight via windows, "mode 7" floor) are generators that fill this table. The developer can
also edit the table directly as curves.

Affine effects on single objects, such as the door from SceneLoaderGBA that opens by scaling
in X, are tables computed by the packer (at build time, like the compile-time tables there)
and played back per frame.

### Running it on each target

| Target | How |
|---|---|
| GBA | registers written in VBlank; line table through DMA0 in HBlank mode (one channel per register group; DMA0 has priority over the sound DMAs 1/2) |
| PC/web, shader PPU | tiles as an 8-bit index texture (`LUMINANCE`), palettes as a 256×2 texture, tile maps as a texture, **line table as a 1×256 texture**; one fragment shader composes layers, priority, windows and blending per pixel |
| PC/web, software PPU | reference in C, pixel exact; produces golden images for tests and serves as fallback |

GLES 2.0 / WebGL 1 has no integer textures, no `texelFetch`, limited NPOT textures and no
guaranteed `highp` in fragment shaders. Therefore:

- all textures power of two, `NEAREST`, `CLAMP_TO_EDGE`; indices recovered with
  `floor(v * 255.0 + 0.5)`;
- check `highp` (`GL_FRAGMENT_PRECISION_HIGH`); without it, fall back to the software PPU;
- render in one pass to 240×160, then scale up with an optional LCD shader.

The software PPU costs very little on a PC and in WASM at 38 400 pixels per frame. It is the
reference; in tests the shader PPU must match it pixel for pixel.

### PPU snapshots

SceneLoaderGBA could copy a captured VRAM/OAM/register dump (cut from an emulator save state)
straight to the hardware. Blutmond keeps that idea as a **PPU snapshot** format (registers,
VRAM, palettes, OAM, line table). It is used for:

- golden image tests (snapshot → software PPU → PNG);
- a "freeze frame" in the editor: grab the PPU state from the running GBA over the live link
  and inspect layers, palettes and sprites;
- importing reference scenes from emulator save states.

## 6. Runtime, content packs and a stable ABI

Goal: fix engine bugs after the fact without rebuilding games, and place packs wherever there
is room (ROM, EWRAM).

- **Pack format**: a tagged chunk format, as in SceneCompiler's `GARCHIVE` (`PALE`, `MTIL`,
  `INDX`, `PIXL`, `AMAP`), extended with a header (magic, format version, ABI version, hash)
  and a table of contents. Chunks are padded to 32-bit alignment. Every reference is an offset
  from the start of the pack. Data stays compressed (LZ77 in BIOS format so `LZ77UnCompVram`
  can unpack straight to VRAM; LZ4 as a faster option for EWRAM).
- **Order matters**: SceneLoaderGBA found that reading the whole archive at once is fast and
  seeking to and reading assets one at a time is slow. Packs are therefore laid out **in load
  order**: one room's chunks lie next to each other, and the runtime reads them as one
  sequential stream, buffering only the chunks still in use. Random access is only for small
  tables that stay in RAM.
- **Runtime API table**: the runtime exports a versioned table of function pointers (`BmApi`,
  like a syscall table). Script modules receive a pointer to it when they are loaded. New
  functions are only appended; the major version only goes up on breaking changes.
- **Nothing in a pack calls the runtime at a fixed address.** A new runtime can load old packs,
  and a pack can live in ROM or RAM.
- **Storage back ends** behind one reader interface (the C version of SceneLoaderGBA's
  `AssetLoader<File>` with `MemoryFile`, `InputStreamFile`, `FatFile`): ROM/RAM, live link,
  and CF/SD on flash carts such as the Movie Player v2 (with a read buffer of a few KB, as the
  4 KiB buffer in `FatFile`).

## 7. Live content: load the runtime once, push content

The runtime is uploaded (or started from the cartridge) **once**. After that the editor only
pushes new content; the GBA is not reset. For that the runtime has to know exactly where its
state is, so it can reset it on its own.

### Memory regions with an owner

The runtime keeps a **region table**. Every byte of RAM, VRAM, palette and OAM belongs to
exactly one region:

| Region | Owner | On "load new content" |
|---|---|---|
| runtime code and data | runtime | kept |
| live-link handler, IRQ table, stack | runtime | kept |
| content area (packs in EWRAM) | content | replaced |
| script overlay slots (IWRAM/EWRAM) | content | unloaded, refilled |
| state arena | content | reset to `defaultproperties`, or kept/migrated (see below) |
| VRAM, palettes, OAM, line tables | runtime, filled from content | cleared, rebuilt by the room loader |
| sound voices, mixer buffers, DMA/timers | runtime | voices stopped, mixer reset, songs from the new pack |
| per-pack caches (tile cache, collision cache) | runtime | dropped |

Rules that make this safe:

- The runtime never holds raw pointers into content. It holds handles (index + generation) and
  offsets. Bumping the generation on reload invalidates every old handle at once.
- Content never registers its own interrupt handlers or DMA channels; it asks the runtime
  through the API table. On reload the runtime can therefore switch all of them off.
- The live-link handler runs on the SIO interrupt with high priority, so the editor can reach
  the GBA even when the game logic hangs in a loop (bytecode) or is paused.
- A reload happens at a frame boundary: stop the frame, mute sound, blank the screen (as
  SceneLoaderGBA blanks while loading), apply, restart.

### Reload modes

| Mode | What is sent | What happens to the game state |
|---|---|---|
| **Restart content** | changed pack chunks | state arena reset, game starts at the chosen room/save point |
| **Hot swap** | changed chunks, schema hash unchanged | state kept; the current room is reloaded around the player |
| **Hot swap with migration** | changed chunks + migration table | the editor knows the old and new schema and sends a table (copy field X from offset A to B, fill new fields with defaults); the runtime applies it |
| **Script swap** | one script module | module replaced in its slot; entities in a state whose code changed are put back to the start of that state |
| **State only** | state arena (or part of it) | jump to a saved situation, e.g. "player in front of the boss with all upgrades" |

What remains as a risk: AOT-compiled native code can overwrite runtime memory, and then only a
reset helps. During development the editor therefore runs scripts as **verified bytecode** by
default; AOT modules are an option for performance tests and release builds. A watchdog
(timer IRQ that checks a frame counter) reports a hang to the editor instead of freezing.

## 8. Game state, save states, link transfer

### State as a schema

All mutable game state lives in a **state arena** (EWRAM, a hot part in IWRAM) **without
pointers**: references are handles (index + generation) or offsets. The state can be copied
with `memcpy` and has the same layout on every target.

The layout is not written by hand in C. It comes from script classes (section 10) and engine
types and is described as a schema. Every field carries flags:

| Flag | Meaning |
|---|---|
| `save` | goes into the save game (SRAM/flash) |
| `link` | is sent on link transfer |
| `snapshot` | part of the save state (debugging, resetting) |
| `transient` | not saved; reset on load |
| `derived` | recomputed after loading (e.g. collision cache) |

A generator produces C headers (`struct`s with `_Static_assert` on offsets and sizes),
serialisers, migration tables (section 7) and a **state map**. The editor shows the map as a
table and as a memory bar: field, type, offset, bytes, flags, how much of it is saved and
transferred. Fields without a flag are clearly marked, so "forgot to save it" stands out.

### Transfer between two GBAs

| Path | Raw rate | Use |
|---|---|---|
| BIOS multiboot (SWI 0x25) | slow, several seconds for large images | only a small bootstrap (2–4 KB) |
| Normal mode 32 bit, 2 MHz | 2 Mbit/s ≈ 256 KB/s | own protocol: runtime, pack, state |
| Normal mode 32 bit, 256 kHz | ≈ 32 KB/s | fallback for unreliable cables |
| Multiplayer mode | up to 115 200 bit/s | multiplayer in the game |

Flow: bootstrap via BIOS multiboot → bootstrap switches to normal mode at 2 MHz → blocks with
CRC and retry → runtime (unless on a cartridge), pack, then only the `link` fields of the state
(compressed, optionally as a delta to a known state). 256 KB in theory take a little over a
second; the real number has to be measured on hardware.

## 9. Transports: Xboo, Raspberry Pi, GBA to GBA

The editor talks to the GBA through one **transport interface** (`bm_link`): open, exchange
32-bit words, optional reset, measured rate. The packet protocol on top (upload, live
reload, state, log, GDB) is the same for every transport.

| Transport | Who makes the clock | Expected rate | Status |
|---|---|---|---|
| **Xboo cable on the parallel port** (Linux `ppdev`) | PC, in software | low, estimated 10–30 KB/s, to be measured | cable and driver exist (xcomms, branch `parport`) |
| **Raspberry Pi** SPI (e.g. as in gba-remote-play) | SPI hardware | close to the hardware limit of normal mode (≈ 2 Mbit/s raw) | optional, uses existing projects as reference |
| **RP2040 / Pico** over USB (e.g. gba-multiboot-pico) | PIO/SPI hardware | close to the hardware limit | optional |
| **GBA to GBA** | one GBA | 2 Mbit/s | section 8 |
| **Host build** | – | memory copy | for tests: the editor talks to the PC runtime directly |
| **mGBA** | – | – | via its GDB stub/scripting, for debugging |

### Why the parallel port is the slow one

The GBA's serial port in normal mode accepts an external clock of up to about 2 MHz, so the
GBA side is not the limit. With the Xboo cable the PC has to produce every clock edge itself:
each edge is a write to the parallel port control register, and with `ppdev` each write is a
system call plus an I/O access on the slow legacy bus. `XbooExchange32` in xcomms does **four
`ioctl` calls per bit** (3× `PPWCONTROL`, 1× `PPRSTATUS`), `XbooSend32` three. At a few
microseconds each that is roughly 10–30 KB/s, about a tenth of what the GBA could take. A
Raspberry Pi or RP2040 generates the clock in hardware and gets close to the GBA's limit.

Possible improvements on the parallel port side:

- set the data bit and the falling clock edge in **one** write, then the rising edge: two
  writes per bit when sending instead of three;
- do not read the status when only sending;
- **double speed burst** over the SD line (/SELECT): two bits per clock. Only used when the
  editor detects the fifth wire at connect time (it sends a test pattern over SD); without it
  everything runs on the four basic wires;
- measure `ioperm` + `outb` (root) against `ppdev`, as an option for measurements.

### Robustness: wherever xcomms works, Blutmond works

The baseline is the plain four-wire Xboo cable (SC, SI, SO, GND) as documented for xcomms:
no SD wire, no reset wire required, any PC and cable length on which xcomms boots a GBA.
Nothing in the protocol may need more than that, and it has to live with some bit errors:

- **Initial boot** uses the same BIOS multiboot sequence as xcomms `NormalBoot` (handshake
  `0x6202`/`0x72`, header, key exchange, CRC), with the same per-word delay setting. If xcomms
  can boot the device, so can the editor.
- **Packets** after that: sync word, type, sequence number, length, payload, CRC-32. The GBA
  answers every packet with ACK or NAK (with the sequence number it expects).
- **Retry**: on NAK, bad CRC or timeout the packet is sent again; after several failures the
  link goes back to a resync pattern and continues where it stopped. A transfer never has to
  start from the beginning, because chunks are confirmed one by one.
- **Adaptive delay**: start with a safe delay per bit (as xcomms' "Transfer Delay"), lower it
  while transfers are clean, raise it again on errors. The value that works is remembered per
  device and transport.
- **Applying is atomic**: a live update (section 7) is only applied after all its chunks have
  arrived and been checked. A broken transfer leaves the running game untouched.
- **Error counters** (CRC errors, retries, resyncs, effective rate) are shown in the editor's
  live-link window, so a bad cable is visible instead of mysterious.

Because this link is slow, the live workflow (section 7) is built to **send little**:
compressed chunks, only chunks whose hash changed (the editor remembers per device what is
already there), and the runtime stays on the device. A changed script module or room is a few
KB and arrives in a fraction of a second even over the parallel port.

### Building on xcomms

- xcomms already has console output and file access from the GBA to the PC
  (`xcomms_cmd.h`: `PRT`, `OPN`, `FRD`, `FWR`, `GTC` ...). Blutmond uses the same cable for its
  own packet channel.
- Because the PC makes the clock, the GBA cannot send on its own: the PC polls, and the GBA
  leaves its answer in the SIO data register. A word from the PC raises the SIO interrupt on
  the GBA, which is also how the live-link handler and the GDB stub are reached.
- The parallel port code is rewritten for Blutmond as a small library (`xboo`) with
  `parport.cxx` as the template; the FLTK user interface of xcomms is not needed.
- **Reset is a setting** per device: `none` or `xboo` (the reset wire from the original Xboo
  documentation, as used by xcomms `ResetGBA`). The live workflow never needs it. When it is
  set, the editor uses it to upload a new runtime, and to recover when the runtime no longer
  answers (after a crash in AOT code, or when the watchdog reports a hang). With `none` the
  editor asks the user to switch the GBA off and on instead.

## 10. Scripting language

Working name **Mondskript**. Goals: easy for beginners, fast on the GBA, serialisable. The
model is the old **UnrealScript** (classes with declared fields, states as a language feature,
latent functions), with a syntax that does not feel foreign to GDScript users.

### Taken from UnrealScript

- **Classes with a fixed layout**: all fields are declared with `var`; the layout is known at
  compile time. The compiler turns it into C `struct`s and entries in the state schema
  (section 8).
- **Field specifiers** as in UnrealScript (`var(Combat) int Health;` shows up in the editor
  under "Combat"). The storage flags from section 8 are specifiers too: `save`, `link`,
  `transient`, `derived`.
- **`defaultproperties`** as a block with initial values; the editor edits it as a form. It is
  also what the runtime resets the state to on "restart content" (section 7).
- **States** (`state Patrol { ... }`, `gotostate Attack;`) with **latent functions**
  (`sleep(30)`, `finish_anim()`, `move_to(p)`). The compiler turns them into state machines;
  the current state and the position in the state code are ordinary fields in the schema. Save
  states, link transfer and live reloads therefore work even in the middle of a cut scene.
- `event` functions that the runtime calls (`event touch(Actor other)`, `event tick()`).

### Done differently on purpose

| UnrealScript | Mondskript |
|---|---|
| strings everywhere, concatenation at run time | no run-time string type. `"..."` are constants in a string table (translatable); text in the game comes from dialogue assets. `name` is an ID assigned at compile time |
| floats | integers and fixed point only (`fix`, 24.8, optionally 16.16); bit exact on every target |
| every function virtual | functions are `final` by default; virtual only with `virtual`. Saves an indirect call per call on the GBA |
| deep inheritance chains (`Actor → Pawn → ...`) | at most one engine base class plus **components** (`has Health, Gravity;`) so abilities can be combined |
| `None` access fails at run time | references are handles with a generation; `Enemy?` has to be checked before use (`if e := target { ... }`), the compiler enforces it |
| `foreach AllActors(...)` searches everything | queries through runtime indices: room, group, type, spatial grid |
| case insensitive | case sensitive |
| dynamic arrays | fixed arrays (`int[8]`), the size is part of the type; fixed-capacity lists from the engine |
| replication as a language feature | dropped; what gets transferred is decided by the field flags |

### Example

```
class Bat extends Enemy;
has Health, Flight;

var(Combat) int Damage = 1;
var(Flight) fix Speed  = 1.5;
var save      bool Defeated;    // goes into the save game
var transient int  Blink;       // not in the save state

state Sleeping {
  event sees(Player p) { gotostate Attacking; }
}

state Attacking {
begin:
  play_anim(ANIM_WAKE_UP);
  finish_anim();
  loop {
    move_toward(player().pos, Speed);
    sleep(2);
  }
}

event dies() {
  Defeated = true;
  spawn(Coin, pos);
}

defaultproperties {
  Health.max = 3;
  start_state = Sleeping;
}
```

Calls into the runtime only go through the API table.

### Text ⇄ nodes

Both views show **one** syntax tree. The text is what is stored; node positions live in a
side file, matched through stable node IDs. Switching is lossless. Nodes show statements as
blocks with flow pins (like Blueprints) and expressions as small function nodes. What does not
read well as nodes (long expressions) stays as an embedded text node.

### Compilation

```
source ─► parser ─► AST ─► type check ─► IR ─┬─► bytecode   (interpreter: PC, browser, GBA during development, hot reload)
                                             └─► C ─► gcc ─► ARM/Thumb module (GBA, AOT)
                                                   └─► host compiler ─► native module (PC tests)
```

- **Bytecode**: register based, 16-bit instructions, interpreter loop in IWRAM (ARM). Verified
  when loaded (jump targets, slot indices, handle types), so a broken module cannot corrupt
  the runtime. Runs everywhere and is the default for live reloads (section 7).
- **AOT**: the C code uses no global variables (only a context pointer) and calls the runtime
  through the table. Built with `arm-none-eabi-gcc -mthumb` (or `-marm` for IWRAM), linked at
  address 0 with `--emit-relocs`. A packer reads the ELF relocations, keeps only `R_ARM_ABS32`
  and rejects everything else, and writes a small module with a relocation list. The module
  loader copies it into an IWRAM or EWRAM slot and patches the addresses. The overlay manager
  keeps slots and reference counts and loads modules after room changes.
- **Debugging**: the generated C contains `#line` directives pointing at the script file, so
  GDB shows script lines directly.
- The browser editor has no GCC: there only bytecode (or AOT on a build server).

## 11. Emulation layer for the PC

No mapping at the real GBA addresses (see section 2), but a **host back end of the HAL** that
models the hardware as data structures: VRAM, palette, OAM, registers, line table, timers,
sound FIFOs, keys, SIO.

- Game logic runs natively (x86-64/WASM); scripts in the interpreter or as native modules from
  the same generated C.
- Because the runtime only uses fixed point and defined integer widths, the logic is bit exact
  with the GBA. This is checked with input recordings: the same run on the host and in mGBA
  (headless) must give the same state hash every frame.
- Picture through the software or shader PPU, sound through the same C mixer as on the GBA.
- The live workflow from section 7 also works against the host build, so the editor uses the
  same code path with or without a GBA attached.
- Timing: the host can keep a cycle estimate per module; real budgets are only measured on the
  GBA or in mGBA (section 16).

## 12. GDB stub

Three ways in, all over the GDB Remote Serial Protocol:

1. **Host build**: normal native debugging with gdb/lldb, plus a script debugger in the editor
   (breakpoints on script lines in the interpreter).
2. **mGBA**: has a built-in GDB stub; the editor starts mGBA with `-g` and connects
   `arm-none-eabi-gdb`.
3. **Real hardware**: own stub in the runtime (≈ 2–3 KB, IWRAM), over the transport from
   section 9:

   ```
   arm-none-eabi-gdb ──TCP──► editor / bm-link bridge ──ppdev──► parallel port ──Xboo──► GBA (stub)
   ```

   The bridge turns GDB packets into link packets. Since the PC makes the clock, it polls the
   GBA; a word from the PC raises the SIO interrupt on the GBA, which is also how Ctrl-C
   arrives. At 10–30 KB/s that is fast enough for GDB (registers and small memory ranges).
   A Raspberry Pi or RP2040 transport works the same way without changing the stub.

   Packets: `g G m M c s Z0 z0 ?` plus `qSupported` and `monitor` commands (state map, load
   pack, PPU snapshot). The ARM7TDMI has no debug hardware the stub can use: breakpoints are
   set by patching code in RAM; single steps use a temporary breakpoint on the next
   instruction (branch targets are decoded). Code in ROM cannot be patched, so debug builds run
   from EWRAM (multiboot). How the break is triggered (BIOS vector for undefined instructions
   vs. a branch into the stub) has to be checked on hardware.

Smaller helpers from SceneLoaderGBA that carry over: an assert that shows file, line and
message on screen and waits for a key (and also reports over the link), and a hex print for
the on-screen console.

## 13. Sound: research, target and tools

### Engines compared

| Engine | Origin | What is documented |
|---|---|---|
| **m4a / MP2000 ("Sappy")** | Nintendo SDK, used by most commercial games | linear interpolation, echo with a fixed delay, known to be noisy |
| **ipatix HQ mixer** | replacement mixer for m4a, based on the Golden Sun engine | higher mixing rate, reverb; according to third parties Golden Sun reverb and synth channels |
| **Krawall** | Sebastian Kienzl; open source since 2013 (LGPL 2.1) | XM/S3M player, very faithful effect handling, 16 or 32 kHz, IWRAM variants, worker at 60/30 Hz |
| **Maxmod** | devkitPro, open source | MOD/S3M/XM/IT, up to 32 channels |
| **MusyX** | Factor 5 | up to 32 sample voices, pulse width modulation synthesis, stereo delay buffer for reverb, surround encoding, dynamic and fixed voice allocation, up to 16 384 samples/SFX |
| **GAX** | Shin'en | built for low CPU load and quality; separate mixing rates for music and effects (5 735–42 049 Hz), FX slots with save/restore; details not public |

**Harry Potter and the Prisoner of Azkaban (GBA)** uses **Krawall**: the US and Japanese
releases are on the list of Krawall games (together with The Sims, LEGO Star Wars II, The Lord
of the Rings: The Third Age, Harry Potter and the Order of the Phoenix and others). Ian Stocker
arranged Jeremy Soule's music for the GBA; he worked in Impulse Tracker and converted to
Krawall.

**Which one is "the most impressive"** cannot be shown objectively; there is no published
comparison. GAX has the best reputation, but its specification is not public. Krawall has the
best mix of sound and **open, checkable code**, and Harry Potter 3 is a good reference to
listen to. The target is therefore the **union of the documented features**, with Krawall read
as the reference for effect accuracy and mixer technique. Krawall is not linked in: LGPL in a
statically linked ROM requires that users can relink; an own implementation avoids that.

### Feature target

| Group | Features | Modelled on |
|---|---|---|
| Mixer | 8-bit samples, 16-bit accumulator, selectable rate (≈ 10–32 kHz, separate rate for SFX), volume ramps against clicks, interpolation none/linear | GAX, Krawall, ipatix |
| Voices | up to 32 sample voices, dynamic and fixed allocation, priorities, voice stealing | MusyX, Maxmod |
| PSG | the 4 original channels (2× pulse, wave, noise) as instruments | m4a |
| Synth | pulse with PWM, saw, triangle, noise, wavetable with cross-fade; envelopes (ADSR, free), LFOs | MusyX, GAX |
| Tracker effects | vibrato, tremolo, portamento, arpeggio, retrigger, volume/pan slides, sample offset, envelopes, NNA | Krawall, Maxmod (XM/IT) |
| Effects | reverb/echo (stereo delay), one-pole low-pass per voice (optional), bit crush, distortion | MusyX, Golden Sun |
| SFX | own slots with priority, music ducking, save/restore state | GAX |
| Control | adaptive music: switch sections/layers from scripts, tempo, transpose | GAX |
| Import | MIDI, MOD/S3M/XM/IT | Maxmod, Krawall |

On the GBA: timer 0 drives, DMA1/DMA2 feed FIFO A/B, double mixing buffer in IWRAM, mixing
loop in ARM code in IWRAM. Target to measure: 16 voices at 18 kHz under 15 % CPU. The same C
mixer runs in the editor, so the preview sounds exactly like the device. With the live link
(section 7) a changed instrument or song can also be played on the real GBA without a reset.

### Sound editor ("LMMS lite")

- **Song**: tracks with clips, sections and loop points, tempo track.
- **Piano roll**: notes, velocity, automation lanes (volume, pan, filter, pitch).
- **Instruments** in a VST-style panel with knobs: source (sample, synth, PSG), envelopes,
  LFOs, filter, sample editor with loop points and conversion to 8 bit.
- **Effect rack** per bus.
- **Live CPU and memory display**: cost per voice and effect in percent of a frame, bytes for
  samples. Overruns are marked, not forbidden.
- **SFX editor** with priorities and preview in game context.
- Own sequence format (events instead of tracker rows), so piano roll and automation fit
  without detours.

## 14. Assets: portable model and pipeline

### Assets are portable, packs are per platform

Everything a developer makes in the editor must be portable to other retro platforms in
principle, even before the runtime is. That is why the project stores **source assets** in a
platform-neutral model, and the GBA data is only a **build output**:

```
source assets (project, platform neutral) ──cook for profile──► pack for GBA
                                          ├─────────────────► pack for SNES
                                          ├─────────────────► pack for GB/GBC
                                          └─────────────────► pack for PC/web
```

| Asset | Stored as (source) | Cooked for a platform |
|---|---|---|
| Images, tilesets | indexed PNG plus palettes in 24-bit colour, tile size 8×8, flip flags; the original true-colour image is kept for re-quantising | colour depth, palette count/size, colour format (BGR555, GB shades, …), tile dedup |
| Tile maps, rooms | grid of tile references with flip flags, layers with roles (foreground, background, parallax), collision as its own layer of types (solid, slope, platform, hazard…) | number of layers, map entry format, screen size, scrolling limits |
| Sprites, animations | frames as images, hitboxes, anchor, timing in frames | OBJ sizes and shapes, meta sprites, per-line limits |
| Raster effects | the effect as parameters (wave: amplitude, period, speed) plus the line table it produces | HDMA tables (GBA, SNES), shader (PC/web), or a simpler fallback where the platform has none |
| Entities, rooms, world map | text (JSON-like), with typed properties | packed binary |
| Scripts | Mondskript source | bytecode (portable) or native module |
| Music, SFX | event sequences plus instruments described by sound (sample, pulse with duty, wave, noise, envelopes, effects) | GBA mixer data; on platforms with sound chips (GB/NES/SNES SPC700) a mapping to their channels |
| Strings, dialogue | UTF-8 text with IDs | font and encoding of the platform |

Rules that follow from this:

- Source formats are open and documented (PNG, text, a small binary only where needed) and
  versioned, so assets can be converted later without the editor.
- The **cooker reports what it had to change** for a profile: colours merged, palettes split,
  layers dropped, sprites over the per-line limit, effects replaced by fallbacks. The editor
  shows this per platform, so a developer sees early what a port would cost.
- Platform-specific tweaks (a hand-made GB palette, a simpler effect for one target) are stored
  as **overrides** next to the source asset, never instead of it.
- The GBA-specific editor views (palette banks, VRAM map, OAM) work on the cooked result for
  the GBA profile; the source asset stays as it was.

### Rooms: layers, collision, physics, objects

A room is a Tiled map (`.tmx`, Tiled 1.8 or newer) or the same data made in the room editor.
Both are the source format; the cooker reads either. Everything below is plain Tiled, so rooms
can be edited without the Blutmond editor.

**Layers** carry a `role` property:

| Role | Content | Cooked to |
|---|---|---|
| `background` | tile layer, may use Tiled's `parallaxx`/`parallaxy` | BG layer with scroll factor (line table if it varies per line) |
| `main` | tile layer the player walks in front of | BG layer |
| `foreground` | tile layer drawn in front of sprites | BG layer with higher priority |
| `collision` | tile layer using the Blutmond collision tileset | collision grid in the pack, not drawn |
| `objects` | object layer | entity spawn table |

Layers without a role are ignored with a warning, so nothing is cooked by accident.

**Collision** is a grid separate from the graphics, cell size 16 px by default (room property
`collision_cell`). It uses a small tileset shipped with Blutmond (`blutmond_collision.tsx`);
each tile has a `collision` property:

| Type | Behaviour |
|---|---|
| `empty` | nothing |
| `solid` | blocks from every side |
| `platform` | one-way: solid from above, can be jumped through from below and dropped through (down + jump) |
| `slope_up_45`, `slope_down_45` | 45° floor; walking follows the surface |
| `slope_up_22_low/high`, `slope_down_22_high/low` | 22.5° floor made of two cells |
| `ladder` | climbable; up/down switches to climbing |
| `water` | swimming: room property `water_gravity`, slower movement |
| `hazard` | damage on touch (spikes) |

Graphics tilesets may also carry a `collision` property per tile; the cooker then fills the
collision grid automatically, and a hand-drawn collision layer overrides it. Room edges without
a `Door` are walls (`edges_solid`, default true); falling out of the bottom is handled by a
`KillZone`.

**Physics** is a simple platformer model, not a rigid-body simulation. All values are fixed
point (`fix`, 24.8) in pixels and frames; Tiled stores them as decimals, the cooker converts.

| Where | Property | Default |
|---|---|---|
| room | `gravity` | 0.25 px/frame² |
| room | `max_fall_speed` | 4.0 px/frame |
| room | `water_gravity` | 0.08 px/frame² |
| entity class (component `Body`) | `hitbox` (w, h, offset from the anchor) | per class |
| `Body` | `gravity_scale` | 1.0 (0 for flying enemies) |
| `Body` | `max_speed`, `accel`, `friction` | per class |
| player (component `PlatformerControl`) | `run_speed` | 2.0 px/frame |
| `PlatformerControl` | `jump_speed` | 5.3 px/frame (about 3.5 tiles high with the default gravity) |
| `PlatformerControl` | `jump_cut` (release early → lower jump), `coyote_frames`, `jump_buffer_frames` | 0.5, 6, 6 |

Per frame: apply input and gravity to the velocity, then move along X and resolve against the
grid, then along Y and resolve (slopes snap the feet to the surface, platforms only block when
falling onto them from above). Entities stand on moving platforms by being carried with them.
Entity against entity uses axis-aligned hitboxes and calls script events (`touch`,
`hit`), no pushing. The editor can check reachability: with the room's physics it draws the
jump arcs from every standing spot and marks floors that cannot be reached.

**Objects** are points or rectangles in the `objects` layer. The Tiled `type` (called `class`
from Tiled 1.9 on) is the Mondskript class; the custom properties are that class's `var(...)`
fields and override its `defaultproperties`. Points mark the anchor (bottom centre, where the
feet are). Each object keeps its Tiled object ID as a **persistent ID**: state that must survive
saving (`Defeated`, "collected", "door opened") is stored per persistent ID, so it does not get
lost when objects are added or moved.

Engine classes every room can use:

| Class | Shape | Properties |
|---|---|---|
| `PlayerStart` | point | `facing` |
| `Door` | rectangle | `target_room`, `target_door`, `direction` |
| `CameraZone` | rectangle | camera bounds while the player is inside; `lock_x`, `lock_y` |
| `KillZone` | rectangle | `respawn` (`last_ground`, `checkpoint`) |
| `Trigger` | rectangle | `event` (`on_enter`, `on_leave`, `on_use`), `call` (script), `once` |
| `SavePoint` | point | – |
| `Pickup` | point | `item`, `persistent` |
| `MovingPlatform` | rectangle + polyline path | `speed`, `wait_frames`, `loop` |

Everything else (enemies, lanterns, NPCs) is a game class written in Mondskript, such as the
`Bat` in section 10.

The test level (local only, see the test data decision in section 20) has these layers added:
roles on the existing three tile layers (background with parallax 0.5), a collision layer
(two ground parts, a floating ledge between two pits, a cliff with an overhang) and objects
(player start, camera zone, kill zone below the pits, door at the cliff top, save point,
pickup on the ledge, lanterns, a bat, a patrolling zombie, a music trigger).

### Ideas from SceneCompiler/SceneLoaderGBA

The earlier SceneCompiler/SceneLoaderGBA pipeline (Tiled maps → binary archive → multiboot
loader) solved several of these problems already. Blutmond takes over the **ideas**, not the
code: it is written from scratch in C and C++ to fit the model above.

| SceneCompiler / SceneLoaderGBA | In Blutmond |
|---|---|
| Tiled `.tmx` input, also with embedded tilesets | **Tiled import** in the editor (rooms, layers, tilesets, object layers → entities), so existing Tiled maps keep working; the own room editor stays the main tool |
| metatiles cut into 8×8 tiles, hashed, stored once | same, plus detection of **flipped** copies (H/V flip bits in the map entry) to save more VRAM |
| tiles not used in the map are skipped | same; the editor also shows unused tiles in a tileset |
| optional 3×3 colour matrix for the GBA screen (after Pokéfan531) | colour correction as a setting per project, with a side-by-side preview "PC colours / GBA screen"; also as a shader option in the browser build |
| palette generation: histogram per tile, merge similar colours with a distance threshold, sort by brightness, pack into 16×16, error if it does not fit | the same algorithm as an **interactive palette packer**: shows which tiles force which palette, what was merged, and *why* it does not fit (which tiles need too many colours), with suggestions (merge threshold, split tileset, 8 bpp layer). Pinned colours and palettes reserved for animation can be locked |
| magenta forced into slot 0, black after it, so slot 0 is never needed | slot 0 is **reserved explicitly** as transparent in the data model; the editor shows it as such, no magic colour needed in source images (magenta still accepted as a transparency key on import) |
| `GARCHIVE` tagged format, 32-bit aligned | basis of the pack format (section 6) |
| `AssetLoader<File>` with memory, stream and FAT back ends | C reader interface with ROM/RAM, live link and CF/SD back ends (section 6) |
| per-layer tile cache in VRAM with LRU eviction | **VRAM tile streaming** for rooms larger than VRAM: tiles are uploaded as the camera moves, LRU per char block; the editor shows how full the cache gets along a path through the room |
| loading is slow when seeking asset by asset | packs in load order, read sequentially (section 6) |
| software 4-way set-associative cache (`cache.h`) | candidate for the stream reader when a room does not fit into RAM; tested on the host first |
| `FunctionProfiler` with hardware timers, results on screen | **profiler** in the runtime: named timer zones, results sent over the live link to the editor's hardware view (section 16), on-screen as fallback |
| raw VRAM/OAM/register scene dump | PPU snapshot format (section 5) |
| debug output: palette `.hex`, mapping files, extracted tiles | editor views of the same data; export stays available for debugging the packer |

The pipeline moves from g++/boost/libpng/gdk-pixbuf to the editor's own code: PNG loading
through a single-header decoder (e.g. `stb_image`), no boost.

## 15. Editor and UI toolkit

### OpenSE toolkit (`engine/ui`)

Checked in the OpenSE repository: about 5 600 lines of C++, retained mode, XML layouts, themes,
docking, menus, lists, tables, text fields, drag and drop. Dependencies:

| Dependency | Effort |
|---|---|
| `core/log`, `core/math`, `core/util`, `fs/filesystem` from OpenSE | small: take along or replace with small versions |
| `rhi/rhi.h` | **medium**: the RHI only has a GLES 3 back end (VAOs, `#version 300 es`). For GLES 2.0/WebGL 1 the `CanvasRenderer` needs its own small back end: textured triangles, alpha blending, scissor. 32-bit indices need `OES_element_index_uint`, or batches with 16-bit indices |
| `stb_truetype` | header only, shipped with it |
| window/input | SDL3 (decided), see below |

Decided: the toolkit is split out as its **own library in its own repository,
[Fighter19/ose-ui](https://github.com/Fighter19/ose-ui)** (created, still empty); OpenSE and
Blutmond both use it. The toolkit itself does not depend on SDL: it receives input events
and draws through a small render interface (GLES2 and GLES3 back ends). SDL3 sits only in a
thin platform layer next to it (`ose-ui-sdl3`). Plus a "classic" theme: bevelled frames, pixel
font, fixed colours, in the style of Lunar Magic and Windows 9x tools.

### SDL3 in the build

- CMake first looks for an installed SDL3 (`find_package(SDL3 CONFIG)`). If it is missing,
  `FetchContent` downloads a **pinned version** (tag plus hash) of SDL3 and builds it
  statically. Can be switched off with `-DBM_FETCH_DEPS=OFF` for offline and distribution
  builds.
- Desktop: only the SDL parts that are used (video, events, audio, gamepad), the rest off, so
  the build stays short.
- WASM: Emscripten ships SDL3 as a port (`--use-port=sdl3`); CMake sets that when building
  with `emcmake`. Audio in the browser then goes through SDL3's WebAudio back end.
- Same approach for other parts without a system package (`stb_truetype`, `stb_image` live as
  headers in the repository).

### Editor windows

| Window | Content |
|---|---|
| Project | packs, rooms, assets, target platform profile |
| World map | grid as in Metroid/Castlevania: place rooms, connect doors, ability gates, generate the in-game map automatically |
| Room | tile layers, collision layer (with slopes), entities, camera regions, raster effects, preview with the real PPU, Tiled import |
| Tiles & palettes | import (PNG, Tiled), colour correction, quantisation to BGR555, tile dedup with flips, interactive palette packer (section 14) |
| Sprites & animation | frames, hitboxes, OBJ sizes, meta sprites, affine animations |
| Script | text and node views, switchable |
| Sound | section 13 |
| State | schema and state map (section 8) |
| Hardware | VRAM, OAM, palette and CPU budgets, profiler zones (section 16) |
| Live link | device, transport, rate, what is on the device, reload mode (section 7) |
| Build & run | host emulation, mGBA, real hardware |
| Debugger | breakpoints, variables, live state, frame step, PPU snapshot |

## 16. Showing hardware limits in the editor

Rule: **hard limits are enforced, soft limits are explained.** Every warning says what the
hardware does and which ways out there are.

- **Modes**: choose mode 0–5 with a table of what the mode allows (mode 0: 4 regular BGs;
  mode 1: 2 regular + 1 affine; mode 2: 2 affine; modes 3–5: bitmap, less OBJ VRAM). When
  switching, the editor shows what will stop working.
- **Palettes**: 16 banks of 16 colours each for BG and OBJ, colour 0 transparent. Shows which
  tiles use which bank, conflicts, colours that collapse in BGR555. 8 bpp with 256 colours as
  an alternative. Palette animation (colour cycling) as its own track.
- **VRAM map**: char blocks and screen blocks as tiles, overlaps in red, free space, streaming
  areas for large rooms.
- **OAM**: 128 sprites, 32 affine matrices; **scanline heat map** of sprite cost (1 210 cycles
  per line, 954 when "H-blank interval free" is set; a regular sprite costs its width, an
  affine one 2 × width + 10). Lines that drop sprites are marked.
- **Windows, blending, mosaic** with live preview.
- **CPU**: frame budget (280 896 cycles) split into logic, scripts, sound, PPU updates, measured
  in mGBA or on hardware through the profiler zones.
- **Memory**: IWRAM/EWRAM/ROM bars per module, with a warning when runtime + content no longer
  fit into 256 KB of multiboot.
- **Link**: size of each pending update and the expected time on the current transport.
- One profile per target platform; the profile decides which limits are shown.

## 17. Ports: GB, SNES, PC, browser

| Target | Realistic | How |
|---|---|---|
| PC (Linux/Windows/macOS) | yes | host back end of the HAL |
| Browser | yes | Emscripten, WebGL 1, SDL3 audio (WebAudio), scripts in the interpreter or as WASM from the generated C |
| SNES (next target) | partly | C compilers for the 65816 produce slow code; data formats, bytecode and interpreter can be ported, the runtime would probably need assembly parts; the PPU model fits well (mode 7, HDMA) |
| Game Boy / GBC | data only | 8 KB of RAM and an 8-bit CPU: the runtime does not fit. A reduced runtime (e.g. with GBDK) for simple games that reads packs in a GB profile is possible |

The runtime is written so it can be ported later (HAL, fixed point, no pointers in state, C
without compiler extensions outside the HAL). Independent of that, **all assets stay portable**
(section 14): even where a runtime port does not exist yet, a project's art, maps, music and
scripts can be cooked for another platform or taken to another engine.

The platform profile in the editor makes sure a project knows from the start which targets it
is meant for, and the cooker reports per profile what a port would change.

## 18. Build, tests, toolchain

- CMake with toolchain files: host (gcc/clang), GBA (`arm-none-eabi-gcc` from the Ubuntu
  packages, own `crt0`, linker script for cartridge and multiboot, header checksum; devkitPro
  is not required), Emscripten.
- Tests:
  - unit tests on the host (like `pool_test.c`), with ASan/UBSan;
  - golden images from the software PPU and PPU snapshots; the shader PPU must match;
  - determinism: input recording, state hash per frame, host against mGBA headless;
  - live reload: load pack A, play, push pack B in every reload mode, compare the state with
    the expected migration;
  - sound: mixer output bit exact between host and the GBA build in mGBA;
  - asset pipeline: palette packer and tile dedup against fixed inputs; cooking one project
    for two profiles (GBA and SNES) with the expected change report;
  - test level (from `BM_TESTDATA_DIR`): cook it, load it on the host, run a recorded input that
    crosses both pits and climbs the cliff, compare the state hash;
  - link robustness: a transport test double that flips bits and drops words at a set rate;
    transfers must still complete and apply correctly;
  - size budgets: runtime size and IWRAM use per module in CI.

## 19. Milestones

| # | Content | Done when |
|---|---|---|
| M0 | clean up: findings from section 2, HAL interface, host and GBA builds | test runs on the host; a ROM shows a backdrop colour in mGBA; no engine code contains a hardware address |
| M1 | graphics core: BG layers, sprites, palettes, line table; software PPU; PPU snapshots | test scene looks the same on GBA and host (golden image) |
| M2 | asset pipeline: source asset model, cooker with change report, pack format, Tiled import, tile dedup, palette packer, colour correction | a room made in Tiled is shown on GBA and host from a Blutmond pack; the same room cooks for a second profile with a report |
| M3 | editor shell: `ose-ui` split out, GLES2 canvas, SDL3, classic theme; tile/palette windows, hardware window | editor runs on the desktop and in the browser (WebGL 1) |
| M4 | room editor, collision grid, player physics, objects and engine classes, camera, room changes, VRAM tile streaming | the test level is playable on the GBA: both pits can be jumped, the cliff can be climbed, the kill zone respawns the player, the door leads to a second room |
| M5 | state schema, save states, state map in the editor | save state taken on the host loads in mGBA (and back) |
| M6 | live link: `xboo` library (measure the rate first), packet protocol with CRC/retry/resync, adaptive delay, region table, reload modes | the runtime stays on the GBA; a changed room and a changed state are pushed without a reset over a four-wire Xboo cable; the bit-error test passes |
| M7 | scripting: parser, types, bytecode, verifier, interpreter; text view; then node view | enemy and door scripted; text/node switch is lossless; script swap over the live link |
| M8 | AOT: C back end, relocation packer, module loader/overlays; GDB stub over the transport | AOT module in IWRAM faster than bytecode with the same state hashes; breakpoint on a script line on hardware |
| M9 | sound engine and sound editor | song with synth, samples and reverb under budget; preview bit exact |
| M10 | GBA to GBA: bootstrap, 2 MHz protocol; optional Raspberry Pi/RP2040 transport | runtime + content + state transferred, measured times documented |
| M11 | shader PPU (HDMA effects), browser game, world map, polish | game runs in the browser with raster effects matching the software PPU |

## 20. Risks, decisions, open questions

**Risks**

- **Scope**: editor, language, sound engine and tracker are each projects of their own. The
  order above gives something playable early; node view, shader PPU and the faster transports
  can come later without changing the design.
- **IWRAM** (32 KB) is shared by mixer, script overlays, interpreter, stack, live-link handler
  and GDB stub. Measure budgets from the start.
- **Live reload without reset** only works if no content code writes outside its regions. With
  verified bytecode that holds; with AOT code it is a matter of discipline and tests.
- **Parallel port rate**: estimated 10–30 KB/s; the live workflow is designed around sending
  little.
- **Link at 2 MHz** is unreliable with some cables; plan a fallback to 256 kHz.
- **GDB stub on hardware**: how breakpoints are triggered can only be settled on real hardware.

**Decided**

| Question | Decision |
|---|---|
| Window/input | SDL3, downloaded by CMake when needed, also for WASM |
| Toolkit | split out as its own repository [Fighter19/ose-ui](https://github.com/Fighter19/ose-ui) |
| Portability | runtime portable later; all assets portable to other retro platforms from the start (section 14) |
| Cable features | only the four basic Xboo wires are assumed; SD burst is used only if detected; the reset wire is a setting per device; the protocol tolerates bit errors (section 9) |
| Second target | SNES is the next platform after the GBA, and the second cooker profile that proves the asset model |
| Test data | third-party test data is used locally only and never committed (`testdata/` is ignored; tests read it from `BM_TESTDATA_DIR` and are skipped without it) |
| SceneCompiler/SceneLoaderGBA | ideas are taken over, code is not |
| Syntax | modelled on UnrealScript (classes, fixed fields, states, few strings), avoiding UnrealScript's mistakes (section 10) |
| PC ↔ GBA cable | Xboo cable on the parallel port with the own `ppdev` driver (xcomms, branch `parport`); Raspberry Pi/RP2040 as later options |
| Development workflow | runtime loaded once, content pushed live, no reset (section 7) |
| Parallel port code | rewritten for Blutmond with `parport.cxx` as the template |
| Repository language | English |

**Still open**

1. **Test level tileset**: the test map references `Global_Tileset.png`, which is not in the
   test data. Its collision was derived from the tile structure alone (section 14) and should
   be checked against the real graphics.

## 21. Sources

- GAX: [Shin'en, GAX](https://www.shinen.com/music/music.php3?gax) (not reachable from this
  session), [loveemu: GAX V3 notes](https://gist.github.com/loveemu/9b3063ffd9a76cb18e379324e43f3251),
  [gaxtapper](https://github.com/loveemu/gaxtapper),
  [Nintendo World Report: GAX all over GBA](https://nintendoworldreport.com/news/6081/gax-all-over-gba)
- Krawall: [sebknzl/krawall](https://github.com/sebknzl/krawall),
  [list of Krawall games](https://gist.github.com/loveemu/8082f19734aee7724ace11b2768b699f),
  [Battle of the Bits: Krawall](https://battleofthebits.com/lyceum/View/Krawall),
  [VGMPF: Ian Stocker](https://www.vgmpf.com/Wiki/index.php?title=Ian_Stocker),
  [VGMPF: GBA sound drivers](https://vgmpf.com/Wiki/index.php?title=User%3AIgoreshaZhu%2FGBA_sound_drivers)
- MusyX: [Nintendo World Report: MusyX for GBA](https://www.nintendoworldreport.com/news/4785/musyx-for-gba)
- m4a and ipatix: [Summary of GBA Standard Sound Driver (MusicPlayer2000)](https://loveemu.github.io/vgmdocs/Summary_of_GBA_Standard_Sound_Driver_MusicPlayer2000.html),
  [pokeemerald wiki: ipatix's High Quality Audio Mixer](https://github-wiki-see.page/m/pret/pokeemerald/wiki/Implementing-ipatix's-High-Quality-Audio-Mixer),
  [FE Universe: Improved Sound Mixer](https://feuniverse.us/t/improved-sound-mixer-by-ipatix/1107/6)
- Maxmod: [Battle of the Bits: Maxmod](https://battleofthebits.com/lyceum/View/Maxmod)
- Hardware facts (memory, PPU, OAM cycles, SIO modes, DMA): GBATEK (Martin Korth)
- Xboo cable and parallel port driver: [Fighter19/xcomms, branch `parport`](https://github.com/Fighter19/xcomms/tree/parport)
  (`source/parport.cxx`, `source/xboo.cxx`, `include/xcomms_cmd.h`, `docs/xcomms.html`)
- Raspberry Pi / RP2040 link projects: [afska/gba-remote-play](https://github.com/afska/gba-remote-play),
  [loopj/gba-multiboot-pico](https://github.com/loopj/gba-multiboot-pico),
  [gba-link-connection](https://github.com/rodri042/gba-link-connection/),
  [pinout notes by AlexApps99](https://gist.github.com/AlexApps99/9e7707b87a2f81c64421b76cfd5d0733)
- SceneCompiler and SceneLoaderGBA: the author's earlier projects (`Fighter19/SceneCompiler`,
  `Fighter19/SceneLoaderGBA`, private), used for ideas only
