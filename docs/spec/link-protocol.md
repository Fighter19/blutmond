# Link protocol

Version 0.5, draft. Implemented in `src/link/` (both sides) and `tools/bmlink/`.

How the editor (or another GBA) talks to a running Blutmond runtime: booting it, sending
content, reading state and logs, debugging. Requirement: **wherever xcomms can boot a GBA over
an Xboo cable, this protocol MUST work**, including with occasional bit errors.

## 1. Transports

A transport moves 32-bit words in both directions at the same time (the GBA serial port in
normal mode, 32-bit). The PC side implements:

```c
struct BmTransport {
    int  (*open)(void *ctx, const char *device);
    u32  (*exchange)(void *ctx, u32 out);   // send one word, return the word received
    int  (*reset)(void *ctx);               // optional, see section 6
    void (*set_delay)(void *ctx, u32 delay);
    u32  caps;                              // bit 0: reset wired, bit 1: SD burst
};
```

| Transport | Clock | Notes |
|---|---|---|
| `xboo-ppdev` | PC, software | Xboo cable on the parallel port through Linux `ppdev` (based on xcomms, branch `parport`); 4 wires required: SC, SI, SO, GND |
| `rpi-spi` | Raspberry Pi SPI | optional |
| `rp2040-usb` | RP2040 over USB | optional |
| `gba` | the other GBA | GBA to GBA, 2 MHz or 256 kHz |
| `host` | – | in-process, for tests against the PC runtime |
| `tcp` | PC | the PC game (`apps/game/host_link.h`): 127.0.0.1:7414 by default, 4 bytes little-endian per word each way; a thread in the game runs the link handler as the serial interrupt does on the GBA |

The device that makes the clock is the **master**; the runtime is always the **slave** when
talking to a PC.

## 2. Boot

1. The master boots the GBA with the BIOS multiboot protocol exactly as xcomms `NormalBoot`
   does: attention handshake (`0x6202` until the reply has `0x72` in the top byte), header
   (`0x6102`, 0x60 halfwords), key exchange (`0x63xx`, `0x64xx`), length, encrypted data,
   CRC check. The per-word delay is the same setting as xcomms' "Transfer Delay".
2. The image is either the runtime (multiboot) or a small bootstrap that starts the runtime
   from the cartridge. Multiboot is only used together with the live link; content is then
   streamed ([streaming.md](streaming.md)).
3. The runtime then waits for packets (section 3) and answers `HELLO`.

The runtime is only booted once per session; afterwards only packets are exchanged.

## 3. Words and packets

Because the master makes the clock, the slave cannot start a transfer. Every exchange moves one
word each way; the slave's word was prepared in its serial interrupt before the master clocks
it. The slave therefore always answers "one word late".

Special single words:

| Word | From | Meaning |
|---|---|---|
| `0x424D504C` (`BMPL`) | master | poll: "give me your next word" |
| `0x424D4944` (`BMID`) | slave | idle: nothing to send |
| `0x4241ss00` (`BA`, seq) | both | ACK for packet `ss` |
| `0x424Ess00` (`BN`, seq) | both | NAK, `ss` is the sequence number expected next |
| `0x424Ess01` (`BN`, seq, busy) | both | the receiver could not take packet `ss` yet (no buffer); not an error: the sender waits a little and sends from `ss` again, without counting a failure |
| `0xFFFFFFFF` ×4, then `0x53594E43` (`SYNC`) | master | resync, section 4 |
| `0xFFFFFFFF` ×4, then `0x53455353` (`SESS`) | master | a new master (another program, another GBA): a resync that also starts both sides' sequence numbers at 0 and drops what the slave still had queued. Every master sends it before its first packet; the slave answers with four `0xFFFFFFFF` and `SESS` |

Packets (both directions):

```
word 0      0x42 | type << 16 | seq << 8 | flags     (top byte 0x42 = 'B')
word 1      payload length in bytes (max 1024)
words ...   payload, padded with zeros to a multiple of 4
last word   CRC-32 (IEEE) over word 0 .. last payload word
```

Type values `0x41`, `0x4D` and `0x4E` are reserved, because the single words above use them in
the same position.

Packets are acknowledged per **window** to keep the number of turnarounds low: the sender
sends up to 16 packets back to back, then polls once. The receiver answers with a cumulative
ACK (the last sequence number it received in order) or a NAK with the sequence number it expects
next; the sender then continues from there (go-back-N). The slave sends its own packets when
polled; the master acknowledges them the same way. Sequence numbers count per direction and
wrap at 256.

The slave answers a burst of polls once (with the state at that moment) and then ignores the
next 8 polls unless a packet arrives in between, so its own packets are not starved by
answers; if an answer is lost the master's further polls get a new one. When the master waits
for an answer, answers that report no progress may be left over from the previous window's
polls; three of them in a row count as "nothing arrived".

A slave may answer more than one word late. The GBA runtime arms the serial port again with a
word prepared in the previous interrupt before it handles the word just received, so the
master can clock the next word at once; its answers are therefore two words late. Nothing in
the protocol depends on the exact delay.

The master drives the transfer: it pushes content according to [streaming.md](streaming.md)
and the slave does not request chunks one by one. The slave's `STATUS` is collected while
polling for window acknowledgements, so it needs no round trip of its own.

### Packet types

| Type | Name | Direction | Payload |
|---|---|---|---|
| 0x01 | `HELLO` | both | protocol version, runtime version, ABI version, profile, capabilities, free content memory, hashes of the chunks currently loaded |
| 0x02 | `PING` | both | – |
| 0x10 | `CHUNK_BEGIN` | M → S | transfer ID, chunk ID, size, unpacked size, compression, hash |
| 0x11 | `CHUNK_DATA` | M → S | transfer ID, offset, data |
| 0x12 | `CHUNK_END` | M → S | transfer ID; the slave checks the hash and NAKs on mismatch |
| 0x13 | `TOC` | M → S | new pack header and table of contents (see [pack-format.md](pack-format.md) §5) |
| 0x14 | `DROP` | M → S | chunk indices the runtime may free (eviction decided by the master, streaming.md §4) |
| 0x15 | `SESSION` | M → S | load profile and categories (streaming.md §3) |
| 0x16 | `STATUS` | S → M | current room, coarse player position, free memory per region, events (door entered, out of memory) |
| 0x17 | `TEST` | M → S | open the test scene for a mini pack (streaming.md §5) |
| 0x18 | `APPLY` | M → S | reload mode, transfer IDs, start room/door (see [runtime-abi.md](runtime-abi.md) §5) |
| 0x20 | `STATE_GET` | M → S | flag mask (`save`, `link`, `snapshot`), optional range |
| 0x21 | `STATE_DATA` | S → M | schema hash, data |
| 0x22 | `STATE_PUT` | M → S | schema hash, data (applied with the `state` reload mode) |
| 0x30 | `LOG` | S → M | text |
| 0x31 | `PROFILE` | S → M | profiler zones and cycle counts for the last frame(s) |
| 0x32 | `PPU_SNAPSHOT` | both | request (M → S) and data (S → M): registers, VRAM, palettes, OAM, line table |
| 0x38 | `MEM_READ`, 0x39 `MEM_WRITE` | M → S | debug builds only |
| 0x40 | `GDB` | both | debugger commands and answers (section 8) |
| 0x50 | `WATCHDOG` | S → M | frame counter and program counter of a detected hang |
| 0x7F | `ERROR` | both | error code and text |

### Payloads (version 0.2)

All values little endian.

| Packet | Payload |
|---|---|
| `HELLO` M → S | empty |
| `HELLO` S → M | u16 protocol, u16 runtime version, u16 ABI major, u16 ABI minor, u32 profile, u32 content buffer size, u32 size and u32 CRC of the running pack, u32 chunk count, u32 n, then n × (u32 hash, u32 size) of the running pack's chunks |
| `PING` | any bytes; the slave sends them back |
| `TOC` | u32 transfer ID, u32 pack size, u16 part, u16 parts, then bytes of the stream "pack header (36 bytes) + chunk table" split over the parts. Part 0 starts a transfer into the content buffer that the running pack does not use; after the last part the runtime copies the chunks it already has (same hash and size) |
| `CHUNK_BEGIN` | u32 transfer ID, u16 chunk index, u16 0, u32 size, u32 hash |
| `CHUNK_DATA` | u32 transfer ID, u16 chunk index, u16 0, u32 offset in the chunk, data (at most 1012 bytes) |
| `CHUNK_END` | u32 transfer ID, u16 chunk index |
| `APPLY` | u32 transfer ID, u8 mode ([runtime-abi.md](runtime-abi.md) §5), 3 × u8 0, optional room ID (for `restart`). The runtime checks that every chunk is there and the pack's CRC, then applies at a frame boundary and answers with `STATUS` |
| `STATUS` | u8 kind (0 report, 1 answer to `APPLY`), u8 result (0 ok, 1 transfer incomplete, 2 CRC mismatch, 3 too large, 4 pack refused, 5 state of another layout, 6 mode not supported), u8 mode, u8 0, u32 transfer ID, u32 game frame, u32 room index, s16 x, s16 y, u32 reloads applied |
| `STATE_GET` | u8 flag mask ([state.md](state.md)) |
| `STATE_DATA`, `STATE_PUT` | the state as `bmStateWrite` writes it (header with schema hash and CRC) |
| `LOG` | text |
| `PPU_SNAPSHOT` M → S | u32 offset, u32 length (0: to the end) of the runtime's video state as a PPU snapshot (`BMPS`, `src/ppu/ppu.h`: registers, VRAM, palettes, OAM, line tables; about 100 KB) |
| `PPU_SNAPSHOT` S → M | u32 offset, u32 total size, then data (at most 1016 bytes). The runtime sends the parts at frame boundaries, as many per frame as its send buffers take; the registers are the values last written (the GBA HAL keeps copies, most video registers are write-only) |

The master does not wait for chunk-level answers: a pack transfer is `TOC`, then for each
missing chunk `CHUNK_BEGIN`, `CHUNK_DATA`…, `CHUNK_END`, in windows; the only round trip is
`APPLY` → `STATUS`.

## 4. Error handling

- **Retry**: on NAK, CRC mismatch or a timeout (no ACK after a set number of polls), the packet
  is sent again with the same sequence number.
- **Resync**: after 4 failed attempts the master sends four `0xFFFFFFFF` and `SYNC`. The slave
  drops any partial packet and answers the next poll with `SYNC`. Both sides then continue with
  the sequence numbers from the last ACK.
- **Word boundary**: the wire has no frame for words. If the slave's port is armed while a
  word is already under way (the runtime starting while the master polls, an interrupt that
  comes late, a glitch on the clock), every later word is cut at the wrong place, on both
  sides; seen on hardware as the idle word arriving rotated by two bits. So the slave arms its
  port anew whenever no word arrived during a frame, and the master is quiet for 100 ms before
  each resync and session start. The emulated port moves whole words and cannot show this.
- **Resume**: chunks are confirmed window by window, so a transfer continues where it stopped.
- **Atomic apply**: the slave applies nothing until `APPLY` arrives and every chunk it refers to
  has passed its hash check.
- **Adaptive delay**: the master starts with a safe per-bit delay, lowers it step by step while
  transfers are clean and raises it after errors. The working value is remembered per device
  and transport.
- **Counters**: the master keeps CRC errors, retries, resyncs and the effective rate and shows
  them to the user.

## 5. SD burst

If the master's transport supports it, it tests at connect time whether the SD wire (Xboo:
`/SELECT`) is connected. Only then MAY both sides switch to a burst mode with two data bits per
clock, negotiated through `HELLO` capabilities. The burst mode itself will be specified after
measurements; without it everything works on the four basic wires.

## 6. Reset setting

Reset is a per-device setting: `none` or `xboo` (the reset wire of the original Xboo cable, as
used by xcomms `ResetGBA`). The protocol never needs it. With `xboo` the editor uses it to boot
a new runtime and to recover after a `WATCHDOG` report or when the runtime stops answering.
With `none` the editor asks the user to switch the GBA off and on.

## 7. Measurements

In mGBA, with `bmlink` clocking the serial port word by word (the GBA side unchanged):

- the runtime keeps up with one word every 600 CPU cycles (about 28,000 words/s, 110 KB/s
  raw); its word handler runs from IWRAM as ARM code; at 400 cycles words are lost;
- at 2048 cycles per word (about 8000 words/s, the expected range of the Xboo cable) a hot
  reload of the test level's changed room (one 6 KB chunk, applied and answered) takes 0.43 s
  with no lost words;
- with 2000 flipped bits and 2000 lost words per million words in each direction, packs, state
  and restarts still go through (one resync on average); at 5000 per million 1 KB packets
  rarely arrive whole: smaller packets on bad cables are future work.

On a real GBA over a four-wire Xboo cable (parallel port through `ppdev`, about 2.5 µs per
port access):

- multiboot of the game image (81 KB), then the runtime answers: works;
- 5 KB there and back: 2270 words/s (9 KB/s) at the default word delay of 8; 2950 words/s at
  delay 1, but that leaves the GBA only about 15 µs to arm its port again, and words were then
  cut at the wrong place (section 4, "Word boundary");
- a restart with a changed level (13 of 21 chunks, 26 KB): 3.4 s; a level it already has: 0.4 s;
- the video state for the editor's graphics view (101 KB): 11 s;
- no bad packets and no resyncs in these runs.

## 8. Debugging

`bmlink ... gdb <port>` is a server for GDB's remote protocol on `127.0.0.1:<port>`; it turns
what GDB asks into `GDB` packets. The runtime's part is small (about 1.7 KB): stop, show and
change registers and memory, keep breakpoints. Everything that needs knowledge of GDB or of
instructions is done on the PC.

```
arm-none-eabi-gdb ──TCP──► bmlink gdb ──link packets──► runtime (serial interrupt, stopped loop)
```

**Packets.** Byte 0 is the command, byte 2 a breakpoint slot, arguments from byte 4. An answer
repeats the command in byte 0, has its result in byte 1 (0 done, 1 only while stopped, 2 not
memory that may be used, 3 not supported) and data from byte 4.

| Command | Arguments | Answer |
|---|---|---|
| `i` info | | u8 stopped, u8 breakpoint slots (8), u8 the device can be stopped |
| `h` halt | | none; a `S` packet follows when the program has stopped at the next frame boundary |
| `c` continue | | result |
| `g` / `G` registers | `G`: 17 words | `g`: r0–r15 and cpsr (17 words) |
| `m` read memory | u32 address, u16 length (≤ 512) | the bytes |
| `M` write memory | u32 address, bytes | result |
| `Z` / `z` breakpoint | slot, u32 address (`Z`) | result |
| `S` stopped (from the runtime) | byte 1: 0 asked to, 1 breakpoint; byte 2: slot | |

Commands are safe to repeat (a write or a breakpoint done twice is the same as once), so a
command whose answer had to wait for room in the send ring is simply handled again.

**Breakpoints.** The ARM7TDMI has no debug hardware a program can use and the BIOS does not
pass undefined instructions on, so a breakpoint is a branch written over an ARM instruction in
RAM, to one of eight landing places in IWRAM (`src/hal/gba/debug_trap.S`), one per slot. The
landing place saves every register, and `bmHalDebug*` (`hal.h`) hands them to the live
module, which reports `S` and then handles `GDB` packets in a loop until `c`. The serial
interrupt keeps running meanwhile; other packets wait. Sound is switched off while stopped.
On `c` every register is loaded again from the saved (possibly changed) values.

Limits that follow from this:

- breakpoints only in **ARM code in RAM**: native script modules (IWRAM) and the runtime's
  IWRAM functions; not in Thumb code, not in ROM. `Z0` with another kind or address is refused;
- never in the link's own code or the serial interrupt, which must keep running;
- `h` (GDB's Ctrl-C) takes effect at the next frame boundary, so it cannot interrupt a game
  that hangs inside a frame;
- at a `h` stop the registers shown are those of the runtime at that point (Thumb code);
  changes to them are not taken over.

**Single steps** are made by the bridge: it reads the registers and the instruction, works
out where it goes next (conditions, `B`/`BL`, `BX`, data processing and loads into pc, `LDM`
with pc; `tools/bmlink/gdb_bridge.c`), puts a breakpoint there and continues. A call into
Thumb code is stepped over as a whole; a return into Thumb code runs on. Thumb code itself
cannot be stepped.

**GDB packets** served: `? g G p P m M c s Z0 z0 D k H T`, `qSupported`, `qAttached`, `qC`,
`qfThreadInfo`; the rest is answered as not supported. The register layout is GDB's default
for ARM without a target description (r0–r15, eight unused floating point registers, cpsr).

**Script lines.** A native module built with `bmcook --native-elf scripts.elf` has debug
information whose lines are the script's (`#line`). It is linked at address 0 and loaded at
`bm_native_slot`:

```
bmlink --xboo /dev/parport0 gdb 2345          (or --mgba game.gba run 60 gdb 2345)
arm-none-eabi-gdb game.elf
(gdb) target remote :2345
(gdb) add-symbol-file scripts.elf &bm_native_slot
(gdb) break bat.ms:14
(gdb) continue
```

Tested in mGBA with a client that speaks the remote protocol (`tests/gdb_session.py`):
breakpoint on a script line, registers, memory, steps over a call into the runtime, Ctrl-C,
detach, and the game plays on to its end. Not yet tried with a real `arm-none-eabi-gdb` or on
hardware.

## 9. GBA to GBA

A game on a cartridge brings the GBA on the other end of a link cable to where it is
(`src/link/transfer.h`; in the test game: Select + Start). The sending GBA makes the clock
(normal mode, 32 bits, 2 MHz); the other side needs nothing special:

| The other GBA | What is sent |
|---|---|
| runs the game (cartridge) | the `link` part of the game state (`STATE_PUT`, `APPLY` state) |
| runs the runtime with other content | the pack first, only the chunks it lacks (sections 3, [streaming.md](streaming.md)), applied as a restart; then the state |
| runs the bootstrap | the runtime as a multiboot image through the bootstrap, then as above |
| waits in its BIOS (no cartridge) | the bootstrap through the BIOS's multiboot protocol (256 kHz), then as above |

The sender finds out which by asking: `HELLO` (a runtime answers), then the bootstrap's start
word, and if neither answers it tries the BIOS. Everything after the bootstrap is the packet
protocol of section 3 with the GBA as master (`link_master.c` with fixed buffers); between
words the master waits 800 cycles, since the receiving runtime handles each word in its serial
interrupt (section 7).

**The bootstrap** (`src/hal/gba/bootstrap`, 868 bytes) is what the BIOS's slow protocol
carries. It runs from IWRAM, polls the port with interrupts off, and takes one image:

```
sender   START   size    word 0  word 1 ... word n-1   crc     any
answer   (idle)  READY   size    1      ... n-1        n       OK or BAD
```

Each answer comes one word late and is the number of words taken so far, so the sender
notices a lost word at once and starts again from `START` (a bootstrap in the middle of an
image takes words until it has its count, fails the checksum and waits for `START` again).
After `OK` the bootstrap jumps to the image at 0x020000C0. The checksum is the CRC-32 of the
words. Size in bytes, a multiple of 4, at most 256 KB.

**Measured** in mGBA with two emulated GBAs joined by a cable (`tools/bm_mgba_pair`, tests
`gba_to_gba_*`), the sender in the second room of the test level:

| Case | Words | Time |
|---|---|---|
| the other GBA runs the game: state only | 860 | 0.09 s |
| the other GBA runs the bootstrap: runtime with its content (81 KB), start-up, state | 24,100 | 1.17 s, of which the image takes 0.75 s (108 KB/s) |

The image goes at about 620 cycles per word: 256 for the 32 bits at 2 MHz, the rest for the
bootstrap to store the word and add it to the checksum. In both cases the receiver then
reports the sender's room and position, and the sender plays on.

Not tested: the BIOS stage. mGBA's built-in BIOS replacement neither sends nor receives
multiboot, so the sender's multiboot code is only checked against a model of the receiving
BIOS (`tests/test_multiboot.c`); it is the same code `bmlink boot` uses over the Xboo cable.
And nothing here has run on hardware: cable quality at 2 MHz is the open question (the plan's
fallback is 256 kHz, one bit in `bmHalLinkMasterBegin`).

## 10. Input mirror: the PC plays along

`bmlink --xboo <port> mirror <[host:]port of the PC game>` makes the PC game show what happens
on the GBA. Nothing but the keys is sent: the game is deterministic (same state, same keys,
same result on both machines, bit for bit), so the PC game that starts from the GBA's state
and gets the GBA's keys for every frame plays the same game.

- The runtime, asked with `INPUT` (payload 1), notes before each game frame which keys that
  frame uses. It sends a packet at once when the keys changed and otherwise every second
  frame: the last frame whose keys are known, and the changes (frame, keys) since the last
  packet.
- `bmlink` first starts the reporting, then reads the GBA's state (`STATE_GET`) and loads it
  into the PC game (`STATE_PUT`, `APPLY` state), then passes every `INPUT` packet on.
- The PC game (`apps/game`, started normally or with `--follow` to wait) uses for frame *n*
  the keys the GBA used for frame *n*, never its own, and holds still while the next frame's
  keys are not there yet.

**Lag.** The cable is slow and the PC only gets a word when it polls, so keys arrive tens of
milliseconds late and not evenly. That delay never changes *what* happens, because keys are
applied by frame number and not by arrival time; it only makes the PC run a little behind the
GBA (the two frames between packets plus the transfer). When packets arrive in a bunch the PC
game runs up to four frames per screen refresh to catch up, so the delay does not add up.

**Checks.** Every 64 frames the runtime adds a CRC-32 of its game state to the packet. The PC
game compares it with its own state at that frame and reports a difference; `bmlink` then
loads the GBA's state again. The same happens if the runtime had more key changes than fit
(16) while nobody polled.

Tested in mGBA (a GBA image played by its autopilot, 900 frames through a room change, every
check equal) and on a real GBA over the Xboo cable (1500 frames in 25.5 s, no reload of the
state needed). Both machines must run the same pack. Not there yet: a button in the editor,
and the other direction (the PC's keys into the GBA).

## History

- 0.1: first draft.
- 0.1.1: window acknowledgements; master-driven streaming with `DROP`, `SESSION`, `STATUS`,
  `TEST`.
- 0.2: busy NAK, one answer per burst of polls, answers may be more than one word late,
  payloads of `HELLO`, `TOC`, `CHUNK_*`, `APPLY`, `STATUS`, `STATE_*`; measurements.
- 0.2.1: `PPU_SNAPSHOT` payloads. An acknowledgement that arrives while the slave is in the
  middle of its next packet must not restart that packet (fixed in the slave: before, every
  packet that followed another one directly was sent twice).
- 0.3: debugging (section 8).
- 0.4: GBA to GBA and the bootstrap (section 9).
- 0.5: input mirror (section 10).
- 0.4.2: the word boundary (section 4), the `RESET` packet (the runtime starts the device over through the BIOS, for cables without a reset wire), measurements on a GBA.
- 0.4.1: `SESS`. Found on hardware: a second run of `bmlink` counted its packets from 0 while the runtime went on from the first run's numbers, and nothing got through.
