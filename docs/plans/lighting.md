# Plan: lit palettes (normal-mapped light without per-pixel shading)

Status: planned, nothing built.

A light in a room shades the player and the room's tiles by the direction their surfaces
face, the way a normal map does in a 3D engine: the side of the knight's helmet that faces
the torch catches it, the far side stays dark, the bricks of the wall show their edges. The
showcase is a **torch on a wall**: it does not move, and its light flickers.

## Dependencies

| Step | Needs |
|---|---|
| 1 (lights and the palette compositor) | nothing from other plans |
| 2 (lit sprites) | step 1; the palette table in `SPRT` from [layered-sprites.md](layered-sprites.md) step 1 (variants keep their normals) |
| 3 (lit tiles) | step 1 |
| 4 (the torch hall) | steps 1 to 3 |
| 5 (editor) | steps 1 to 3 |
| 6 (scripts) | step 1 |

## 1. Why palettes

The GBA has no per-pixel shading. What it can do cheaply every frame is rewrite palette
entries: 512 colours, written in VBlank. Every other approach fails on this hardware:

| Approach | Why not |
|---|---|
| Per-pixel shading into a bitmap (mode 3/5) | 38 400 pixels × a dot product and a multiply each is several times the 280 000 cycles of a frame; and the room would lose its tiles and scrolling |
| A light sprite blended over the figure | the GBA blends an OBJ only with what is *behind it on a background*, never with another OBJ. And BLDALPHA is one setting for the whole screen, already used by the UI's see-through boxes |
| Windows and BLDY (the existing `spotlight`) | a hard circle that brightens everything equally. It does nothing per surface. It stays as an effect of its own |

So the plan is: **every palette slot that a light touches knows what it stands for**. That
can be a colour with a normal (sprites), or a colour with a baked amount of light (tiles).
Each frame the runtime computes the slot's colour from the lights. The pixels never change,
only the 15 colours of a few banks. This costs under 2 % of a frame (section 6). It
looks the same on the PC, web and GBA without new code there, because the software and
shader PPUs only ever see palettes.

The lighting formula, per slot, channel by channel (the authored colour is the colour in
full light):

```
colour = albedo × min(1, ambient + Σ intensity(t) × light_colour × term)
```

- `ambient` is the room's darkness: 0 is black, 1 is the room as drawn. A lit room uses
  around 0.3 to 0.4.
- `term` differs for sprites and tiles:
  - **Sprites** (they move): `term = max(0, N·L) × falloff(distance)`, worked out every frame.
  - **Tiles** (the light does not move): the cooker bakes `term`, so the runtime only knows
    "how lit is this pixel at intensity 1".
- **Emissive** slots (the flame itself, glowing eyes) ignore the light and stay as drawn.

## 2. Lights

A room object of the built-in class `Light`, placed in Tiled like any other object:

| Property | Type | Default | Meaning |
|---|---|---|---|
| `colour` | colour | `#ffc080` | the light's colour |
| `radius` | int px | 72 | where `falloff` reaches 0 (smooth, `(1 − d²/r²)²`) |
| `height` | int px | 12 | how far the light stands out of the wall toward the viewer (z). With 0 a back wall's flat bricks would get no light at all |
| `intensity` | fix | 1.0 | the brightness without flicker |
| `flicker` | fix 0..1 | 0 | how far the intensity wavers (0.25: between 0.75 and 1.25 of it) |
| `flicker_speed` | int | 6 | frames per noise step: 3 is a nervous candle, 12 a slow brazier |
| `shadows` | bool | true | walls of the collision layer cast baked shadows on tiles (step 3) |
| `ambient` (on the room) | fix | 1.0 | the room's darkness; any `Light` in the room makes the lighting run |

**Flicker** is value noise taken from a hash of `(frame / flicker_speed, light id)`,
smoothed between steps, plus a second octave at a quarter of the amplitude. It is a pure
function of the frame counter, which is already in the state. So the flicker holds **no
state**: a snapshot continues with the same flicker, and the picture tests can stop at a
known frame and get a known brightness. A torch is a `Light` plus a sprite (the bracket and
an animated flame, marked emissive). The test level's class `Torch` attaches both, so the
level designer places one object.

At most 4 lights per room for the first version. Lights are cooked into a `LITE` chunk of
the room: position, colour, radius, height, intensity, flicker values, 16 bytes each.

## 3. Lit sprites

### Normals per slot

A sprite's 15 slots become 15 pairs of **(albedo, normal)**. The cooker gets the normals in
one of three ways:

- `normal="player_n.png"`: a normal map drawn beside the sheet, the usual RGB encoding
  (x right, y down, z toward the viewer).
- `normal="auto"`: derived from the shape. The cooker takes the distance to the silhouette's
  edge, smoothed, as a height ("pillow shading" turned into normals) and adds the drawing's
  luminance as small bumps. This is good enough for small enemies and a first pass on the
  player.
- None: every slot faces the viewer (0, 0, 1). The sprite then only gets brighter and
  darker with distance, without sides.

The cooker clusters the sheet's pixels by (colour, normal) into 15 slots, weighting the
colour more than the normal, and remaps the pixels. A character made of 4 materials (skin,
cloth, steel, hair) with 3 to 4 normal directions each fits. The split figure helps: the
player's legs and torso are two sheets, so they get 30 slots. The cooker reports the worst
error per sheet, the way it reports the sprite memory.

The cooked form is `SPRT` version 5: after the palette table, 15 × 4 bytes. Each slot has a
normal (s8 x, y, z) and flags (bit 0 emissive, bit 1 shiny).

- **Palette variants** (`palette` and `swap`, the chain mail's `look_palette`) change only
  the albedo of a slot and keep its normal. So a red or steel variant is lit the same way.
- **Mirrored** frames negate the light's x before the dot product. That costs nothing.
- **Direction sets and aimed frames** are drawn per frame, so their normals are too.
- **Shiny** slots (steel, wet stone) add a small highlight: `(N·H)^8` with the eye fixed
  toward the viewer. A table of 32 entries holds the power.

### Banks

A lit figure's colours depend on where it stands, so it **needs a bank of its own**. Two
bats of one sheet and palette no longer share one bank once both are lit. The rules:

- The player's banks are always lit (its layers already have their own banks, 0 and 1).
- Other figures inside a light's radius get their own bank, nearest first, while banks 0 to
  12 last.
- Figures that miss out, and every figure outside all radii, share the ambient-lit bank of
  their (sheet, palette) pair. Their colours are albedo × ambient, computed once per room.

The light vector of a figure is taken from the light to the figure's centre (its `centre`
point, else its box). One vector per figure, not per pixel: at 32 pixels high and a radius
of 72 this is close enough. The figure's layers share it.

## 4. Lit tiles

A torch does not move, so the geometry is baked. Per pixel of the room, the cooker computes
`term = max(0, N·L) × falloff × shadow`. The shadow is 0 where a straight line from the
pixel to the light crosses a solid cell of the collision layer. The tileset gets normals the
way sprites do: `normal="bricks_n.png"` on the tileset (a Tiled tileset property), `auto`,
or flat.

Pixels inside a light's radius then cluster into slots of **(albedo, term level)**, with
term quantised to 8 levels, into **lit banks**, at most 4 per room. These are 60 (albedo,
level) pairs, enough for a wall of bricks, mortar and a beam or two. Outside the radius
nothing changes: those tiles keep their banks with albedo × ambient. Where the term reaches
0 at the edge, a lit slot's colour equals the ambient colour, so the edge is seamless at
every intensity.

The costs:

- **Tiles**: inside the radius two tiles of the same drawing get different light, so they
  stop being merged. A radius of 72 px covers at most about 18 × 18 cells. In practice the
  room report lists the lit tiles (the showcase: about 250 tiles, 8 KB, out of 1024 and the
  area's share).
- **Banks**: lit banks come out of the room's 15 background banks. The palette packer
  plans them per room, not per area, because a light is a room's.
- **Overlapping lights**: two flickering lights on one pixel would need slots of (albedo,
  term 1, term 2). The first version gives each pixel to its stronger light. Two lights that
  should add up must share a `flicker` group (an open question below).

The player's shadow on the wall and moving lights on tiles are not covered: both would need
the per-pixel work the hardware cannot do (see section 9).

## 5. Each frame

1. Per light: `intensity(t)` (hash, smooth, two octaves), once.
2. Per lit tile bank: for 15 slots, `albedo × (ambient + I × colour × level)`. The 8 levels
   × 4 lights give a table of 32 RGB factors that is computed once per frame, so each slot
   is three multiplies.
3. Per lit figure: L (light minus centre), falloff from the squared distance, then per slot
   `N·L` (3 multiplies), the optional highlight, and the colour.
4. All of it goes into a shadow palette, which the VBlank copy that already writes the
   palette sends to the hardware. A flash (bank 13) and the UI's bank stay untouched.

Fixed point throughout (s8 normals, u8.8 factors, 5-bit channels with a rounding rule
shared with the host), so the host and the GBA give identical colours and the picture
comparisons hold.

## 6. Cost

| Part | Size |
|---|---|
| CPU | about 4 banks × 15 slots + 2–4 figures × 15 slots ≈ 120 slots × ~40 cycles ≈ 5 000 cycles: under 2 % of a frame, mostly in the multiplies |
| IWRAM | none. The slot tables (albedo, normal, flags: 6 bytes × 15 per bank) live in EWRAM, about 1 KB for a room |
| Pack | `LITE` 16 bytes per light, slot normals 60 bytes per sheet, lit tiles (the showcase: ~8 KB) |
| State | none for the flicker. The intensities a script sets: 2 bytes per light |
| Multiboot | the torch hall goes into the absent list of the multiboot pack, like the boss hall, and is pushed on demand. The engine code adds a few KB to EWRAM, which is checked when step 1 is built |

## 7. The showcase: the torch hall

A new room `torch_hall` behind a fifth door of the effect lobby (next to Wave, Spotlight,
Pulse and Dusk):

- **Walls**: a dark brick tileset with a drawn normal map. `tools/make_test_tiles.py`
  generates both: bricks with bevelled edges, mortar set back, a few cracked bricks that
  catch the light differently. The room's `ambient` is 0.3.
- **One torch** in a bracket, halfway up the back wall: `Torch` (a `Light` with
  `colour=#ffb060`, `radius=80`, `height=14`, `flicker=0.3`, `flicker_speed=5`) and a
  4-frame emissive flame sprite.
- **A pillar** between the torch and the right half of the room. Its baked shadow on the
  floor and wall shows that the shadows are real, and the player walking behind it drops
  out of the light (the figure's light vector is tested against the same collision cells,
  one ray per frame).
- **The player**, with a normal map for legs and torso (drawn on top of `auto` for the
  helmet and the chain mail, which is shiny). The chain mail glints when the player stands
  in front of the torch and goes dark when walking away.
- **A bat** that circles the torch, so a second lit figure with its own bank is seen to
  light up on the near side.

Tests:

- **Host**: the flicker at frames 0, 60 and 61 gives fixed intensities. A lit bank's colours
  match a reference computed in the test.
- **Pictures**: the player left of, under and right of the torch, compared against golden
  images, host and mGBA. `--tolerance 1` as for the blending.
- **Snapshot**: taken mid-flicker, it continues with the same palettes, byte for byte.
- **Cooker**: the bank count and lit tiles are as reported, and slot clustering is stable
  (the same input gives the same output).

## 8. Editor

- **Animation tab**: a sprite's normal map is shown beside it, or the `auto` result, as a
  view toggle (albedo / normals / lit). In the lit view a light follows the mouse, so the
  artist sees the sides. The slot list shows each slot's normal as a small arrow and its
  flags (emissive, shiny), which can be edited.
- **Room view**: lights draw their radius and their baked shadows as an overlay. A slider
  scrubs the flicker, and play mode shows it live through the same compositor (it is
  runtime code compiled for the host).
- **Room report**: lit banks, lit tiles, lit figures that got a bank and those that missed
  out, and the slot error per sheet.

## 9. Steps

1. `Light` objects, the `LITE` chunk, `ambient`, the flicker function, the palette
   compositor with emissive and ambient-only banks. Test: the flicker values and an
   ambient-darkened room against a picture.
2. Lit sprites: `normal=` in `.bmsprite` (image, `auto`, none), clustering into (albedo,
   normal) slots, `SPRT` v5, the light per figure, mirrored frames, shiny, banks for lit
   figures. Test: the player left and right of a light (pictures), a variant lit like the
   default.
3. Lit tiles: tileset normals, the baked term with shadows from collision, (albedo, level)
   banks, the cooker's report. Test: lit tile and bank counts, a picture of a lit wall.
4. The torch hall: tileset and normal maps from the script, `Torch`, the pillar, the
   circling bat, the door from the lobby. Pictures and snapshot on host and mGBA.
5. The editor: views in the Animation tab, overlays and scrubbing in the room view, the
   report.
6. Scripts: `light_set(#id, intensity)`, `light_intensity(#id)`, `light_flicker(#id, a)`
   (a torch blown out by a gust, a lamp switched on). These are the only lighting state in
   snapshots.

Done when: in the torch hall the flame flickers and the wall and the player flicker with
it. The near side of the helmet and coat is lit and the far side dark, and both swap when
the player walks past the torch. The pillar's shadow lies on the wall, and the player
behind it is dark. The same frames look the same in the browser, on the host and in mGBA.
A snapshot taken mid-flicker continues identically.

## 10. Open questions

- **Overlapping lights**: whether "stronger light wins" looks acceptable or whether a few
  slots of (albedo, term 1, term 2) at coarse levels are needed. Decide with a second torch
  in the hall.
- **Moving lights** (a lantern the player carries, a fireball): sprites work as they do,
  since their light is per frame anyway. Tiles do not: either their banks are relit per
  frame with one light vector per bank (cheap, but wrong across a large bank), or moving
  lights light sprites only and use the existing `spotlight` window for the room. The
  second is planned for when it is needed.
- **The figure's own shadow** on the wall is not possible with palettes. A darkened
  sprite copy with OBJ semi-transparency (which blends with the background behind it) could
  fake it. That takes a hardware sprite and a bank more, so it is not in the first version.
- **Clustering quality**: whether 15 slots per sheet are enough for the player's normal map
  without visible banding. The torso and legs split gives 30, and if that is not enough a
  third layer (head) gives 45.
