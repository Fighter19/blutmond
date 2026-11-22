# Findings from the first runs on a real GBA

Everything up to milestone M10 was developed against the software PPU and mGBA. This file
records what the first runs on a real GBA over an Xboo cable showed that emulation had not,
and what it took to make the input sync (one of GBA and PC plays, the other plays along) work.
The protocol itself is in [spec/link-protocol.md](spec/link-protocol.md); how to use the
tools is in [USAGE.md](USAGE.md).

Setup these findings come from: a GBA without a cartridge, a four-wire Xboo cable on a PC
parallel port (`/dev/parport0`, Linux `ppdev`, about 2.5 µs per port access), Ubuntu 24.04,
`arm-none-eabi-gcc` 13.2.

## 1. What emulation could not show

Each of these stopped the link or the sync completely on hardware while every emulator test
passed.

| # | What happened on the GBA | Why the emulator did not show it | Fix |
|---|---|---|---|
| 1 | The BIOS would refuse our multiboot image after its header | mGBA's built-in BIOS does not check the header | `bmlink boot` puts the logo, fixed value, device type and complement check into the header before sending, as xcomms' "header fix" does. The logo is not in the repository; it is read from a file (section 3) |
| 2 | The second run of `bmlink` got nothing through | every emulator test starts a fresh GBA, so a second master never met a running runtime | every master starts with a session word (`SESS`) that puts both sides' packet numbers at 0 |
| 3 | After loading, the runtime "did not connect"; a push stopped after 9 KB | the emulated serial port moves whole words | the word boundary, section 2 |
| 4 | With the GBA following the PC, the link broke again (30 bad packets and 12 resyncs in 30 s) and the state was loaded over and over, restarting the music each time | the same, plus the emulated GBA was never short of time | the boundary check ran on every pass of the main loop; now once per two frames of real time (section 2) |
| 5 | The GBA following the PC ran slow, its sound was choppy, and it fell more than a minute behind | the emulated GBA runs as fast as it is polled | section 4 |
| 6 | Run in the editor did nothing, without a message | not a hardware matter: a mirror process left over from an ended editor still held the parallel port, and claiming a busy port waits without end | `bmlink` locks the port device and reports another `bmlink`; it ends with the program that started it; the editor says what a job waits for |

Two smaller ones:

- `build-gba` configured without the build type's flags compiled unoptimised, and the
  multiboot images overflowed EWRAM. The GBA build now adds `-Os` itself when the C flags
  carry no optimisation level.
- The multiboot game was down to about 500 bytes of free EWRAM. Its two buffers for pushed
  content were cut to 48 KB each, which limited a pushed pack to 48 KB. They are one pool now
  (about 140 KB, [spec/runtime-abi.md](spec/runtime-abi.md) §0): a pack up to that size can be
  pushed, beside the running one when both fit, otherwise over it.

One that was not software at all: for a while the GBA answered nothing because the cable's
plug was pushed too far into the GBA. The symptom is described in section 5.

## 2. The word boundary

The serial port in normal mode moves 32 bits per word and has no frame around them. If the
GBA arms its port while the PC is already in the middle of a word, every later word is cut at
the wrong place, in both directions. Seen directly: the runtime's idle word `0x424D4944`
arrived as `0x10935251`, the same bits rotated by two, on every exchange. No resync got
through, because the resync words were cut apart as well.

It happens when:

- the runtime starts while the PC is already polling (right after a boot);
- the GBA's serial interrupt arms the port too late for the next word. At `bmlink`'s old word
  delay of 1 the gap between words was about 15 µs;
- the port is armed anew at the wrong moment (finding 4 above: our own cure, applied too
  often).

What makes it robust now:

- **The runtime** arms its port anew when no word arrived for a while (`bmHalLinkRealign`).
  "A while" is measured in real time, once per two frames, however often the main loop runs.
- **The master** is quiet for 100 ms before every resync and every session start, so the
  runtime gets that pause.
- **The word delay** on the Xboo cable is 8 by default instead of 1 (about 120 µs between
  words).

Rule for anything new on the GBA side: never touch `SIOCNT` outside the serial interrupt and
`bmHalLinkRealign`, and never call the latter in a way that depends on how fast a loop runs.

## 3. The logo

A real GBA only starts an image whose header contains the 156 logo bytes. They are not part
of this repository (see `tools/gbafix.py`). `bmlink boot` takes them from a file:

- `--logo FILE`: a file with exactly the 156 bytes, or any GBA image to copy them from;
- default `~/.config/blutmond/logo.bin`.

Without a logo `bmlink` warns and sends the image anyway; the BIOS then refuses it after the
header. Emulators do not care.

## 4. What the input sync needed

The idea is simple: the game is deterministic, so the side that follows only needs the other
side's state once and then its keys for every frame. Making that hold on a slow cable took
these, in the order they were found:

1. **Keys by frame number, not by arrival time.** The playing side reports which keys each
   game frame used; the other uses them for the same frame. Lag then only delays, it never
   changes what happens.
2. **Keys first, then the state.** Reporting is switched on before the state is read, so no
   key change between the two is missing.
3. **The follower holds still before it gets the state.** Otherwise it plays a few frames of
   its own between the state arriving and the first keys (found as a failed first state
   check).
4. **Compare only the game's own state.** The autopilot's variables are part of a snapshot
   but are not played along; including them made every check after a jump differ.
5. **A state checksum every 64 frames**, and a reload of the state when it differs or when
   key changes were lost.
6. **An orderly stop.** Both sides are told; a follower that hears nothing for about two
   seconds goes back to its own keys, so it never stays frozen.
7. **The follower must be able to keep up** (finding 5). Four things were wrong:
   - the follower worked out the state checksum before *every* frame instead of only for the
     frames that have one to compare with;
   - `bmlink` polled the follower all the time, and every word costs a GBA an interrupt. It
     now asks ten times a second and leaves the cable alone when there is nothing to pass on;
   - the PC ran at 60 frames per second, a GBA draws 59.7275. The PC game now runs at the
     GBA's rate while a GBA plays along;
   - nothing limited the delay. A follower more than 45 frames behind now asks for the other
     side's state: a jump instead of a delay that grows. Catching up is one extra frame per
     refresh, so the sound stays whole.

Measured on the GBA after all of this, PC playing with random key presses: 3600 frames in
60.1 s, no reload of the state, no jump, no bad packet, about 300 words per second on the
cable (before: about 2200).

## 5. Reading the cable

When nothing works, a raw exchange tells a lot. `BMLINK_TRACE=N bmlink ...` prints the first
N words each way.

| What comes back | What it means |
|---|---|
| `0xFFFFFFFF` to everything, the port's status never changes | the GBA is not shifting anything out: off, not waiting in its BIOS, or SO does not reach the PC |
| the word just sent, in the same exchange, perhaps with stray bits | the PC reads its own SI output: SO is not connected (this is what the plug pushed in too far looked like) |
| `0x424D4944` ("BMID") | the runtime is running and idle |
| a constant word that is "BMID" rotated by some bits | the word boundary is lost (section 2); a runtime without the fix needs a reset |
| `0x4241ss00` | an acknowledgement for packet `ss` |

The BIOS answers the attention word `0x6202` with `0x72` in the top byte once it is ready.

## 6. Measurements

| What | Result |
|---|---|
| Multiboot of the game image (83 KB) | works; the runtime answers right after |
| 5 KB there and back | 2270 words/s (9 KB/s) at word delay 8; 2950 words/s at delay 1 (not reliable, section 2) |
| Restart with a changed level (13 of 21 chunks, 26 KB) | 3.4 s |
| Restart with a level it already has | 0.4 s |
| Hot reload of the changed level | 3.4 s, applied while the game ran |
| Video state for the editor's graphics view (101 KB) | 11 s |
| `bmlink boot` over a running runtime (reset by the runtime, then multiboot) | works |
| Sync GBA > PC | 1800 frames, 28 state checks, all equal |
| Sync PC > GBA, random keys | 3600 frames in 60.1 s, no reload, no jump |

## 7. Not tried on hardware yet

- GBA to GBA (M10): two GBAs and a link cable; so far only between two emulated GBAs.
- GDB over the link (M8): only against the emulator, with a test client instead of a real
  `arm-none-eabi-gdb`.
- Native script modules (M8) on the device.
- The reset wire: pulsing it did reset the GBA, but it did not come back waiting for
  multiboot in our tries; the runtime's own reset (`bmlink reset`) is what works.
- Sound quality: frame load and timing were measured, nobody has compared the sound by ear
  against the PC, and the mixer still misses its CPU target (see the plan, M9).
- The 256 kHz fallback for GBA to GBA, SD burst on the Xboo cable.
