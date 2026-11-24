# Plan: a light layer and lit palettes (normal-mapped light on the GBA)

Status: six ways of lighting the tiles built side by side (four, then two streamed), and lit sprites
([../spec/room-format.md](../spec/room-format.md) §1 "Light", §7 `LITE`;
[../spec/sprite-format.md](../spec/sprite-format.md) version 5;
[../spec/runtime-core.md](../spec/runtime-core.md) §12 "The light"). A room chooses with its
property `lighting`: `palette` (the light baked into the room's palettes, the first version of
this plan) or `layer` (the light layer of section 3), whose values are, by `light_index`,
`shared` (the default), `fitted` or `normals` (section 3 as planned). Only rooms with a
`Light` object are lit; every other room is as before. The test level's debug lobby has a
fifth door into the torch halls, one behind the other: "palettes", "light layer" (shared),
"fitted", "normals", and in the PC, browser and pushed cartridge packs "streamed" and "hybrid"
(`tools/make_torch_hall.py`; `test_game` writes all six side by side into
`test-output/torch_halls.ppm`).

**The light layer's values.** The flicker only scales the light, so a pixel needs one number,
its light at full intensity (`max(0, N·L) × falloff`, with its shadow). `fitted` stores that
per pixel (direction, distance and shadow merged): smooth, but a tile for nearly every lit
cell. `shared` quantizes it so that tiles repeat: the light is split into the cell's strength
(how lit a flat surface is there, in the palette bank) and the pixel's pattern (how lit it is
against a flat surface, from its normal and the light's slope across the wall, one of 8 slopes
for the room, fitted to the cells by the error they cause), the patterns on one ladder of 15
steps fitted to all the room's pixels. A brick under the same slope is the same tile at any
distance. The distance is quantized per cell (one strength for its 8x8 pixels, one of 14):
far from the light that hardly shows, but near it the light changes by half across two
cells, so there (where it changes by more than `light_tolerance` across a cell: 72 cells,
about 40 px around the torch) a cell has a tile of its own, each pixel its own light on the
same ladder. Cells on a shadow's edge have their own tiles too. Mean error against each
pixel's true light: shared 0.021, fitted 0.0066 (of full light).

Compared in the test level (radius 160):

| | palettes | light layer, shared | light layer, fitted | light layer, normals |
|---|---|---|---|---|
| look | the light multiplies each colour; smooth | added; smooth, the bevels and the falloff far from the light a little coarser | added; smooth | added; steps from region to region |
| tiles | 676 (the room's own, lit) | 19 room + 238 light (72 without shadows and the light's surroundings) | 19 + 644 | 19 + 40 |
| banks | 8 | 1 + 14 | 1 + 14 | 1 + 14 |
| layers | the room's 3 | 2 + the light layer | 2 + the light layer | 2 + the light layer |
| tile memory (32 KB: char blocks 0-1, 1024 tiles) | 676 tiles, 21,632 B (66 %) | 257 tiles, 8,224 B (25 %) | 663 tiles, 21,216 B (65 %) | 59 tiles, 1,888 B (6 %) |
| map memory (12 KB: screen blocks 26-31, 4 KB a layer) | 2 layers, 8 KB (67 %) | 3 layers, 12 KB (100 %) | 12 KB (100 %) | 12 KB (100 %) |
| the room's video memory (44 KB) | 29,824 B (68 %) | 20,512 B (47 %) | 33,504 B (76 %) | 14,176 B (32 %) |
| background palette (15 banks; the 16th is the UI's) | 8 (53 %) | 15 (100 %) | 15 (100 %) | 15 (100 %) |

The same hall unlit takes 19 tiles (608 B) and 2 layers' maps (8 KB): 8,800 B, 20 % of the
room's video memory, and 1 bank. The UI's tiles (char block 2) and map (screen block 25) and
sprite memory (32 KB) are the same in every hall: the sprites' normals only regroup their
colours, the player's and the bat's sheets keep their tiles.

**The streamed light layer** (`light_index = stream`, the fifth torch hall; PC, browser and,
pushed over the link, the cartridge game, not multiboot): the tiles hold one of 7 normals
(72 tiles, a brick one tile wherever it is), and the light is streamed into the light layer's
banks line by line by H-blank DMA from a table in the pack, a row per room pixel row: exact
from line to line, by bands of 8 to 40 pixels across (narrower near the torch). The flicker is
the light layer's blend weight, so the UI is not see-through there. Measured in mGBA against
the other light layers: 19 room + 72 light tiles (15,200 B of video memory, 34 %), 1 + 7
banks, 43 KB of rows in the pack, mean error 0.025 (normals 0.061, shared 0.020). The light's
code drops from 32,400 to 3,000 cycles a frame, but the DMA stalls the CPU for about 48,000
(112 colours a line, about 3 cycles each from ROM): a game frame takes 172 of its 228 lines
against 157 with the other light layers (peak lines; the mean of 300 frames in the same build
as the hybrid below: 176 against 133).

**The hybrid** (`light_index = hybrid`, the sixth torch hall, same platforms): the light split
into across (X) and down (Y). Only the 2 banks (4 bands) around the torch are streamed, where
the light changes fastest both ways; the other 5 banks hold fixed colours for the light's own
row, and the light layer's blend weight follows the room row line by line (BLDALPHA from a
line table, H-blank DMA channel 0) with the fixed bands' vertical falloff times the flicker.
The streamed rows are divided by that weight, so near the torch it is as exact as `stream`.
Same tiles and banks as `stream` (19 + 72, 1 + 7), 12 KB of rows instead of 43 KB, 32
colours a line instead of 112, mean error 0.032 (stream 0.025, normals 0.061): away from the
torch a band is one colour per line for its whole width, so the band edges show a little
more. Measured in mGBA, mean lines of a game frame over 300 frames: normals 133, hybrid 152,
stream 176. The weights are built again only when the camera's row or the flicker changes,
with one division a frame (160 of them cost 60,000 cycles). EVA has only 17 steps, which at
first showed as horizontal bands 7 to 9 lines tall in the fixed bands (a sixteenth of the
light each); the rounding error is now carried down the lines (error diffusion), so lines
alternate between neighbouring steps: about 3,600 cycles and 2 lines more a frame.

**Ramps across the bands** (`light_bands = ramp`, both streamed halls now): with flat bands the
light can only change where one bank's cells give way to the next (the palette changes per line,
not along it), so it steps across the screen every 8 to 40 pixels, by up to 32 of 255 next to the
torch. The values a bank holds need not be one band each: with a ramp a bank is one band, and
its 15 values are samples of the light across it, per normal (the flat wall, most of the pixels,
7 samples; the bevels their band's two edges). The tiles say where a pixel is between two
samples (an ordered dither between them), the stream gives each sample its light per line. The
edges are shared with the neighbouring banks, so a bank's edge does not show. On the torch's
row the worst step against the exact light falls from 32 to 10 of 255 and the mean difference
from 6.5 to 4.1; mean error (seen from afar) 0.023 against 0.025 for the stream, 0.029 against
0.032 for the hybrid. The hybrid's fixed colours and per-line weights are now fitted together
(least squares over the pixels that use them), and its streamed bands meet the fixed ones at
their fitted value, so there is no seam. The cost is tiles: 205 light tiles instead of 72, as a
cell's dither depends on where in its band it is (19,456 B of the room's video memory, 44 %).
The same time a frame (152 and 176 lines). `middle` (flat, as before) and `mean` (a band's mean
light) stay; 5 normals with 20 flat bands is the cheap middle way (85 tiles, smaller steps).

**Two torches** (measured in two-screen halls, torches 240 px apart, pillar between them; the
comparison page has the pictures): up to 4 lights per room, flickering together, so every
mode still bakes one sum. Palettes grows from 750 to 908 tiles (89 % of the 1024), shared from
145 to 257 light tiles (39 % to 47 % of the room's video memory), fitted to 887 light tiles
(92 %), normals stays at 30. Sprites cost about 11,500 cycles a frame per lit bank, with one
light or two. Lights that flickered apart would need (colour, level, level) entries in mode
`palette` (1,482 tiles: does not fit), (left, right) pairs per bank in the light layer's
fitted and shared values (coarse where both reach), and only two vectors per region in mode
`normals`. Not built: the lights of a room flicker together.

Where it differs from the plan below:

- **Regions** of the normals' light layer (and the banks of the others): at most 14 (not 8), clustered by the light's
  strength first and its direction second, so they lie in rings. A 2x2 dither between
  neighbouring regions was tried and looked worse (a checkerboard of 8 px cells).
- **The UI** over a room with a light layer is blended at EVB 16 instead of 8 (one pair of
  weights for both), not through the line table: its boxes are lighter there.
- **Figure palettes and a bank per piece** (section 4) are not built: a sprite's normals split
  its own 15 slots (each colour into a few directions while slots are left), the player's legs
  and torso keep a bank each, and figures sharing a sheet and palette share a bank and the
  light of the one nearest to it. The player's and the bat's normals are `auto`, not drawn.
- **The editor** shows lit rooms unlit in the Room tab (its play mode runs the light); the
  views of section 8 are not built. **Scripts** (section 9, step 5) are not built.
- **Multiboot** images leave the light out (`src/hal/gba/light_off.c`): their packs have no lit
  room.
- **Cost** on the GBA, measured in mGBA: about 26,000 to 32,000 cycles a frame for the room's
  banks and 11,000 for the sprites' (13 % of a frame), from ROM; IWRAM has no room for the
  kernels in every image. All halls run at 60 frames a second (`light_mgba_*` tests keep the
  budget).
- A new `Light` property `gain` (the light layer's strength: it adds the light, while the baked
  mode multiplies it).

## Dependencies

| Step | Needs |
|---|---|
| 1 (the light, the light layer) | the line table (built, runtime-core.md §12) |
| 2 (lit sprites, banks per piece) | step 1; the palette table in `SPRT` from [layered-sprites.md](layered-sprites.md) step 1 (variants keep their normals) and figures from its step 3 |
| 3 (the torch hall) | steps 1 and 2 |
| 4 (editor) | steps 1 and 2 |
| 5 (scripts, a moving light) | step 1 |

## 1. The hardware and what does not work

The GBA has no per-pixel shading. It can rewrite palette entries cheaply every frame (512
colours, written in VBlank), and it can blend two layers per pixel: a first target (the top
one) and a second target (the first visible pixel below it), as `A × EVA/16 + B × EVB/16`,
saturating at 31. Everything below is built from those two.

| Approach | Why not |
|---|---|
| Per-pixel shading into a bitmap (mode 3/5) | 38 400 pixels × a dot product and a multiply each is several times the 280 000 cycles of a frame; and the room would lose its tiles and scrolling |
| Two light tiles per tile (one for left/right, one for up/down), blended | the hardware blends **two** layers per pixel, not three: the base and one light layer. The two sides are added in the palette instead (section 3) |
| The light baked into the room's own palettes | works, but the same brick at another place gets other light, so tiles stop being merged (about 250 more tiles for one torch). The light layer keeps the room's tiles as they are |
| A light sprite blended over the figure | a sprite blends only with the BG below it, never with another sprite |
| Windows and BLDY (the existing `spotlight`) | a hard circle that brightens everything equally. It does nothing per surface. It stays as an effect of its own |

## 2. The light

A room object of the built-in class `Light`, placed in Tiled like any other object. There is
at most one per room (a second one is a cooker error):

| Property | Type | Default | Meaning |
|---|---|---|---|
| `colour` | colour | `#ffc080` | the light's colour |
| `radius` | int px | 72 | where `falloff` reaches 0 (smooth, `(1 − d²/r²)²`) |
| `height` | int px | 12 | how far the light stands out of the wall toward the viewer (z). With 0 a back wall's flat bricks would get no light at all |
| `intensity` | fix | 1.0 | the brightness without flicker |
| `flicker` | fix 0..1 | 0 | how far the intensity wavers (0.25: between 0.75 and 1.25 of it) |
| `flicker_speed` | int | 6 | frames per noise step: 3 is a nervous candle, 12 a slow brazier |
| `shadows` | bool | true | walls of the collision layer cast shadows (section 3) |
| `ambient` (on the room) | fix | 1.0 | the room's darkness: 0 black, 1 as drawn. A lit room uses about 0.3 |

**Flicker** is value noise taken from a hash of `frame / flicker_speed`, smoothed between
steps, plus a second octave at a quarter of the amplitude. It is a pure function of the
frame counter, which is already in the state. So the flicker holds **no state**: a
snapshot continues with the same flicker, and the picture tests can stop at a known frame
and get a known brightness. A torch is a `Light` plus a sprite (the bracket and an animated
flame, marked emissive). The test level's class `Torch` attaches both, so the level
designer places one object.

**Ambient**: once per room, the room's BG and sprite palettes are scaled by `ambient`, so
the room is dark and the light is added on top of it.

Cooked into a `LITE` chunk of the room: position, colour, radius, height, intensity and
flicker values, 16 bytes.

## 3. Tiles: the light layer

### Normal tiles

Every tileset that should take light gets a normal map beside it: `normal="bricks_n.png"`
(a Tiled tileset property, the usual RGB encoding: x right, y down, z toward the viewer),
or `normal="auto"` (a height taken from the drawing's brightness and edges, turned into
normals), or nothing (flat, facing the viewer).

The cooker quantises the normals to a **set of 15** per room (the 15 that cover the lit
tiles best: flat, the four sides, the four diagonals and tilts between, as the drawing needs
them). For every tile of the main layer it makes a **normal tile**: a 4bpp tile whose pixel
values are indices into that set, 0 where the tile is empty. Normal tiles are merged like
any tiles, because a brick's normal tile is the same wherever the brick is. The showcase's
brick wall needs about as many normal tiles as it has brick tiles: a few dozen, not
hundreds.

The normal tiles form a **light layer**: a BG with the main layer's map, but pointing at the
normal tiles. Only cells inside the light's radius are filled; everything else is empty, so
the layer costs nothing outside the light.

### Blending

- The light layer is a BG of priority 1 with a lower BG number than the main layer, so it
  lies over the main layer and under the sprites (sprites of priority 1 are in front of
  every BG of priority 1).
- BLDCNT: alpha blending, the light layer as first target, the main and background layers
  and the backdrop as second. EVA 16, EVB 16: the light is **added** to the darkened room.
- So each pixel of the main layer gets `albedo × ambient + light(normal)`. The light is the
  same for every colour, so lit stone tints toward the torch's colour as it would. Very
  dark and very bright materials take it the same way, which is a simplification of
  multiplying by the albedo.
- **The UI**: its see-through boxes blend BG3 with EVA 16 / EVB 8. While a box is shown,
  the lines of the box take the UI's BLDCNT and BLDALPHA through the line table, and the
  other lines keep the light. A room effect that blends (spotlight, pulse, gradient) and the
  light do not go together; the cooker reports a room that has both.
- **Layers**: the light layer is one of the room's three BGs, so a lit room has at most two
  graphics layers of its own (the cooker reports a third). The showcase uses background and
  main.

### Two sides at once: the palette

A light-layer pixel holds a normal index, and its colour is that normal's light:

```
light(n) = intensity(t) × colour × falloff × max(0, nx·Lx + ny·Ly + nz·Lz)
```

That sum is where the two sides meet. Light from the top right has both `Lx` and `Ly`, so a
brick's right bevel (nx), its top bevel (ny) and its top-right corner (both) are lit at
once, each by how much it faces that way. Two index tiles per tile, one per axis, would mean
the same thing, but the hardware can add only one layer to the base, so the cooker folds
both axes into the 15 normals and the runtime adds them per palette entry.

### Position: banks by region

The light's direction and distance change across the room, and a palette bank has one
colour per normal. So the light layer uses **several banks, one per region**:

- The cooker cuts the light's radius into regions of tiles, by direction from the light
  (8 sectors) and distance (rings of 16 px), merging regions while the error stays small: 6
  banks for the showcase, at most 8.
- Each cell of the light layer's map names its region's bank (the map's bank bits). The
  normal tile stays the same; only the bank number differs.
- Each frame every region bank is computed with the region's L (from the light to the
  region's centre) and its falloff: 15 entries × 3 multiplies.
- Within a region, light steps at its edges. To hide the steps, the cooker dithers the edge
  pixels of a region's cells between the two banks' rings: a cell on a ring edge gets a
  normal tile of its own whose pixels use the neighbour ring's normals. These are the only
  tiles that depend on the position (a few dozen).

### Shadows

A cell that lies in the shadow of a solid collision cell (a straight line from it to the
light crosses one) is left empty in the light layer: no light. On a shadow's edge, the
cell gets a normal tile with the shadowed pixels empty, so the shadow is sharp to the
pixel. Like the ring edges, these tiles are unique.

### Costs

- **Tiles**: the normal tiles (merged) plus the edge tiles (ring and shadow edges). The
  showcase: about 80 tiles, 2.5 KB.
- **BG banks**: the region banks, 6 to 8 of the room's 15.
- **A BG**: the light layer.
- **Per frame**: 8 banks × 15 entries, and the light layer is never rewritten.
- **A moving light** later (section 9): the region banks are recomputed anyway; only the
  map's bank bits in the light's radius (a few hundred cells) would be rewritten when the
  light moves. The baked shadows would not move.

## 4. Sprites: lit palettes

### Normals per slot

A sprite's colour slot becomes a pair of **(albedo, normal)**. The cooker gets the normals
from a normal map beside the sheet (`normal="player_n.png"`), from `normal="auto"` (the
distance to the silhouette's edge as height, plus the drawing's brightness as small bumps:
good for small enemies and a first pass on the player), or none (flat: the sprite only gets
brighter and darker with distance).

The cooker clusters the pixels by (colour, normal), weighting the colour more than the
normal, and remaps them. Nearby normals of one colour merge into one slot; the diagonals
stay their own. The cooker reports the worst error, the way it reports the sprite memory.

The slot's colour each frame is

```
albedo × min(1, ambient + intensity(t) × colour × falloff × max(0, N·L))
```

Here the light is multiplied by the albedo, so sprites are lit more exactly than the wall.
The sum `nx·Lx + ny·Ly + nz·Lz` is the same as on the light layer, so a slot facing up and
right is lit from both sides at once. Each slot also has flags: **emissive** (the flame,
glowing eyes: stays as drawn) and **shiny** (steel: adds a small highlight `(N·H)^8` with
the eye fixed toward the viewer, from a table of 32 entries).

- **Palette variants** (`palette` and `swap`, the chain mail's `look_palette`) change only
  the albedo of a slot and keep its normal. So a red or steel variant is lit the same way.
- **Mirrored** frames negate the light's x before the dot product. That costs nothing.
- **Direction sets and aimed frames** are drawn per frame, so their normals are too.

### Banks per piece, shared by the figure

On the hardware every sprite piece (one OAM entry, 8×8 up to 64×64) names its **own
palette bank**. Today the runtime takes the bank from the sheet, so the legs use one bank
and the torso another, and each sheet has its own 15 colours. This changes to a **figure
palette**:

- All layers of a figure share one light vector, so their slots can share banks. The
  cooker clusters the slots of the **whole figure** (every layer's sheets, every frame)
  together, so legs and torso reuse each other's (albedo, normal) pairs. The boots' leather
  and the belt's leather become one slot, whichever sheet they are in.
- The figure gets **1 to 3 banks**: as few as fit the error limit (`banks="auto"`, or a
  number in the `.bmfigure`). Each **piece** goes into the bank that holds its colours. A
  piece is stored with a bank index (0, 1, 2 within the figure's set). So a frame of the
  torso may draw its head from bank 0 and its chain mail from bank 1, and the legs may use
  bank 0 alone.
- So the split no longer decides the palette. A figure with one simple palette takes one
  bank even with three layers, and frees banks for the room. A figure with a detailed normal
  map spreads its slots over up to 45 colours however its layers are cut. The only rule is
  that the pixels of one piece share a bank. The cooker cuts pieces with that in mind and
  reports a piece that needed splitting.
- Unlit rooms gain the same: with one figure palette the player's legs and torso use one
  bank instead of two, and 12 are left for the room.

Cooked: `SPRT` version 5 stores a bank index per piece (2 bits in the piece's attributes),
and per bank 15 × 4 bytes of normal (s8 x, y, z) and flags after the palette table. A
`.bmfigure` with lighting cooks its banks into `FIGR` (palettes and normals), and the
layers' sheets point into them. A sprite that is not in a figure is a figure of one layer.

### Banks of other figures

A lit figure's colours depend on where it stands, so it needs banks of its own. Two bats of
one sheet and palette no longer share a bank once both are in the light:

- The player's banks are always lit.
- Other figures inside the light's radius get their own banks, nearest first, while banks
  0 to 12 last.
- Figures that miss out, and every figure outside the radius, share the ambient-lit banks of
  their (figure, palette) pair, computed once per room.

The light vector of a figure is taken from the light to the figure's centre (its `centre`
point, else its box). One vector per figure, not per pixel: at 32 pixels high and a radius
of 72 this is close enough. A ray from the centre to the light through the collision cells
(once per frame) puts a figure behind a pillar into the shadow.

## 5. Each frame

1. `intensity(t)` (hash, smooth, two octaves), once.
2. Per region bank of the light layer: L and falloff of the region (cooked), then for 15
   normals `colour × I × falloff × max(0, N·L)`.
3. Per lit figure: L (light minus centre), falloff from the squared distance, the shadow
   ray, then per slot of each of its banks `N·L`, the optional highlight, and the colour.
4. All of it goes into a shadow palette, which the VBlank copy that already writes the
   palette sends to the hardware. A flash (bank 13) and the UI's bank stay untouched.

Fixed point throughout (s8 normals, u8.8 factors, 5-bit channels with a rounding rule
shared with the host), so the host and the GBA give identical colours and the picture
comparisons hold. The PC and web PPUs blend as the GBA does already, so no new code is
needed there.

## 6. Cost

| Part | Size |
|---|---|
| CPU | 8 region banks + 2–3 figures × 1–3 banks ≈ 15 banks × 15 slots × ~30 cycles ≈ 7 000 cycles: under 3 % of a frame |
| IWRAM | none. Region and slot tables (about 1 KB) live in EWRAM |
| VRAM | the normal tiles (~80 for the showcase), one screen block for the light layer |
| Pack | `LITE` 16 bytes, the region table 8 bytes per region, normal tiles, normals 60 bytes per sprite bank |
| State | none for the flicker. The intensity a script sets: 2 bytes |
| Multiboot | the torch hall goes into the absent list of the multiboot pack, like the boss hall, and is pushed on demand. The engine code adds a few KB to EWRAM, which is checked when step 1 is built |

## 7. The showcase: the torch hall

A new room `torch_hall` behind a fifth door of the effect lobby (next to Wave, Spotlight,
Pulse and Dusk), with a background and a main layer:

- **Walls**: a dark brick tileset with a drawn normal map. `tools/make_test_tiles.py`
  generates both: bricks with bevelled edges on all four sides and rounded corners (so the
  diagonals show), mortar set back, a few cracked bricks that catch the light differently.
  The room's `ambient` is 0.3.
- **The torch** in a bracket, halfway up the back wall: `Torch` (a `Light` with
  `colour=#ffb060`, `radius=80`, `height=14`, `flicker=0.3`, `flicker_speed=5`) and a
  4-frame emissive flame sprite. Bricks above it are lit on their bottom bevels, bricks to
  its right on their left bevels, and bricks up and to the right on both.
- **A pillar** between the torch and the right half of the room. Its shadow on the floor and
  wall is sharp to the pixel, and the player walking behind it drops out of the light.
- **The player**, with a normal map for legs and torso (drawn on top of `auto` for the
  helmet and the chain mail, which is shiny), as one figure palette. The helmet's rim lights
  from the top right while the player stands left of and below the torch. The chain mail
  glints when the player stands in front of the torch and goes dark when walking away.
- **A bat** that circles the torch, so a second lit figure with its own bank is seen to
  light up on the near side, through all directions of its circle.

Tests:

- **Host**: the flicker at frames 0, 60 and 61 gives fixed intensities. A region bank's
  colours match a reference computed in the test. A normal facing top-right is brightest
  with the light at the top right, and less bright with it straight above or to the right.
- **Pictures**: the wall at two flicker frames, and the player left of, under and right of
  the torch, compared against golden images, host and mGBA. `--tolerance 1` as for the
  blending. One more with a dialogue box shown (the UI's lines blended as the UI, the rest
  lit).
- **Cooker**: the normal tiles are merged (a repeated brick has one), and the region and
  edge tile counts are as reported. The player's figure palette fits its banks, legs and
  torso share slots, and every piece uses one bank. Unlit, the player takes one bank where
  it took two. The clustering is stable (the same input gives the same output).
- **Snapshot**: taken mid-flicker, it continues with the same palettes, byte for byte.

## 8. Editor

- **Room view**: the light layer as an overlay toggle (normals as colours, or the lit
  result). The light draws its radius, its regions and its shadows. A slider scrubs the
  flicker, and play mode shows it live through the same compositor (it is runtime code
  compiled for the host).
- **Animation tab**: a sprite's normal map beside it, or the `auto` result, as a view
  toggle (albedo / normals / lit). In the lit view the light follows the mouse, so the
  artist sees the sides. For a figure the view shows its banks: which slot is in which bank,
  each slot's normal as a small arrow and its flags (emissive, shiny), which can be edited,
  and which bank each piece of the frame uses.
- **Graphics tab**: a tileset's normal map beside it.
- **Room report**: the light layer's tiles (merged and edge), its banks, lit figures that
  got banks and those that missed out, and the slot error per figure.

## 9. Steps

1. `Light` objects, the `LITE` chunk, `ambient`, the flicker function. The light layer:
   tileset normals (image, `auto`), the room's 15 normals, normal tiles, regions and their
   banks, ring and shadow edges, the BG and its blending, the UI's lines through the line
   table. Test: flicker values, normal tile merging, a picture of a lit wall.
2. Lit sprites: `normal=` in `.bmsprite`, figure palettes clustered over all layers into 1
   to 3 banks, a bank per piece (`SPRT` v5, `FIGR`), the light per figure with its shadow
   ray, mirrored frames, shiny, banks for lit figures. Test: the player left and right of
   the light (pictures), a top-right slot, a variant lit like the default, one bank for the
   unlit player.
3. The torch hall: tileset and normal maps from the script, `Torch`, the pillar, the
   circling bat, the door from the lobby. Pictures and snapshot on host and mGBA.
4. The editor: overlays and scrubbing in the room view, views in the Animation and Graphics
   tabs, the report.
5. Scripts: `light_set(intensity)`, `light_intensity()`, `light_flicker(a)` (the torch blown
   out by a gust, a lamp switched on). These are the only lighting state in snapshots.

Done when: in the torch hall the flame flickers and the wall and the player flicker with it.
Every brick around the torch is lit on the sides that face it, both sides at once on the
diagonals. The near side of the helmet and coat is lit and the far side dark; the shading
moves smoothly through top-right to top to top-left as the player walks under the torch, and
the sides swap when the player walks past it. The pillar's shadow lies on the wall, and the
player behind it is dark. A dialogue box stays see-through. The same frames look the same in
the browser, on the host and in mGBA. A snapshot taken mid-flicker continues identically.

## 10. Open questions

- **Additive light on the wall**: the light layer adds the same light to every colour, so
  dark mortar and bright stone gain the same amount. If that looks flat, the normal index
  can carry a material class too (for example 5 normals × 3 brightness classes in the 15
  values) and the palette scales by the class. That costs normal detail, so it waits for
  the showcase.
- **Region steps**: whether 16 px rings with dithered edges hide the steps of the falloff,
  or whether more banks (smaller regions) are needed. The room report shows the banks left.
- **A moving light** (a lantern the player carries): sprites work as they do, and the light
  layer can follow by rewriting its map's bank bits in the radius, without shadows. Planned
  when it is needed.
- **The figure's own shadow** on the wall is not possible with this. A darkened sprite copy
  with OBJ semi-transparency (which blends with the background behind it) could fake it.
  That takes a hardware sprite and a bank more, so it is not in the first version.
- **Clustering quality**: whether 45 slots in 3 banks are enough for the player's normal map
  without visible banding, and whether "one bank per piece" costs many extra pieces. The
  figure view's budget shows both.
