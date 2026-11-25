# Sound format

Version 1.3, draft. The reasons and the full feature target are in [../PLAN.md](../PLAN.md),
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
| `song` | `bpm` (20-400); `length` and `loop` in ticks, 24 per beat (`loop`: where the song repeats from, -1: it plays once); `effect` (1: a sound effect); `echo` in ms (0: none, at most 225) and `echo-feedback` (0-255); `intensity` (0-15, default 15: the intensity a room starts the song at, its first mode) and `fade` (0-1020 frames an instrument takes to come in or go out, default 0: 43); dynamic music, below | |
| `instrument` | `name`; `wave`: `pulse12`, `pulse25`, `pulse50`, `saw`, `triangle`, `sine`, `noise` or `sample`; for samples `sample` (a WAV file next to the song: uncompressed, 8 or 16 bit; converted to 8 bit mono), `root` (the key at which it plays at its own rate) and `loop` (1: it repeats); envelope `attack`, `decay`, `release` in frames (1/60 s) and `sustain` (0-255); `volume` (0-255); `pan` (-64 left to 63 right); `layer` (0-15, default 0): the layer it belongs to (dynamic music, below) | |
| `layer` | `id` (0-15), `name` (for people), `from` and `until` (0-15): the music intensities the layer's instruments are heard at; a layer without an entry is heard from its id to 15 | a group of instruments |
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
  `echo-feedback / 256`. Only the music's song sets it, and a room's acoustics replace it
  while the player is in that room ([room-format.md](room-format.md) §1).
- Everything is integer arithmetic and identical on every target: a test compares a
  checksum over every sample mixed while the test level is played for 1000 game frames,
  between the PC and the GBA. The next section has the arithmetic.
- The engine is a struct with no global state besides the wave tables and a mixing scratch
  buffer, so the editor's preview runs the same code on its own instance.

### Dynamic music, fades, pitched effects

The terms are the usual ones of adaptive (dynamic) music: **vertical layering** (also vertical
re-orchestration) brings instruments of a piece in and out while it plays on; **horizontal
re-sequencing** jumps between sections of it. This engine does the first, which also makes
**modes**: versions of one piece over the same chords and length, of which one is heard at a
time, so that any moment can fade into the other mode.

- **Layers.** Each instrument of a song played as music belongs to a **layer** (0-15), a group
  of instruments; a layer is heard while the engine's **intensity** (`bmSoundSetIntensity`, the
  script's `music_intensity`; 15 at the start) is from its `from` to its `until`. By default
  layer n is heard from n to 15: layer 0 always, the others adding to the ones below (the moon
  bat's song). A layer ending lower makes a **mode** that gives way to the next: test_chamber
  has `always` (0-15, the pad), `chamber` (0-0), `escape` (1-1: its bass and bleeps), `drive`
  (1-15: the kick and the lament) and `pursuit` (2-15). The cooker counts the voices a song
  needs at each intensity and reports the most; while one mode fades into another, a few more
  sound for the fade's time.
- **Fading.** Each layer has a volume of 0 to 4096 that moves towards 4096 (heard) or 0 by the
  song's step a frame, at the start of a frame: 96 (43 frames) without a `fade`, else
  `ceil(4096 / (4 * f))` with `f` the chunk's fade byte. A song started as music begins with
  each layer at its target (no fade). A note of a layer whose volume is 0 takes no voice.
- **A room's song** (`bmSoundPlayRoomMusic`) starts at the song's own intensity, so in its
  first mode; the song already playing goes on in the mode it is in, from room to room. A
  script's `play_music` keeps the intensity the script set.
- **The music's volume** (0-256) moves towards a target by a step a frame (`bmSoundFadeMusic`:
  `step = ceil(|target - volume| / frames)`, at least 1; `frames` 0: at once). With `stop`, the
  music stops when it reaches 0. Starting a song as music sets it to 256; asking for the song
  already playing fades it back to 256 in 30 frames.
- A music voice's volume is multiplied by `(music volume * (layer volume >> 4)) >> 8`, then
  `>> 8`; at 256 and 4096 nothing changes, so songs without layers sound as before.
- **Pitched effects**: `bmSoundPlayEffectAt(name, semitones)` adds `semitones` to every note of
  the effect (keys kept in 0..127): the dialogue's voices.

### The arithmetic, exactly

What a second implementation needs to produce the same samples. `>>` is an arithmetic shift;
`/` truncates. Chunk fields are named as in "The `SONG` chunk" below.

**Wave tables**, signed 8 bit, 64 samples, index i = 0..63:

| Wave | Sample i |
|---|---|
| 0 pulse 12.5 % | 100 for i < 8, else -100 |
| 1 pulse 25 % | 100 for i < 16, else -100 |
| 2 pulse 50 % | 100 for i < 32, else -100 |
| 3 saw | `-124 + 4i` |
| 4 triangle | `-124 + 8i` for i < 32, else `124 - 8(i - 32)` |
| 5 sine | with `q = i & 15`, quarter table Q = 0 12 25 37 49 60 71 81 90 98 106 112 117 122 125 126 127: `Q[q]` in quadrants 0 and 2, `Q[16 - q]` in 1 and 3, negated in quadrants 2 and 3 (quadrant = `i >> 4`) |
| 6 noise | 2048 samples: `lfsr = 0x1ACE5`; for each sample `lfsr = (lfsr >> 1) ^ (lfsr & 1 ? 0xB400 : 0)`, then the sample is `(lfsr & 0xFF) - 128` |

**Pitch.** A voice has a position and a step, both with 12 fraction bits; it plays
`data[position >> 12]` and then adds the step. The step at key 60 is 3777 for waves 0-5, 4096
for noise, and the instrument's field for samples. For another key: `n = key - 60`, brought
into 0..11 by adding or subtracting 12 while counting octaves; `step = (step60 * S[n]) >> 16`
(64-bit) with S = 65536 69433 73562 77936 82570 87480 92682 98193 104032 110218 116772 123715;
then shifted left by the octaves (or right for negative ones); at most 0x7FFFFFFF, at least 1.

**End of the data.** Built-in waves repeat. For a sample: when the position reaches
`length << 12`, a sample without loop ends the voice; one with a loop continues at
`position - ((length - loop start) << 12)`. The loop also holds while the note fades out.

**Starting a note** (`noteOn`): pick a voice, the lowest-numbered one in each case: a free
one; else the one fading out with the lowest level; else, for an effect's note, the music's
oldest note, and for a music note, the music's own oldest note. If there is none (a music
note when the effect holds all voices), the note is dropped. The voice starts with position
0, level 0, in the attack phase, with the instrument's values, velocity limited to 127, the
note's length in frames (at least 1), and the frame counter as its age.

**The sequencer**, once per frame and song (music, then effect): `tick += ticks per frame`
(16.16). Then every event not yet played with `event tick <= min(tick >> 16, length)` and
`event tick < length` starts its note, in the chunk's order; the note's length in frames is
`(length in ticks << 16) / ticks per frame`, at most 65535. If `tick >> 16 >= length`: without
a loop the song stops (its notes play out); with one, `tick -= (length - loop tick) << 16`,
the next event is the first one at or after the loop tick, and the same is done again (at
most four times in a frame).

**One frame** (`bmSoundFrame`, 304 samples): the frame counter goes up; the two sequencers
run; then for the voices 0 to 7 in order, each active voice first runs its envelope and is
then mixed. So a note sounds from the frame it starts in.

**Envelope**, level 0..65535, sustain level = `sustain << 8`:

- attack: if the rate is 0 or `level + rate >= 65535`: level 65535, go to decay; else add the
  rate.
- decay: if `level <= sustain level`, or `level - sustain level <= rate`, or the rate is 0:
  level = sustain level, go to sustain; else subtract the rate.
- sustain: nothing.
- release: if the rate is 0 or `level <= rate`: the voice ends (it is not mixed this frame);
  else subtract the rate.
- then: if not in release and the frames left reach 0 (counted down here), release begins,
  taking effect in the next frame. A voice in sustain whose sustain level is 0 ends.

**Volume and sides**: `vol = min(256, ((level >> 8) * velocity * instrument volume) >> 15)`.
`left = pan > 0 ? (vol * (64 - pan)) >> 6 : vol`; `right = pan < 0 ? (vol * (64 + pan)) >> 6 :
vol`.

**Mixing**: per sample and side a 32-bit sum starting at 0; each voice adds `sample * left`
and `sample * right`. A voice whose two volumes are 0 is not mixed, but its position moves on
by `step * 304` (a sample without loop that passes its end ends).

**Output**: `clamp(sum >> 8, -128, 127)`. With an echo: `clamp((sum >> 8) + e)`, where `e =
line[p] * feedback / 256` rounded towards zero (so the echo dies away to 0; rounded down, a -1
came back for ever) and `line` is the delay line of that side; the result is written back to
`line[p]`, and `p` goes on, wrapping at the delay. The echo is on while the music's song has a
delay other than 0; delay at most 4096 samples.

**A room's acoustics** (`room`: on, delay, feedback, damping, muffle) replace the song's
delay and feedback while on (a delay of 0 is run as 1 with feedback 0). With damping or
muffle, two one-pole low passes per side, kept in 8.8: `d += ((line[p] << 8) - d) * (256 -
damping) >> 8` and `e` from `d >> 8` instead of `line[p]`; and after writing the line, `m +=
((out << 8) - m) * (256 - muffle) >> 8`, the sample put out being `m >> 8`. The delay lines
are not cleared when a room's acoustics start or end: what rings goes on into the new
setting. A song started as music while a room's acoustics are on leaves the delay lines
alone.

**Starting a song**: the notes of the song it replaces (music or effect) get one frame left,
so they fade out. Position and next event to 0. For music also: the delay and feedback from
the chunk, the echo position to 0, both delay lines to silence. Asking for the music that is
already playing does nothing. Songs are found by name, the first `SONG` chunk in pack order
whose name is equal.

**The checksum** tests compare, starting at 0: for each sample pair `a += left byte; b += a;
a += right byte; b += a`, both kept to 16 bits at the end of a frame as `a | b << 16` (bytes
taken unsigned).

**What the cooker puts into the chunk:**

- ticks per frame = `round(bpm * 24 * 65536 / 3583.65)`;
- echo delay = `ms * 18157 / 1000`, at most 4096;
- attack rate = `max(1, 65535 / attack)`, decay rate = `max(1, (65535 - sustain * 256) /
  decay)`, release rate = `max(1, 65535 / release)`, each 0 if the time is 0. So a release
  lasts its number of frames from full level, and proportionally shorter from the sustain
  level;
- a sample's step at key 60 = `round(sample rate / 18157 * 4096 * 2^((60 - root) / 12))`;
- events of all tracks sorted by tick, ties in track order (this decides which note gets
  which voice); notes outside the song's length are dropped;
- an effect never loops.

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
automation tracks, horizontal re-sequencing (jumps between sections; layers and modes are there), import of MIDI and tracker files. The
position in a song is not part of the game state yet: a loaded state starts the room's song
from its beginning.

## The `SONG` chunk

```
0   u16 name (string id)        u16 flags (1: effect; 2: layer table; bits 8-11: 15 - the song's intensity)
4   u16 instruments             u16 tracks (for information)
8   u32 events                  u32 length in ticks
16  u32 loop tick (0xFFFFFFFF: plays once)
20  u32 ticks per frame, 16.16
24  u16 echo delay in samples (0: none)   u8 echo feedback   u8 fade (in 4 frames: round(fade / 4), at least 1; 0: none)
28  u32 offset of: instruments, events, sample data
40  with flag 2: 16 × u8, layer 0 to 15's range: from in bits 0-3, until in bits 4-7
    (without it, layer n is n to 15; the cooker writes the table only if a range differs)

instrument (28 bytes): u8 wave (0-6 as listed above, 255: sample), u8 volume, s8 pan, u8 sustain,
                       u16 attack, decay, release (level change per frame; the level is 0-65535; 0: at once), u16 layer (0-15),
                       u32 sample offset, u32 sample length, u32 loop start (0xFFFFFFFF: none),
                       u32 step at key 60 (20.12)
event (12 bytes):      u32 tick, u16 length in ticks, u8 instrument, u8 key, u8 velocity, 3 × u8 0
samples:               signed 8-bit
```

Events are sorted by tick. A reader MUST check that the tables and every sample lie inside the
chunk and that every event names an instrument that exists, before playing (`bmSoundSongValid`).

## History

- 1: first version.
- 1.1: the arithmetic written out.
- 1.2: layers and intensity, the music's volume and fades, pitched effects.
- 1.3: layers as groups with ranges of their own (`<layer>`, the chunk's layer table): modes
  of a song; a song's own fade time and start intensity. Songs of 1.2 cook to the same bytes
  and sound the same.
