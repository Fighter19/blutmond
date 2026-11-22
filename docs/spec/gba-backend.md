# The hardware layer and the GBA back end

Version 0.1. Describes `src/hal/public/hal.h` (the only interface between the engine and a
machine) and how `src/hal/gba/` fills it in. Enough to write the GBA back end again, or
another one. What was learnt about the serial port on a real GBA is in
[../HARDWARE.md](../HARDWARE.md).

## 1. The model

The engine does not know a GBA. It knows the GBA's *video model*, as data:

- **Registers**: 16-bit values at the byte offsets of the GBA's I/O area, 0x00 to 0x5F
  (`DISPCNT` at 0, `BGnCNT` at 8 + 2n, scroll at 0x10 + 4n and 0x12 + 4n, windows, blending,
  as the hardware has them). The engine uses mode 0.
- **Video memory**: 96 KB laid out as on the GBA (character blocks of 16 KB from 0, screen
  blocks of 2 KB, sprite tiles from 0x10000).
- **Palette**: 512 entries of 15-bit colour (blue in the top five bits, green, red in the
  low five), 256 for backgrounds and then 256 for sprites.
- **Sprites**: 128 entries of 4 halfwords in the GBA's OAM format.

On the PC these are arrays that a software PPU draws from (`src/ppu/`). On the GBA they are
the hardware. A back end for another machine translates.

## 2. The interface

| Function | Contract |
|---|---|
| `bmHalPlatformName` | "gba", "host" |
| `bmHalGetMemoryRanges` | three ranges the engine may allocate from: slow RAM, fast RAM, video memory |
| `bmHalVideoInit` | display on, mode 0, no layers: only palette entry 0 shows. Also sets up interrupts |
| `bmHalPaletteWrite/Read(index, colour)` | one palette entry; the top bit is dropped |
| `bmHalVideoWriteReg(offset, value)` | writes a video register and keeps a copy |
| `bmHalVideoReadReg(offset)` | the copy (most GBA video registers cannot be read back) |
| `bmHalVramWrite(offset, data, size)` | copies into video memory; offset and size even |
| `bmHalOamWrite(first, data, count)` | copies halfwords into the sprite table; call in the vertical blank |
| `bmHalVideoWaitVBlank()` | hands the sound buffers just filled over, waits for the next vertical blank, restarts the line tables; returns the count of vertical blanks so far, so a missed frame shows as a gap |
| `bmHalVideoLine()` | the line being drawn, 0-227; 160 and up is the vertical blank |
| `bmHalLineTableSet(slot, reg, values)` | from the next frame on, register `reg` gets `values[y]` on line y (160 values, copied); `NULL` switches it off. Three slots (0 to 2) |
| `bmHalVideoSnapshotPart(offset, out, size)` | the video state as a snapshot (section 6), piecewise; returns the total size |
| `bmHalInputKeys()` | the buttons held, as bits: A 1, B 2, Select 4, Start 8, Right 16, Left 32, Up 64, Down 128, R 256, L 512 |
| `bmHalSaveRead/Write(offset, data, size)` | the save memory, 32 KB |
| `bmHalCycles()` | a running 32-bit count of processor cycles; 0 where time is not measured |
| `bmHalAudioStart()`, `bmHalAudioBuffers(&left, &right)` | section 4 |
| `bmHalLinkStart(handler, context, firstWord)`, `bmHalLinkStop()` | section 5 |
| `bmHalLinkLock()`, `bmHalLinkUnlock(state)` | keeps the handler from running while the main program changes what they share |
| `bmHalLinkRealign()` | section 5 |
| `bmHalLinkMasterBegin(fast)`, `...Exchange(word)`, `...Wait(cycles)`, `...End()` | the serial port as the side that makes the clock |
| `bmHalReset()` | starts the device over as if switched on |
| `bmHalDebug...` | breakpoints, registers and memory for the debugger ([link-protocol.md](link-protocol.md) §8) |

The frame rate is the machine's: 59.7275 per second on a GBA (16,777,216 cycles per second,
280,896 per frame).

## 3. Memory and start on the GBA

| Region | Address | Size | Holds |
|---|---|---|---|
| ROM (cartridge) | 0x08000000 | up to 32 MB | code and constants of a cartridge image |
| EWRAM | 0x02000000 | 256 KB | large, rarely used data (`.sbss`); for a multiboot image also all code and constants |
| IWRAM | 0x03000000 | 32 KB | code that must be fast (`.iwram`), `.data`, `.bss`, the stacks |

- User stack from 0x03007F00 downward (4 KB reserved), interrupt stack from 0x03007FA0. The
  BIOS uses 0x03007FE0 and up. The link's handler runs on the user stack.
- Linker scripts: `gba_cart.ld` and `gba_mb.ld`. They differ only in where code, constants
  and the embedded pack go.
- EWRAM from the bottom: what is loaded (a multiboot image: all code and constants, then the
  embedded pack, section `.bm_pack`, last); the content pool for pushed packs
  (`__bm_pool_start` to `__bm_pool_end`, [runtime-abi.md](runtime-abi.md) §0); the engine's
  EWRAM heap (4 KB, `__bm_ewram_heap_size`); the zero-initialised data (`.sbss`) at the top.
  So a multiboot image does not carry its zeros, its embedded pack is the start of the pool,
  and whatever the program does not take belongs to the pool. A program too large for any
  pool is a link error.

**Start code** (`crt0.S`): the header (the logo is left empty and put in by the sending tool,
[../HARDWARE.md](../HARDWARE.md) §3; game code `BMND`, maker `00`, the fixed value 0x96). A
multiboot image has its second entry at 0xC0. Then: `WAITCNT = 0x4317` (faster cartridge
access); the interrupt stack and the user stack; copy `.iwram` and `.ewram` from where they
were loaded; zero `.bss` and `.sbss`; call `main`.

**Functions in IWRAM** are compiled as ARM code and optimised for speed whatever the rest of
the build does (`BM_IWRAM_CODE`): the serial handler and what it calls, the sound mixer (with
the room acoustics' loop), the script interpreter's loop, the wait loops of the serial master.
The script verifier (`bmVmModuleVerifySome`, about 1.2 KB) is in IWRAM as Thumb code built
for size (`BM_IWRAM_THUMB`): half the size of ARM code. From ROM it made the checks at a
room's entry miss frames (multiboot autoplay 11 missed frames in 1000 instead of 8).

IWRAM is nearly full: the cartridge game leaves about 380 bytes between `.bss` and the
stacks (the image with native scripts about 560). When room is needed: move what runs rarely
from `BM_IWRAM_CODE` to `BM_IWRAM_THUMB`, or out of IWRAM, and check that the autoplay runs
miss no more frames than before (`bm_game_stats` lag, the `game_render_*` tests); the
verifier's spare-time budget is `BM_SPARE_WORDS_PER_LINE` in `apps/game/main.c`.

## 4. Interrupts, video, sound, input, save

**Interrupts.** The handler's address goes to 0x03007FFC. Enabled: vertical blank (`DISPSTAT`
bit 3, `IE` bit 0), while line-table slot 2 is in use horizontal blank (`DISPSTAT` bit 4, `IE`
bit 1), and while the link listens serial (`IE` bit 7). The handler (`irq.S`, ARM, in IWRAM)
acknowledges what is pending and enabled in `IF` and in the BIOS's copy at 0x03007FF8, counts
vertical blanks, starts the frame's sound at a vertical blank (below), writes slot 2's next
value at a horizontal blank, and for a serial interrupt
switches to system mode with interrupts off and calls the C handler on the user stack.

**Waiting for a frame** is the BIOS call 0x05 (VBlankIntrWait). Before it the sound buffers
filled this frame are handed over; after it the line tables are restarted.

**Video.** `DISPCNT = 0` at start. Register writes go to the hardware and to the copy. Video
memory is written 16 bits at a time. Palette values are masked with 0x7FFF.

**Line tables.** Three slots. In each, line 0's value is written directly at the vertical
blank; then the value of the next line is written at every horizontal blank. Each table has
161 entries (the last one doubled), because the last horizontal blank reads one more.

- Slots 0 and 1: DMA 0 and DMA 3 (DMA 1 and 2 are the sound's): source the table from entry
  1, destination fixed, count 1, control = enable + start at horizontal blank + repeat. They
  cost no processor time.
- Slot 2: the horizontal-blank interrupt. The handler reads `VCOUNT` (the line just drawn);
  below 160 it writes entry `VCOUNT + 1` of the table `bm_hal_line2` (a pointer to the
  register, then the 161 values, in IWRAM) to the register. About 160 short interrupts a
  frame; while the serial handler runs (interrupts off) a line can keep the previous value.

**Sound.** Two channels of signed 8-bit samples, 18,157 per second, exactly 304 per frame.

- `SOUNDCNT_X = 0x80`. `SOUNDCNT_H = 0x9A0C`: both direct sound channels at full volume,
  channel A on the left and B on the right, both on timer 0, both FIFOs reset.
- Timer 0: reload `65536 - 924`, running without prescaler. 280,896 / 924 = 304.
- Two pairs of buffers of 304 + 16 bytes; the 16 stay zero, so a request that falls just
  before the swap plays silence.
- `bmHalAudioBuffers` gives the pair to fill. Waiting for the frame hands it over
  (`bm_hal_audio_next`: left, right, sound on) and the other pair becomes the one to fill. At
  each vertical blank the interrupt handler stops DMA 1 (to FIFO A, 0x040000A0) and DMA 2 (to
  FIFO B, 0x040000A4) and starts them (32 bits, repeat, destination fixed, start on FIFO
  request) on the pair handed over, or, if none was (the frame ran late), on a frame of
  silence (`bm_hal_audio_silence`, EWRAM). Before, the buffers were swapped only when the
  game waited: a late frame left the DMA reading on past the buffer, over the other side's
  and the other pair's samples, so the end of the sound was heard again.

So what is mixed in one frame plays during the next.

**Cycle counter.** Timer 2 counts cycles, timer 3 counts timer 2's overflows; read high, low,
high again until the two high readings agree.

**Input.** `~KEYINPUT & 0x3FF`: the bit order is the interface's.

**Save.** SRAM at 0x0E000000, byte by byte. The string `SRAM_V113` in the image tells
emulators and flash cartridges which kind of save memory it is.

**Reset.** Interrupts off, then the BIOS call 0x26.

## 5. The serial port

Normal mode, 32 bits per word ([link-protocol.md](link-protocol.md)).

**Listening** (the other side makes the clock): `RCNT = 0`; `SIOCNT = 0x1000`; the first word
into `SIODATA32`; `SIOCNT = 0x5000`, then `0x5080` (32 bit, interrupt, start); serial
interrupt on.

The interrupt, in this order:

1. read the word received from `SIODATA32`;
2. write the word prepared in the *previous* interrupt and arm the port again
   (`SIOCNT = 0x5080`), so the other side can send the next word at once;
3. call the handler with the word received; what it returns is the word for the interrupt
   after this one.

So an answer is two words late. The order matters: arming the port must not wait for the
handler.

**Re-alignment** (`bmHalLinkRealign`, called every frame): words have no frame on the wire. If
no word arrived during two vertical blanks, the port is armed anew: `SIOCNT = 0x5000`, the
armed word again, `0x5080`. This must be tied to real time, not to how often it is called;
see [../HARDWARE.md](../HARDWARE.md) §2 for what happens otherwise.

**Lock**: the serial interrupt's enable bit off, and back to what it was.

**Stop**: serial interrupt off, `SIOCNT = 0x1000`.

**As the side that makes the clock**: `SIOCNT = 0x1001` (256 kHz) or `0x1003` (2 MHz). An
exchange writes the word, sets bit 7, waits until bit 7 is clear, reads the word. The wait
between words is a loop of four cycles per turn in IWRAM.

## 6. The video snapshot (`BMPS`)

What `bmHalVideoSnapshotPart` gives and the editor's Graphics tab and `bm_ppu_render` read.
Little endian, 101,422 bytes with three line tables (a reader takes any number):

| Offset | Content |
|---|---|
| 0 | `BMPS` |
| 4 | u16 version (1), u16 number of line tables (3) |
| 8 | 0x60 bytes: the video registers 0x00-0x5F as last written |
| 0x68 | 0x18000 bytes of video memory |
| 0x18068 | 512 x u16 palette |
| 0x18468 | 512 x u16 sprite table |
| 0x18868 | per line table: u16 register offset (0xFFFF: unused), 160 x u16 values |

## 7. The PC back end

`src/hal/host/`: the same interface over arrays, drawn by the software PPU (`src/ppu/`): mode 0
backgrounds, regular sprites, windows and colour effects as GBATEK describes them. Windows: the
control of window 0 where it covers, else window 1, else the OBJ window (pixels of sprites in
OBJ-window mode), else the outside; without a window everything shows. Colour effects work on
the front pixel and the one behind it, in 5 bits per channel: alpha `min(31, (a * eva + b *
evb) >> 4)` where the one behind is a second target, brighten `c + ((31 - c) * evy >> 4)`,
darken `c - (c * evy >> 4)`, weights above 16 counting as 16. A semi-transparent sprite is
alpha-blended with a second target behind it even where BLDCNT names another effect or a
window has effects off, and is otherwise a first target. mGBA, which the tests compare with,
computes effects in 8 bits per channel (results one level apart), blends a semi-transparent
sprite with the colour the effect gave the layer behind it, darkens or brightens sprites that
are no target while the OBJ window is on and differs from a window in having effects, and
draws no window one pixel wide on its first line; the test scene keeps out of those cases. Save memory is a
buffer the program reads from and writes to a file. Sound buffers are handed to SDL. The
serial port is a function another thread or a test calls with a word; `bmHalCycles` is 0;
nothing can be debugged or reset through it.
