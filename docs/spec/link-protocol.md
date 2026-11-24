# Link protocol

Version 0.7, draft. Implemented in `src/link/` (both sides) and `tools/bmlink/`.

How the editor (or another GBA) talks to a running Blutmond runtime: booting it, sending
content, reading state and logs, debugging. Requirement: **wherever xcomms can boot a GBA over
an Xboo cable, this protocol MUST work**, including with occasional bit errors.

## 1. Transports

A transport moves 32-bit words in both directions at the same time (the GBA serial port in
normal mode, 32-bit). The PC side implements:

```c
typedef struct BmLinkTransport {
    void *pContext;
    uint32_t (*exchange)(void *pContext, uint32_t out);  // send one word, return the word received
    void (*setDelay)(void *pContext, uint32_t delay);    // optional
    bool (*reset)(void *pContext);                       // optional, see section 6
    uint32_t caps;                                       // bit 0: reset wired, bit 1: SD burst
    void (*pause)(void *pContext, uint32_t milliseconds);// optional: nothing on the wire for that long
} BmLinkTransport;
```

Built: `xboo-ppdev`, `tcp`, `host`, `gba`, and mGBA as a transport for tests. The other two
are plans.

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

1. The master boots the GBA with the BIOS multiboot protocol as xcomms `NormalBoot` does
   (below). The per-word delay is the same setting as xcomms' "Transfer Delay".
2. The image is the runtime as a multiboot image, with its content inside.
3. The runtime then waits for packets (section 3) and answers `HELLO`.

A runtime that is already running is asked to start the device over first (`RESET`).

**The multiboot sequence** (`src/link/multiboot.c`; each step is one 32-bit exchange, "top
byte" is bits 24 to 31 of the reply):

1. The image is padded with zeros to a multiple of 16 bytes, at least 0x1C0; at most 256 KB.
2. Send `0x6202`, pausing between tries, until the top byte of the reply is `0x72` (give up
   after about 1000 tries).
3. Send `0x6102`. Then the first 0xC0 bytes as 0x60 halfwords; the reply's top byte counts
   down from 0x60 and must match. Send `0x6200`, then `0x6202`.
4. Send `0x63D1` until the top byte of the reply is `0x73`; bits 16 to 23 of it are key 1.
   Send `0x6400 | ((key 1 + 0x0F) & 0xFF)` (the confirmation), again until `0x73`.
5. Wait about a sixteenth of a second. Send `length / 4 - 0x64`; bits 16 to 23 of the reply
   are key 2.
6. Seed = `0xFFFF0000 | key 1 << 8 | 0xD1`. For every word from offset 0xC0: add the plain
   word to the checksum (below); seed = `seed * 0x6F646573 + 1`; send
   `word ^ seed ^ (0xFE000000 - offset) ^ 0x43202F2F`. Bits 16 to 31 of each reply must be
   the low 16 bits of the offset.
7. Send `0x65` until bits 16 to 31 of the reply are no longer `0x74`; they must then be
   `0x75`. Send `0x66`.
8. Add `0xFFFF0000 | key 2 << 8 | (confirmation & 0xFF)` to the checksum and send the
   checksum; bits 16 to 31 of the reply must be the same.

The checksum is 16 bits, starts at `0xC387`, and for each word: `c ^= word & 0xFFFF` and
the upper half of a 32-bit register is the word's upper half; then 32 times: shift the
register right by one, and if the bit that fell out was 1, xor its low 16 bits with `0xC37B`.
The low 16 bits are the new checksum.

A real GBA also checks the header: [../HARDWARE.md](../HARDWARE.md) §3.

## 3. Words and packets

Because the master makes the clock, the slave cannot start a transfer. Every exchange moves one
word each way; the slave's word was prepared in its serial interrupt before the master clocks
it. The slave therefore always answers "one word late".

Special single words:

| Word | From | Meaning |
|---|---|---|
| `0x424D504C` (`BMPL`) | master | poll: "give me your next word" |
| `0x424D4944` (`BMID`) | slave | idle: nothing to send; also the first word a runtime arms its port with |
| `0x4241sscc` (`BA`, seq) | both | ACK: `ss` is the last packet received in order (`0xFF` before the first one) |
| `0x424Esscc` (`BN`, seq) | both | NAK, `ss` is the sequence number expected next |
| `0x424Esscc` with bit 0 set (`BN`, seq, busy) | both | the receiver could not take packet `ss` yet (no buffer); not an error: the sender waits a little and sends from `ss` again, without counting a failure |

In these three `cc` bits 1-7 are a check of `ss`: its bits 0-6 inverted, exclusive-or `0x55` if its
bit 7 is set (protocol version 3). An answer whose check does not fit is noise: one flipped bit
in a sequence number would otherwise release packets the other side never had.
| `0xFFFFFFFF` ×4 or more, then `0x53594E43` (`SYNC`) | master | resync, section 4. Recognised anywhere, also in the middle of a packet |
| `0xFFFFFFFF` ×4, then `0x53455353` (`SESS`) | master | a new master (another program, another GBA): a resync that also starts both sides' sequence numbers at 0 and drops what the slave still had queued. Every master sends it before its first packet |

Packets (both directions):

```
word 0      0x42000000 | type << 16 | seq << 8 | flags
word 1      payload length in bytes (max 1024)
words ...   payload, little endian, the last word padded with zeros
last word   CRC
```

`flags` is 0. The CRC is the standard CRC-32 (polynomial 0xEDB88320 reflected, start and
final xor 0xFFFFFFFF, as in zlib) over the four bytes of each word before it, lowest byte
first: header word, length word, payload words with their padding. A length above 1024 ends
the packet at once as a bad one. Outside a packet, a word whose top byte is not `0x42` is
ignored.

**The slave's answer to `SYNC` and `SESS`** comes without being polled: its next five words
are four `0xFFFFFFFF` and then `SYNC` or `SESS`. The master polls until it sees that word
(recognised the same way: after at least four fill words). Both sides drop a partial packet,
pending answers, and start their unacknowledged packets over from their first word. After
`SESS` both also expect and send sequence number 0 next, and the slave drops what it had
queued.

**Sequence numbers** are compared modulo 256. With `d = (seq - expected) & 0xFF` for a packet
received whole: 0 is taken (or refused as busy); 1 to 127 means one before it is missing (a
NAK is due); 128 to 255 is a copy of one already taken (the master acknowledges it again, the
slave ignores it). An ACK for `ss` releases the sender's packets up to and including `ss` if
that is one it still holds; a NAK for `ss` releases those before `ss` and sends again from
`ss`. A packet being sent is not started over because an ACK for earlier ones arrives.

Type values `0x41`, `0x4D` and `0x4E` are reserved, because the single words above use them in
the same position.

Packets are acknowledged per **window** to keep the number of turnarounds low: the sender
sends up to 16 packets back to back, then polls once. The receiver answers with a cumulative
ACK (the last sequence number it received in order) or a NAK with the sequence number it expects
next; the sender then continues from there (go-back-N). The slave sends its own packets when
polled; the master acknowledges them the same way (it sends an ACK or NAK word instead of a
poll). Sequence numbers count per direction and wrap at 256.

The runtime's numbers: 4 send buffers and 6 receive buffers of one packet each. A packet is
kept in its receive buffer until the frame boundary handles it; when fewer than two buffers
are free the packet is refused (busy). If the slave has sent everything it holds and 32 polls
pass without an ACK, it sends its unacknowledged packets again. What the slave sends in an
exchange, first that applies: the resync answer; an answer to a poll (only between its own
packets): NAK if a packet was lost or bad since the last answer, else busy if one was refused,
else ACK; the next word of its own packets; idle.

The master's numbers: it waits 400 polls for an answer; a window that made no progress is
sent again, after 4 such windows in a row it resyncs, after 8 resyncs in a row without a packet getting through (or 4
resync tries without an answer) the link has failed. After a busy answer it polls 16 times
and sends again from the number named, without counting a failure.

The slave answers a burst of polls once (with the state at that moment) and then ignores the
next 8 polls unless a packet arrives in between, so its own packets are not starved by
answers; if an answer is lost the master's further polls get a new one. When the master waits
for an answer, answers that report no progress may be left over from the previous window's
polls; three of them in a row count as "nothing arrived".

A slave may answer more than one word late. The GBA runtime arms the serial port again with a
word prepared in the previous interrupt before it handles the word just received, so the
master can clock the next word at once; its answers are therefore two words late. Nothing in
the protocol depends on the exact delay.

The master drives the transfer: it pushes content and the slave does not request chunks one
by one.

### Packet types

"Built" says whether the runtime does anything with it. A runtime ignores (but acknowledges)
every type it does not know.

| Type | Name | Direction | Built | Content |
|---|---|---|---|---|
| 0x01 | `HELLO` | both | yes | what runs there |
| 0x02 | `PING` | both | yes | echo |
| 0x10 | `CHUNK_BEGIN` | M → S | ignored | announces a chunk; the master sends it, the runtime needs nothing from it |
| 0x11 | `CHUNK_DATA` | M → S | yes | a piece of a chunk |
| 0x12 | `CHUNK_END` | M → S | yes | the chunk is complete |
| 0x13 | `TOC` | M → S | yes | starts a pack transfer |
| 0x14 | `DROP` | M → S | no | planned ([streaming.md](streaming.md)) |
| 0x15 | `SESSION` | M → S | no | planned |
| 0x16 | `STATUS` | S → M | yes | only as the answer to `APPLY` |
| 0x17 | `TEST` | M → S | no | planned |
| 0x18 | `APPLY` | M → S | yes | use the pack or state transferred |
| 0x19 | `RESET` | M → S | yes | start the device over |
| 0x20 | `STATE_GET` | M → S | yes | |
| 0x21 | `STATE_DATA` | S → M | yes | |
| 0x22 | `STATE_PUT` | M → S | yes | |
| 0x30 | `LOG` | S → M | sent by nothing yet | text |
| 0x31 | `PROFILE` | S → M | no | planned |
| 0x32 | `PPU_SNAPSHOT` | both | yes | the video state |
| 0x33 | `INPUT` | both | yes | section 10 |
| 0x38, 0x39 | `MEM_READ`, `MEM_WRITE` | M → S | no | the debugger's `m` and `M` do this (section 8) |
| 0x40 | `GDB` | both | yes | section 8 |
| 0x50 | `WATCHDOG` | S → M | no | planned |
| 0x7F | `ERROR` | both | no | planned |

### Payloads

All values little endian.

| Packet | Payload |
|---|---|
| `HELLO` M → S | empty, or u32 the first chunk to list (runtime version 4 on): the master asks again from where the list stopped until it has all chunks |
| `HELLO` S → M | u16 protocol (3), u16 runtime version (6; 5 takes packs with left-out chunks, 6 asks for rooms: [runtime-abi.md](runtime-abi.md) §0), u16 ABI major, u16 ABI minor and u32 profile (those of the running pack's header), u32 the largest pack that can arrive while the running one stays, u32 size and u32 CRC of the running pack, u32 chunk count, u32 n, u32 the largest pack at all (it then replaces the running one, [runtime-abi.md](runtime-abi.md) §0), u32 the largest pack that replaces it at its place (0: none), then n × (u32 hash, u32 size, u32 offset) of the running pack's chunks from the first asked for (0 if none), n at most 81, then (runtime version 6) u32 the room the game waits for: the string id of its id + 1 (0: none). Beside the running pack the runtime has every chunk with the same hash and size; over it at its place only those also at the same offset (and only if both chunk tables ascend, as the cooker writes them); over it elsewhere none |
| `PING` | any bytes; the slave sends them back |
| `TOC` | u32 transfer ID, u32 pack size, u16 part, u16 parts, then at most 1012 bytes of the stream "pack header (36 bytes) + chunk table". Part 0 must hold the whole pack header; it starts a transfer into the content pool beside the running pack, or over it if it does not fit beside it (the game then holds still until a pack is applied); that place is first filled with zeros up to the pack size. The header goes to offset 0 and the table to where the header says. After the last part the runtime marks every left-out chunk (absent, [pack-format.md](pack-format.md) §0) as present, copies every other chunk the running pack also has (same hash and size) and marks it present; over the running pack at its place it marks the chunks that are there already (same offset, size and hash) and zeroes only the gaps between chunks. A pack larger than the pool, more than 512 chunks or a table outside the pack make the transfer fail (reported by `APPLY`) |
| `CHUNK_BEGIN` | u32 transfer ID, u16 chunk index, u16 0, u32 size, u32 hash |
| `CHUNK_DATA` | u32 transfer ID, u16 chunk index, u16 0, u32 offset in the chunk, data (at most 1012 bytes), copied to the chunk's place in the new pack. Outside the chunk: the transfer fails |
| `CHUNK_END` | u32 transfer ID, u16 chunk index: marks the chunk present. The chunk's hash is not checked here; the pack's CRC is, at `APPLY` |
| `APPLY` | u32 transfer ID, u8 mode (0 restart, 1 hot, 2 hot with migration, 3 module, 4 state; 2 is answered "not supported"), 3 × u8 0, then optionally the text of a room ID from byte 8 to the end of the payload, at most 63 bytes, no terminator (the room to start in; used by restart). Modes 0, 1 and 3: every chunk must be present and the pack must open with a correct CRC; mode 4 loads the last `STATE_PUT`. Applied at a frame boundary and answered with `STATUS` |
| `STATUS` (24 bytes) | u8 kind (1 answer to `APPLY`; 0 is reserved for reports), u8 result (0 ok, 1 transfer incomplete, 2 CRC mismatch, 3 too large, 4 pack refused, 5 state of another layout, 6 mode not supported), u8 mode, u8 0, u32 transfer ID, u32 game frame, u32 room index, s16 x, s16 y, u32 reloads applied |
| `STATE_GET` | u8 flag mask ([state.md](state.md)); without it, snapshot |
| `STATE_DATA`, `STATE_PUT` | u32 offset, u32 total size, then a part of the serialised state ([state.md](state.md) §3), at most 1016 bytes per packet, the parts in order from offset 0. The runtime sends the parts of `STATE_DATA` at frame boundaries as its send buffers take them, and answers a state that does not fit its buffer (3328 bytes) with one packet of total size 0. `STATE_PUT` only stores the parts; the state counts as received with the part that reaches the total; `APPLY` with mode 4 loads it |
| `RESET` | empty. 20 frames later (so the acknowledgement gets out) the device starts over ([gba-backend.md](gba-backend.md) §4) |
| `INPUT` | section 10 |
| `LOG` | text, no terminator |
| `PPU_SNAPSHOT` M → S | u32 offset, u32 length (0: to the end) of the runtime's video state as a PPU snapshot (`BMPS`, [gba-backend.md](gba-backend.md) §6; 101,422 bytes) |
| `PPU_SNAPSHOT` S → M | u32 offset, u32 total size, then data (at most 1016 bytes). The runtime sends the parts at frame boundaries, as many per frame as its send buffers take; the registers are the values last written (the GBA HAL keeps copies, most video registers are write-only) |

The master does not wait for chunk-level answers: a pack transfer is `HELLO` (to learn which
chunks are there), `TOC`, then for each chunk the runtime lacks `CHUNK_BEGIN`,
`CHUNK_DATA`…, `CHUNK_END`, in windows; the only round trip is `APPLY` → `STATUS`. If the
running pack has more than 82 chunks the master cannot know about the rest and sends them;
that is harmless.

Packets are handled in the order received, once per frame, between two game frames. A packet
whose answer finds no free send buffer waits for the next frame.

## 4. Error handling

- **Retry**: on NAK, CRC mismatch or a timeout (no ACK after a set number of polls), the packet
  is sent again with the same sequence number.
- **Resync**: after 4 failed attempts the master is quiet for 100 ms, then sends four
  `0xFFFFFFFF` and `SYNC` (section 3). Both sides then continue with the sequence numbers
  from the last ACK.
- **Word boundary**: the wire has no frame for words. If the slave's port is armed while a
  word is already under way (the runtime starting while the master polls, an interrupt that
  comes late, a glitch on the clock), every later word is cut at the wrong place, on both
  sides; seen on hardware as the idle word arriving rotated by two bits. So the slave arms its
  port anew whenever no word arrived during a frame, and the master is quiet for 100 ms before
  each resync and session start. The emulated port moves whole words and cannot show this.
  The slave looks only once per two frames of real time, however often its main loop runs: a
  first version looked at every pass of the loop, and a GBA catching up in the input mirror
  (section 10) then armed its port anew between two words of the same packet and cut them
  apart itself.
- **Atomic apply**: the slave applies nothing until `APPLY` arrives, every chunk is present and
  the pack's CRC is right. A transfer that broke off is started again with a new `TOC`; the
  chunks of the running pack are then copied again and only the rest is sent.
- **Adaptive delay** (optional, off for the Xboo cable, whose delay is fixed at 8): after 8
  clean windows the delay goes down by one, after a failed one it becomes twice itself plus
  one, at most 64.
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

- multiboot of the game image (83 KB), then the runtime answers: works;
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
| `i` info | | from byte 4: u8 stopped, u8 breakpoint slots (8), u8 the device can be stopped, u8 0 |
| `h` halt | | none; a `S` packet follows when the program has stopped at the next frame boundary |
| `c` continue | | result |
| `g` / `G` registers | `G`: 17 words | `g`: r0–r15 and cpsr (17 words) |
| `m` read memory | u32 address, u16 length (≤ 512) | the bytes |
| `M` write memory | u32 address, bytes | result |
| `Z` / `z` breakpoint | slot, u32 address (`Z`) | result |
| `S` stopped (from the runtime) | byte 1: 0 asked to, 1 breakpoint; byte 2: slot (0xFF when asked to) | |

Commands are safe to repeat (a write or a breakpoint done twice is the same as once), so a
command whose answer had to wait for room in the send ring is simply handled again.

Memory that can be read: EWRAM, IWRAM, the I/O registers (0x04000000 to 0x040003FF), palette,
video memory (to 0x06017FFF), the sprite table, and ROM. Written: only EWRAM and IWRAM. A
breakpoint needs an address in RAM that is a multiple of 4; the word there is replaced by
`0xEA000000 | (((landing - (address + 8)) >> 2) & 0xFFFFFF)`, where slot n's landing place is
12 bytes after slot n-1's, and put back when the breakpoint is cleared. When it stops there,
r15 in the registers is the breakpoint's address.

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
(gdb) break bat.ms:13
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
| runs the runtime with other content | the pack first, only the chunks it lacks (sections 3, [streaming.md](streaming.md)), applied as a restart; then the state. A pack larger than the runtime takes (a multiboot GBA) is fitted around the room the sender is in ([runtime-abi.md](runtime-abi.md) §0), or, where the sender has a smaller pack for this (`bmTransferSendWith`, its rooms numbered as the first's), that one is sent instead: the test game's cartridge sends the test level without its two streamed torch halls |
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
words (as for packets, starting anew). Size in bytes, a multiple of 4, at most 256 KB. The
words: `START` 0x424D4253, `READY` 0x424D4252, `OK` 0x424D4F4B, `BAD` 0x424D4E4F, idle
0x424D4249.

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

## 10. Input mirror: one plays, the other plays along

`bmlink --xboo <port> mirror from <[host:]port of the PC game>` makes the PC game show what
happens on the GBA; `mirror to ...` is the other direction: the game is played in the PC window
and the GBA plays along. In the editor these are the buttons "Sync: Off, GBA > PC, PC > GBA"
next to Run; switching ends the running mirror and starts the other. What follows describes
the first direction; the other is the same with the roles swapped. Nothing but the keys is sent: the game is deterministic (same state, same keys,
same result on both machines, bit for bit), so the PC game that starts from the GBA's state
and gets the GBA's keys for every frame plays the same game.

- The runtime, asked with `INPUT` (payload 1), notes before each game frame which keys that
  frame uses. It sends a packet at once when the keys changed and otherwise every second
  frame: the last frame whose keys are known, and the changes (frame, keys) since the last
  packet.
- `bmlink` first tells the PC game to get ready (`INPUT`, payload 3: it holds still from then
  on, so it plays no frames of its own between getting the state and the first keys), starts
  the reporting, then reads the GBA's state (`STATE_GET`) and loads it
  into the PC game (`STATE_PUT`, `APPLY` state), then passes every `INPUT` packet on.
- The PC game (`apps/game`, started normally or with `--follow` to wait) uses for frame *n*
  the keys the GBA used for frame *n*, never its own, and holds still while the next frame's
  keys are not there yet.

**Lag.** The cable is slow and the PC only gets a word when it polls, so keys arrive tens of
milliseconds late and not evenly. That delay never changes *what* happens, because keys are
applied by frame number and not by arrival time; it only makes the PC run a little behind the
GBA (the two frames between packets plus the transfer). When packets arrive in a bunch the PC
game runs one extra frame per screen refresh to catch up, so the delay does not add up.

**Keeping up.** The side that follows must not be slower than the side that plays, or the
delay grows without end. So: the PC game, while a GBA plays along, runs at the GBA's frame
rate (59.7275 frames per second, not 60); the following side works out its state's checksum
only for the frames that have one to compare with; `bmlink` asks the following side for its
reports only ten times a second and otherwise leaves the cable alone (every word costs a GBA
an interrupt); a follower more than two frames behind runs one extra frame before the next refresh; and
one that is more than 45 frames behind says so and gets the other side's state, a jump
instead of a growing delay.

**Payloads of `INPUT`.**

| From | Payload |
|---|---|
| master | one byte: 1 start reporting; 0 stop reporting and following; 3 get ready to follow (hold still until keys arrive) |
| the reporting side, passed on unchanged to the following side | u32 the last frame whose keys are known, u8 number of changes n (at most 16), u8 flags (1: changes were lost, 2: a check follows), u16 0; n × (u32 frame, u16 keys, u16 0); with flag 2: u32 frame, u32 CRC-32 of the state before that frame |
| the following side | u8 2, u8 reason (0 its state differed, 1 more than 45 frames behind), u16 0, u32 frame |

The first frame after "start" is always reported as a change. The state that is compared is
the serialised block `game` with the snapshot mask ([state.md](state.md) §6), and frames are
the game's frame counter, compared as signed differences. The follower keeps up to 64
changes; it uses for frame *f* the keys of the last change at or before *f*, once the "last
frame known" has reached *f*. It reports "behind" at most once per 120 frames.

**Checks.** Every 64 frames the runtime adds a CRC-32 of its game state to the packet. The PC
game compares it with its own state at that frame and reports a difference; `bmlink` then
loads the GBA's state again. The same happens if the runtime had more key changes than fit
(16) while nobody polled.

**Stopping.** Ctrl-C (or the editor's "Off") ends `bmlink mirror` in order: it tells both sides
to stop (`INPUT`, payload 0), and each plays on with its own keys from where it is. A side that
follows and hears nothing for about two seconds does the same by itself. While the mirror runs
it holds the cable; the editor lets it step aside for a hot reload (which then goes to both
sides) and starts it again.

Tested in mGBA in both directions (the playing side driven by its autopilot, 800 to 900 frames
through a room change, every check equal, no reload of the state) and on a real GBA over the
Xboo cable: the PC following the GBA for 1800 frames, and the GBA following the PC game's
autopilot for 1180 frames into the second room. Both machines must run the same pack.

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
- 0.6: brought in line with the code: the header word, the CRC, the answer to `SYNC` and
  `SESS`, sequence number rules, buffer and retry numbers, which packet types are built,
  `RESET` and `INPUT` in the tables, the multiboot sequence, memory the debugger may touch.
- 0.7: protocol version 2: `STATE_DATA` and `STATE_PUT` carry the state in parts (offset,
  total size, data), since a snapshot with the inventory is larger than one packet.
- 0.4.1: `SESS`. Found on hardware: a second run of `bmlink` counted its packets from 0 while the runtime went on from the first run's numbers, and nothing got through.
