# Link protocol

Version 0.1, draft.

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

The device that makes the clock is the **master**; the runtime is always the **slave** when
talking to a PC.

## 2. Boot

1. The master boots the GBA with the BIOS multiboot protocol exactly as xcomms `NormalBoot`
   does: attention handshake (`0x6202` until the reply has `0x72` in the top byte), header
   (`0x6102`, 0x60 halfwords), key exchange (`0x63xx`, `0x64xx`), length, encrypted data,
   CRC check. The per-word delay is the same setting as xcomms' "Transfer Delay".
2. The image is either the full runtime (multiboot case) or a small bootstrap that starts the
   runtime from the cartridge.
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

After the last word of a packet the receiver answers ACK or NAK (the master polls for it).
The slave sends its own packets when polled; the master acknowledges them the same way.
Sequence numbers count per direction and wrap at 256.

### Packet types

| Type | Name | Direction | Payload |
|---|---|---|---|
| 0x01 | `HELLO` | both | protocol version, runtime version, ABI version, profile, capabilities, free content memory, hashes of the chunks currently loaded |
| 0x02 | `PING` | both | – |
| 0x10 | `CHUNK_BEGIN` | M → S | transfer ID, chunk ID, size, unpacked size, compression, hash |
| 0x11 | `CHUNK_DATA` | M → S | transfer ID, offset, data |
| 0x12 | `CHUNK_END` | M → S | transfer ID; the slave checks the hash and NAKs on mismatch |
| 0x13 | `TOC` | M → S | new pack header and table of contents (see [pack-format.md](pack-format.md) §5) |
| 0x18 | `APPLY` | M → S | reload mode, transfer IDs, start room/door (see [runtime-abi.md](runtime-abi.md) §4) |
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

## 4. Error handling

- **Retry**: on NAK, CRC mismatch or a timeout (no ACK after a set number of polls), the packet
  is sent again with the same sequence number.
- **Resync**: after 4 failed attempts the master sends four `0xFFFFFFFF` and `SYNC`. The slave
  drops any partial packet and answers the next poll with `SYNC`. Both sides then continue with
  the sequence numbers from the last ACK.
- **Resume**: chunks are confirmed packet by packet, so a transfer continues where it stopped.
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

## History

- 0.1: first draft.
