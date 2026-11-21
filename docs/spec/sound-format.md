# Sound format

Version 1, draft. The reasons and the full feature target are in [../PLAN.md](../PLAN.md),
section 13; this document describes what version 1 of the engine (`src/sound/sound.h`) plays.

A **song** is a source asset ([asset-model.md](asset-model.md)): a `.bmsong` file (XML) with
instruments and tracks of notes. Music and sound effects are both songs; an effect is a short
song that plays once on top of the music. The file name without its extension is the song's
**name**: rooms name their music with the `music` property ([room-format.md](room-format.md)
§1), scripts call `play_music(#name)` and `play_sound(#name)`, and the engine plays the effects
named `jump` and `hurt` when the player jumps and is hurt.

```xml
<song version="1" bpm="112" length="384" loop="0" effect="0" echo="140" echo-feedback="70">
 <instrument name="lead" wave="pulse25" attack="1" decay="12" sustain="150" release="10" volume="150" pan="-12"/>
 <instrument name="kick" wave="sample" sample="kick.wav" root="60" loop="0" attack="0" decay="0" sustain="255" release="2" volume="230" pan="0"/>
 <track name="lead" instrument="lead">
  <note t="0" key="76" len="34" vel="105"/>
 </track>
</song>
```

| Element | Attributes | Meaning |
|---|---|---|
| `song` | `bpm` (20-400); `length` and `loop` in ticks, 24 per beat (`loop`: where the song repeats from, -1: it plays once); `effect` (1: a sound effect); `echo` in ms (0: none, at most 225) and `echo-feedback` (0-255) | |
| `instrument` | `name`; `wave`: `pulse12`, `pulse25`, `pulse50`, `saw`, `triangle`, `sine`, `noise` or `sample`; for samples `sample` (a WAV file next to the song: uncompressed, 8 or 16 bit; converted to 8 bit mono), `root` (the key at which it plays at its own rate) and `loop` (1: it repeats while the note is held); envelope `attack`, `decay`, `release` in frames (1/60 s) and `sustain` (0-255); `volume` (0-255); `pan` (-64 left to 63 right) | |
| `track` | `name`, `instrument` | notes for one instrument |
| `note` | `t` (tick), `key` (MIDI number, 60 = middle C), `len` (ticks), `vel` (1-127) | |

## The engine

- **Rate**: 18157 Hz, signed 8-bit, stereo. One video frame is exactly 304 samples (timer 0
  with a period of 924 cycles), so the engine mixes one frame of sound per frame of game time
  and the output buffers swap at the vertical blank. Direct sound channel A is the left side,
  B the right.
- **Voices**: 8. A note takes a free voice; if there is none, the quietest voice that is
  fading out; if there is none, an effect takes the oldest voice of the music, and music the
  oldest of its own. The cooker reports songs that need more voices than there are
  (`sound-reduced`).
- **Waves**: the built-in waves are one cycle of 64 samples (the noise 2048 samples of a
  shift register); pitch comes from stepping through them in 20.12 fixed point, from a table
  of semitone ratios. Samples play at `rate / 18157 × 2^((key - root) / 12)`.
- **Envelope**: per voice, updated once per frame: attack to full level, decay to the sustain
  level, held until the note ends, then release. A time of 0 means at once.
- **Sequencer**: per frame the song's position advances by `bpm × 24 / 3583.65` ticks (the
  GBA shows 59.7275 frames per second); notes whose tick has come start. At `length` the song
  goes back to `loop` or ends.
- **Echo**: one delay line per side; the output of `echo` ms ago comes back multiplied by
  `echo-feedback / 256`. Only the music's song sets it.
- Everything is integer arithmetic and identical on every target: the test compares a
  checksum over all samples mixed in 1000 frames between the PC and the GBA.
- The engine is a struct with no global state besides the wave tables and a mixing scratch
  buffer, so the editor's preview runs the same code on its own instance.

### Cost on the GBA

Measured in mGBA with the test level (display lines a game frame takes, of 228), from ROM:
the mixer takes about 6 % of a frame as a fixed cost (clearing and writing 2 × 304 samples,
sequencer, envelopes), 4 % more with the echo, and about 2 % per sounding voice. The test tune
(5 to 7 voices, echo) takes about 19 % of a frame. The plan's target (16 voices under 15 %) is
not met: it needs the inner loop in assembly and more voices than 8.

### Not in this version

Volume ramps within a frame (changes are steps at frame boundaries), interpolation, the
original PSG channels, pulse width modulation and other synthesis, tracker effects (vibrato,
portamento, arpeggio as effects), filters, separate rates for music and effects, tempo and
automation tracks, adaptive music (sections, layers), import of MIDI and tracker files. The
position in a song is not part of the game state yet: a loaded state starts the room's song
from its beginning.

## The `SONG` chunk

```
0   u16 name (string id)        u16 flags (1: effect)
4   u16 instruments             u16 tracks (for information)
8   u32 events                  u32 length in ticks
16  u32 loop tick (0xFFFFFFFF: plays once)
20  u32 ticks per frame, 16.16
24  u16 echo delay in samples (0: none)   u8 echo feedback   u8 0
28  u32 offset of: instruments, events, sample data

instrument (28 bytes): u8 wave (0-6 as listed above, 255: sample), u8 volume, s8 pan, u8 sustain,
                       u16 attack, decay, release (level change per frame; the level is 0-65535; 0: at once), u16 0,
                       u32 sample offset, u32 sample length, u32 loop start (0xFFFFFFFF: none),
                       u32 step at key 60 (20.12)
event (12 bytes):      u32 tick, u16 length in ticks, u8 instrument, u8 key, u8 velocity, 3 × u8 0
samples:               signed 8-bit
```

Events are sorted by tick. A reader MUST check that the tables and every sample lie inside the
chunk and that every event names an instrument that exists, before playing (`bmSoundSongValid`).

## History

- 1: first version.
