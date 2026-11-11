# Asset model

Version 0.1, draft.

## Principle

Everything a developer makes MUST be portable to other retro platforms in principle, even
where no runtime port exists. The project therefore stores **source assets** in a
platform-neutral model. Platform data is only a **build output** produced by the **cooker**
for a **platform profile**.

```
source assets (project) ──cook(profile = GBA)──►  pack for GBA
                        ├─cook(profile = SNES)─►  pack for SNES
                        └─cook(profile = HOST)─►  pack for PC/web
```

Profiles in version 0.1: `GBA` (first target), `SNES` (next target), `HOST` (PC and browser,
uses the GBA limits by default so it matches the device).

## Source assets

| Asset | Source form | What a profile decides when cooking |
|---|---|---|
| Image, tileset | indexed PNG plus palettes in 24-bit colour; the original true-colour image is kept next to it for re-quantising; tile size 8×8 | colour depth, palette count and size, colour format (BGR555 on GBA and SNES), tile deduplication with flips |
| Room | Tiled `.tmx` or the editor's room file, see [room-format.md](room-format.md) | number and kind of BG layers, map entry format, scrolling, collision cell size |
| Sprite, animation | frames as images, hitboxes, anchor, frame timing | OBJ sizes and shapes, meta-sprite split, per-line limits |
| Raster effect | parameters (e.g. wave: amplitude, period, speed) and the per-line table they produce | HDMA tables (GBA, SNES), shader (HOST), or a fallback |
| Entity class, world map | Mondskript source and text files | packed binary |
| Script | Mondskript source, see [mondskript.md](mondskript.md) | bytecode or native module |
| Music, SFX | event sequences; instruments described by sound (sample, pulse with duty, wave, noise, envelopes, effects) | mixer data (GBA), SPC700 data (SNES), mapping to sound chip channels |
| Text, dialogue | UTF-8 with string IDs | font and encoding |

Rules:

- Source formats MUST be open and documented (PNG, text, Tiled XML; a small binary only where
  text would be unreasonable) and carry a version.
- A platform-specific change (a hand-made palette for one profile, a simpler effect) MUST be
  stored as an **override** next to the source asset, keyed by profile. Overrides never replace
  the source.
- Graphics, palettes, sprites and sounds are assets of their own that rooms **reference**; a
  room never contains its own copy. How shared assets are cooked (global, area or room level,
  deduplication, fixed VRAM layout per area) is in [pack-format.md](pack-format.md) §2.
- The editor shows for every asset which rooms use it, and warns before a change to a shared
  asset that it affects several rooms (and possibly the area's palettes and VRAM layout).
- Editor views that show hardware details (palette banks, VRAM, OAM) work on the cooked result
  of the selected profile.

## Overrides

Overrides of an asset are kept in `<asset file>.bmasset` next to it (e.g. `tileset.png.bmasset`),
an XML file with a version:

```xml
<asset version="1">
 <override profile="gba">                  <!-- or "*" for every profile -->
  <colour from="#8c2c28" to="#ff2929"/>    <!-- source colour -> colour to cook instead -->
 </override>
</asset>
```

| Override | Meaning |
|---|---|
| `colour` | every pixel of the image with the source colour `from` is cooked as `to` instead. The replacement is a hand-made colour: the profile's colour matrix is not applied to it. An entry for the profile being cooked wins over one for `*` |

Rules:

- The cooker MUST apply the overrides of every image it reads and MUST list them in the change
  report (`colour-override`, per asset).
- An override names source colours, not palette slots, so it survives re-packing palettes.
- The editor writes colour overrides when a colour of a cooked palette is changed: for every
  source colour that became that hardware colour, in every image it came from. It shows which
  rooms share those images before the change takes effect.
- When no override is left, the file is removed.

## Change report

For every cook the cooker MUST produce a report listing what it had to change to fit the
profile, per asset:

| Kind | Example |
|---|---|
| `colour-merged` | two source colours became one hardware colour |
| `palette-split` | a tileset needed more than one palette bank |
| `layer-dropped` | the room has more layers than the profile allows |
| `sprite-over-line-limit` | a frame exceeds the per-line sprite budget |
| `effect-fallback` | a raster effect was replaced by its fallback |
| `sound-reduced` | an instrument was simplified (voices, effect, sample rate) |
| `does-not-fit` | the asset cannot be cooked; the cook fails |
| `colour-override` | colours of the asset were replaced by its overrides for this profile |

Each entry names the asset, the profile, the kind, and a human-readable reason with a
suggestion. The editor shows the report per profile.

## Testing single assets

Every asset MUST be testable on its own, on the host and on the device, through a mini pack
and a test scene; see [streaming.md](streaming.md) §5.

## Test data

Third-party test data MUST NOT be committed. It lives in `testdata/` (ignored by git) or in
the directory named by `BM_TESTDATA_DIR`. Tests that need it MUST be skipped when it is
missing.
