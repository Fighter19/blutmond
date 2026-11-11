# Link protocol

Version 0.2, draft. Implemented in `src/link/` (both sides) and `tools/bmlink/`.

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
| 0x40 | `GDB` | both | GDB remote serial protocol data, tunnelled |
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

The rate on a real Xboo cable still has to be measured (`bmlink --xboo /dev/parport0 ping 50`).

## History

- 0.1: first draft.
- 0.1.1: window acknowledgements; master-driven streaming with `DROP`, `SESSION`, `STATUS`,
  `TEST`.
- 0.2: busy NAK, one answer per burst of polls, answers may be more than one word late,
  payloads of `HELLO`, `TOC`, `CHUNK_*`, `APPLY`, `STATUS`, `STATE_*`; measurements.
- 0.2.1: `PPU_SNAPSHOT` payloads. An acknowledgement that arrives while the slave is in the
  middle of its next packet must not restart that packet (fixed in the slave: before, every
  packet that followed another one directly was sent twice).
