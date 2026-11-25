# Room format

Version 0.2, draft.

**What is built.** Sections 1 to 6 describe the source form and the design, and not all of it
exists. What the runtime does with a room, exactly, is in
[runtime-core.md](runtime-core.md) (collision §4, the player's body §5, doors and respawn §6,
the camera §7); where that file and this one differ, that file is right. Section 7 (the
cooked form) is exact. Differences to keep in mind:

- The collision cell is the map's tile; the property `collision_cell` is not read.
- Custom shapes (§3.6, shape 14, `SHAP`) are not built: shape 14 is empty.
- `half_top` (shape 2) is solid in its upper half for walls and ceilings but is no floor.
- Materials with behaviour: water, damage, death, ladder. Conveyor, ice, bounce and sticky
  are stored and do nothing yet.
- Layer properties `priority` and `effect` are not read: background is priority 2, main 1,
  foreground 0. A room has at most 3 graphics layers.
- §4.5 (entity against entity) and §4.6 are not built. §4.1 to §4.4 are the design;
  runtime-core.md §5 has the rules and numbers as built.
- Engine classes with behaviour: `PlayerStart`, `Door`, `CameraZone`, `KillZone`,
  `SavePoint`. `Trigger` is recognised and does nothing; the others in §5 do not exist. Any
  other class name is a script class ([mondskript.md](mondskript.md)); a name no script
  defines is ignored.
- Of §6 the cooker reports as errors: diagonal flips, more than one `PlayerStart`, more than
  3 graphics layers, what does not fit the hardware. A layer without role and a door whose
  target room is not in the cook are warnings. The rest is not checked.

A room is one screen-sized or larger area of the game world: tile layers, a collision grid,
physics settings and objects. The source form is a Tiled map (`.tmx`, Tiled 1.8 or newer) or
the editor's room file with the same content. Everything in the source form is plain Tiled, so
rooms can be edited without the Blutmond editor. The cooked form is described at the end and
in [pack-format.md](pack-format.md).

## 1. Room properties

Map-level custom properties:

| Property | Type | Default | Meaning |
|---|---|---|---|
| `room_id` | string | file name | unique ID within the project; doors refer to it |
| `gravity` | float | 0.25 | px/frame² |
| `max_fall_speed` | float | 4.0 | px/frame |
| `water_gravity` | float | 0.08 | px/frame² inside water |
| `water_max_fall_speed` | float | 1.5 | px/frame inside water |
| `edges_solid` | bool | true | room edges without a `Door` behave as walls (left, right, top); the bottom edge is open |
| `collision_cell` | int | 16 | collision cell size in px: 16 or 8 |
| `music` | string | – | song started on entering the room (unless already playing) |
| `area` | string | `default` | area the room belongs to; rooms of an area share graphics, palettes and VRAM layout ([pack-format.md](pack-format.md) §2) |
| `attach` | string | – | the room's own scripts: `Attachment` classes, separated by commas, attached to the room in this order when it is entered and detached when it is left ([mondskript.md](mondskript.md) §11, "Attachments") |
| `Class.variable` | any | – | for a class named in `attach`: the value of its `var(Category)` variable of that name |
| `effect` | string | `none` | the screen effect while in the room: `wave`, `spotlight`, `pulse`, `gradient` ([runtime-core.md](runtime-core.md) §12) |
| `effect_a`, `effect_b` | int | 0 | the effect's two values (0: its default) |
| `title` | string | – | shown as a notice at the bottom left when the room is entered |
| `acoustics` | string | – | what the room does to the sound: `dry`, `hall`, `cave`, `water` (below) |
| `echo` | int | preset's | ms until the echo comes back, at most 225 (0: none) |
| `echo_feedback` | int | preset's | how loud each repeat comes back: 0-255 (256 would be as loud) |
| `echo_damping` | int | preset's | how much treble each repeat loses: 0 none - 255 most |
| `muffle` | int | preset's | a low pass over everything heard: 0 none - 255 most |
| `lighting` | string | `palette` | how the room's `Light` object lights its tiles: `palette` (baked into the room's colours) or `layer` (a light layer added over the room) (below) |
| `ambient` | float | 1.0 | a lit room's darkness: 0 black to 1 as drawn |
| `light_levels` | int | 8 | with `lighting = palette`: levels of light, 2 to 16 (each lit colour takes a palette entry per level it is used at: more than 9 hardly fit) |
| `light_curve` | string | `linear` | with `lighting = palette`: the levels' light in even steps (`linear`) or in even steps of how bright a surface looks, the square root of ambient + light (`perceived`): finer in the dark, where a step shows most |
| `light_dither` | bool | false | with `lighting = palette`: a pixel takes the level below or above its light by an ordered 4x4 dither (of its room position) instead of the nearest, so the steps between levels blend; lit tiles repeat less (the test level's palette hall: 850 tiles instead of 676) |
| `light_index` | string | `shared` | with `lighting = layer`, what the light layer's values stand for: `shared`, `fitted` (levels of light), `normals` (directions), `stream` (directions, the light streamed line by line) or `hybrid` (as `stream`, only near the lights) (below; the last three are lit only by a runtime built with `BM_LIGHT_EXPERIMENTAL`) |
| `light_normals` | int | 7 | with `light_index = stream` or `hybrid`: 15, or 3 to 7 directions (flat bands: 15 / normals bands a bank) |
| `light_bands` | string | `middle` | with `light_index = stream` or `hybrid`: a band's colours are the light at its `middle` column, its `mean` across the band, or a `ramp` across it (one band a bank, samples per normal; needs 3 to 7 normals) |
| `light_band` | int | 24 | with `light_index = stream` or `hybrid`: the bands' mean width in pixels (widened when the banks would not do) |
| `light_stream_banks` | int | 2 | with `light_index = hybrid`: how many banks (two bands each) are streamed, around the first light |
| `light_directions` | int | 8 | with `light_index = shared`: how many light directions its tiles are made for |
| `light_tolerance` | float | 0.1 | with `light_index = shared`: a cell across which a flat surface's light changes by more than this (of full light) gets a tile of its own (near the light) |

**Acoustics.** While the player is in a room with any of the last five properties, the
room's echo is heard instead of the one the playing song brings (sound-format.md), over the
music and the sound effects alike; a room without them hears the song's echo again. The
presets (echo, feedback, damping, muffle): `dry` 0, 0, 0, 0 (no echo at all, not even the
song's); `hall` 120, 150, 60, 0; `cave` 200, 120, 150, 0 (a far, dull echo); `water` 70, 70,
200, 200 (everything muffled). The numbers change a preset's values or, without `acoustics`,
make the room's own (the rest 0). On the GBA damping or muffle cost about a twentieth of a
frame (`echo` alone costs what a song's echo does).

**Light** ([../plans/lighting.md](../plans/lighting.md)). A room with a `Light` object (§5) is
lit: its tiles, and the sprites in it, are shaded by the direction their surfaces face, from
normal maps. A tileset gets its normals from its own property `normal`: an image beside it of
the tileset's size (RGB: x right, y down, z toward the viewer, as usual for normal maps), or
`auto` (made from the drawing: a bevel at the edges of each tile and its brightness); without
one its tiles face the viewer. Black in a normal map (0, 0, 0, alpha not 0) marks a pixel that
gives light itself, a lamp's glass, a lit window, the night sky: in mode `palette` it is drawn as
it is, not shaded (one level past the last in `LITE`). A lit room is an area of its own (its `area` is not read), and
only layers without parallax are lit. The two modes:

- `palette`: per pixel the cooker works out how much light reaches it at full intensity
  (`max(0, N·L) × falloff`, L toward the light raised by its `height`, 0 where a straight line
  to the light crosses a solid collision cell other than the pixel's own), in 8 levels, and
  every (colour, level) pair becomes a palette entry; tiles are merged on those pairs. Each
  frame an entry is `colour × min(1, ambient + intensity × light colour × level / 7)`. A tile
  with more than 15 pairs gives its rarest ones the nearest level of their colour.
- `layer`: the room's colours stay as drawn and are darkened to the ambient; a **light
  layer** (a fourth layer, role 3 in `ROOM`, so the room may have at most 2 graphics layers of
  its own) is added over the room by alpha blending (EVA 16, EVB 16). Its lit cells use at
  most 14 palette banks, and a pixel's value (1 to 15, 0 none) one of its bank's colours.
  What a value stands for (`light_index`):
  - `shared`: a level of light, made so that tiles repeat. A pixel's light is split into its
    cell's **strength**, how lit a flat surface is there (`falloff × Lz`), which is the
    cell's bank (up to 14 strengths, clustered on their square roots), and its **pattern**,
    how lit it is against a flat surface, `nz + nx tx + ny ty` with t = (Lx, Ly) / Lz, the
    light's slope. The cells share `light_directions` slopes: clustered from the cells'
    directions, then refined in a few rounds (each cell takes the slope that gets its pixels'
    true light nearest; each slope moves to the mean of its cells', weighted by their
    brightness). The patterns of all lit pixels are fitted to one ladder of 15 steps, the
    same in every bank; a bank's colours are its strength × the ladder. So the same drawing
    under the same slope gives the same tile, whatever its strength: a repeated brick is one
    tile for each slope it is lit from. Cells on a shadow's edge have tiles of their own,
    and so have the cells near the light, where its strength changes fast across a cell (by
    more than `light_tolerance` between the corners): there each pixel takes the step of its
    own light on the same ladder, in the bank that fits the cell best.
  - `fitted`: a level of light. The cooker works out each pixel's light as for `palette` (its
    normal, distance and shadow), groups the cells by how lit they are (mean and brightest
    pixel) into the banks, and fits each bank's 15 levels to its pixels (1D k-means); a pixel
    takes the nearest level. Each frame a bank's colours are
    `intensity × gain × light colour × level`. Smooth to the pixel, but most lit cells need a
    tile of their own.
  - `stream`: one of 7 (or 15) normals, as `normals`, so a brick is one tile wherever it is;
    the light is not in the banks but **streamed** into them line by line (H-blank DMA,
    runtime-core.md §12). The lit columns are cut into bands, narrower near the lights (half
    by steps of the light's square root along the wall at the lights' height, half evenly), a
    band per half bank (7 normals) or per bank (15); each 8x8 cell takes its column's band.
    For every room pixel row and band the cooker writes the band's colours:
    `light colour × intensity × (1 + flicker) × gain × max(0, N·F)` with F the lights'
    direction and falloff at the band's middle column on that row. Shadowed pixels are empty.
  - `hybrid`: as `stream`, but only the `light_stream_banks` banks whose bands are nearest the
    first light are streamed (32 colours a line instead of 112). The others hold fixed colours
    for the room's middle row of the light (its height), and the light layer's blend weight is
    set per line (BLDALPHA from a line table) to how lit the fixed bands are on that row against
    that middle row, times the flicker. The streamed rows are divided by that weight, so they come
    out exact. The fixed colours and the weights are fitted together, as the product closest to
    the light on every row where lit pixels use them (least squares, weighted by those pixels;
    the most weight is full light).
  - `light_bands = ramp` (stream and hybrid): one band per bank, and the bank's 15 values are
    samples of the light across the band per normal, from its left edge to its right. Each
    normal has its two edges (the same places as the neighbours' edges, so a bank's edge does not
    show), and the values left go one at a time to the normal whose pixels gain most (by its
    share of the pixels and its samples' spacing): with 5 normals the flat wall has 7 samples. A
    pixel takes the sample before or after it by an ordered 4×4 dither of where it is between
    them (the nearest when its normal has fewer than 4 samples), so the light ramps across the
    band. Its tiles depend on where in the band a cell is, so there are more of them. Hybrid: a
    streamed band's edge next to a fixed band has that band's fitted value there.
  - `normals`: one of 15 normals (the cooker's clusters of the room's normals). The cells
    inside the light's radius are grouped into regions (by the light's strength there first
    and its direction second), a region per bank, whose colours are, each frame,
    `intensity × gain × light colour × max(0, N·F)` with F the region's direction scaled by
    its falloff. Normal tiles are merged as usual (a repeated brick is one tile), but the
    light steps from region to region; cells on a shadow's edge have their shadowed pixels
    empty.

The map's tile size MUST be a multiple of 8. Map width and height MUST be at least one screen
of the profile (240×160 px on GBA).

## 2. Layers

Every layer MUST carry a `role` property. Layers without one are ignored with a warning.

| Role | Kind | Cooked to |
|---|---|---|
| `background` | tile layer, MAY use Tiled's `parallaxx`/`parallaxy` | BG layer with a scroll factor; a line table if the factor varies per line |
| `main` | tile layer | BG layer the entities stand in front of |
| `foreground` | tile layer | BG layer with a higher priority than sprites |
| `collision` | tile layer with the Blutmond collision tileset | collision grid, not drawn |
| `collision_material` | tile layer with the Blutmond material tileset | merged into the collision grid |
| `objects` | object layer | spawn table |

Optional layer properties: `priority` (int 0–3, overrides the default from the role) and
`effect` (name of a raster effect asset applied to this layer).

## 3. Collision

### 3.1 Model

The collision grid is separate from the graphics. Each cell describes a **shape**, **which
sides are solid**, **flips** and a **material**. This follows the layout used by the
Castlevania games on GBA and DS (block shape, "solid on top" and "solid at sides and bottom"
bits, H/V flips, water and effect bits), Super Metroid (block type plus a byte selecting the
slope and its flip) and Sonic (a height array per shape).

### 3.2 Cell encoding

Each cell is two bytes:

```
byte 0  bits 0-3  shape
        bit  4    hflip               mirror left/right
        bit  5    vflip               mirror top/bottom (ceilings)
        bit  6    solid_top           blocks movement downward into the shape
        bit  7    solid_sides_bottom  blocks movement sideways and upward
byte 1  bits 0-3  material
        bits 4-7  parameter           meaning depends on the material
```

### 3.3 Shapes

A shape is defined by a **height profile**: 16 values, one per pixel column of a 16-px cell
(every second column for 8-px cells), each the height of the solid part measured from the
bottom of the cell (0–16), plus a **surface angle** (`u8`, 256 = full turn, 0 = flat floor).

| Id | Name | Height profile (columns 0..15) | Angle |
|---|---|---|---|
| 0 | `empty` | all 0 | – |
| 1 | `full` | all 16 | 0 |
| 2 | `half_top` | same as `half_bottom` with vflip; kept as its own id for editing | 0 |
| 3 | `half_bottom` | all 8 | 0 |
| 4 | `half_left` | 16 for columns 0–7, 0 for 8–15 | 0 |
| 5 | `slope_1_1` | `i + 1` | 32 (45°) |
| 6 | `slope_1_2_a` | `(i + 1) / 2` | 19 (≈ 27°) |
| 7 | `slope_1_2_b` | `8 + (i + 1) / 2` | 19 |
| 8–11 | `slope_1_4_a`…`d` | `4k + (i + 1) / 4`, k = 0..3 | 10 (≈ 14°) |
| 12 | `slope_2_1_low` | `min(16, 2(i + 1))` | 45 (≈ 63°) |
| 13 | `slope_2_1_high` | `max(0, 2(i + 1) − 16)` | 45 |
| 14 | `custom` | from the room's custom shape table (§3.6) | from the table |
| 15 | reserved | – | – |

(`/` is integer division.) Unflipped slopes rise to the right. `hflip` mirrors the columns
(slope falls to the right). `vflip` measures heights from the top instead of the bottom (ceiling
shapes). Shapes that span several cells (1:2, 1:4, 2:1) MUST be placed in order; the cooker
warns when parts are missing or out of order.

### 3.4 Solidity

| `solid_top` | `solid_sides_bottom` | Behaviour |
|---|---|---|
| 1 | 1 | solid from every side |
| 1 | 0 | **jump-through**: collides only while moving down and only if the bottom of the hitbox was above the surface in the previous frame. Works for every shape: full platforms, half platforms (`half_bottom`, `half_top`), and jump-through slopes. Down + jump drops through (§4.3) |
| 0 | 1 | blocks sideways and upward movement, but is no floor |
| 0 | 0 | not solid; only the material applies |

### 3.5 Materials

| Id | Material | Parameter (bits 4-7) | Behaviour |
|---|---|---|---|
| 0 | `normal` | – | – |
| 1 | `water` | current: 0 none, 1–7 right, 9–15 left | swimming physics; with a solid shape the shape is solid under water |
| 2 | `damage` | damage amount (0 = 1) | `hit` event with that damage on touch, then the usual invulnerability frames |
| 3 | `death` | – | instant death (pits inside a room) |
| 4 | `conveyor` | speed in 1/8 px/frame; direction by hflip | adds speed to entities standing on it |
| 5 | `ice` | friction 0 (none) to 15 (normal) | replaces the entity's ground friction |
| 6 | `ladder` | 0 ladder, 1 vine | up/down starts climbing; the top cell SHOULD also be jump-through |
| 7 | `bounce` | strength in 1/2 px/frame | sets upward speed on landing |
| 8 | `sticky` | slowdown 1–15 | multiplies ground speed by (16 − p) / 16 |
| 9–15 | game defined | game defined | readable from scripts (`collision.material_at(x, y)`), no engine behaviour |

### 3.6 Custom shapes

A room MAY define up to 16 custom height profiles in a map property `shapes` (JSON array of
objects `{"heights": [16 integers], "angle": n}`). A cell with shape 14 uses the profile given
by the material parameter bits of its cell, so custom shapes cannot carry a material parameter
(the material itself still applies). In Tiled the collision tileset provides one tile per
custom index (`shape` = `custom`, `custom_index` = 0–15). Cooked rooms store the profiles in
the `SHAP` chunk (§7).

### 3.7 Breakable and changing blocks

Breakable, crumbling and switchable blocks are objects (§5), not materials. They change cells
at run time; the runtime stores such changes per persistent ID of the object that made them, so
they survive saving.

Built: the engine class `Breakable` (§5). Its rectangle covers whole collision cells that the
room's map draws (and makes solid) as usual. A hit of the player's side (the player's, or of
what they made: a thrown knife) whose rectangle overlaps it breaks it, if `by` allows: `any`
(the default), `pierce` (only a hit with `#Pierce`), `above` (only a hit whose origin is above
its top: a downward stab, the ground pound). Broken, from then on (`bmGameBreak`,
`src/world/game.c`):

- its cells read as `cell`: `empty` (the default), `top` (jump-through) or `full`, with
  material 0 (collision overrides, `BmCollision`; up to 8 broken ones a room);
- the main and foreground layers show nothing over its rectangle, to whole 8 pixel cells (holes,
  `bmRoomAddHole`); the background stays;
- debris (the sprite `debris`, one per 16 pixel cell, as many as there are effect places), the
  sound effect `crumble`, and its `drop` (a record name) left behind as an object of class
  `Drop`;
- the key room << 24 | persistent ID << 8 | 0xFF is kept with the value 1 among the `save`
  values (`BmActors` store, shared with scripts' `save` variables, 16 in all): entering the room
  again, or a save game loaded, breaks it again without debris, sound or drop.

Not yet: `collision.set` from scripts, crumbling and switchable blocks.

### 3.9 Physics puzzles

Built (`src/world/puzzle.c`): push blocks, pressure plates and gates (§5), drawn with the
sprite `puzzle` (animations `block`, `gate`, `plate`, `plate_down`, frames anchored at their top
left). Per room up to 4 of each.

- A `PushBlock` is a solid box to the pixel (`BmSolidBox` of the collision grid: solid on top
  and at its sides; a body stopped by it stops at its edge, not at a cell's). The player on the
  ground walking into its side, as tall as it there, for `push_frames` frames (6 by default)
  pushes it that way at 3/4 of a pixel a frame for as long as they keep at it (the player
  plays the animation `push`); it stops at what is solid at its sides (the map's cells, a
  broken wall's as it left them, closed gates), another block, the room's edge, the player.
  With nothing under any part of it (a cell solid on top, another block, the room's bottom) it
  falls, its speed growing by 1/4 pixel a frame up to 4, a pixel at a time onto what is below.
  Sounds: the effect `drag` while a block slides (started when it starts to slide, or again if
  another effect took its place; stopped when it stops, its notes fading with their release:
  the test level's plays `drag.wav`, a looping sample of dark noise made by
  `tools/make_drag_sound.py`), `thud` when a falling one lands.
- A `PressurePlate` is pressed while a push block overlaps it or the player stands on it; it
  opens the `Gate` its `gate` names.
- A `Gate` is solid (whole cells) until it is all the way open. Held open by a plate it slides
  up at 2 pixels a frame; when nothing holds it, it comes down again, unless the player is
  under it. Drawn behind the main layer, so it goes up into the wall above it.
- Where the blocks are (to 1/256 of a pixel across), how fast they fall, how far each gate is
  up and which plates are pressed are state (snapshots). A block at rest away from its place
  in the room is also kept in the store of kept values (`actors.store`, key `room << 24 |
  persistent id << 8 | 0xFE`, value `x | y << 16`; in the save game, as broken walls are):
  entered again, the room has it there, and a gate that a plate under such a block holds open
  is open from the start. A scripted object whose anchor a block covers when the room is
  entered starts on the block's top (its `home_y` too).

### 3.8 Tiled mapping

- `collision`: tile layer using `blutmond_collision.tsx`. Its tiles carry the properties
  `shape` (shape name) and `solid` (`all` = both bits, `top` = jump-through, `sides` =
  sides/bottom only). Row 0 of the tileset holds the solid shapes, row 1 the jump-through
  shapes. Tiled's flip flags on a placed tile set `hflip`/`vflip`; diagonal flips are an
  error.
- `collision_material`: tile layer using `blutmond_material.tsx`. Tiles carry `material`
  (name) and `parameter` (int). Tiled cannot give a placed tile its own properties, so each
  parameter value needs its own tile; the editor generates these tiles on demand.
- Graphics tilesets MAY carry `shape`, `solid` and `material` properties per tile. The cooker
  then fills the grid from the graphics; a non-empty cell in the `collision` or
  `collision_material` layer overrides it.
- With a 16-px collision cell and 8-px map tiles, the cooker takes the collision from the
  top-left tile of each 2×2 group; with tile and cell size equal it maps one to one.

## 4. Physics

### 4.1 Model

Simple platformer physics, not a rigid-body simulation. Values are `fix` (24.8). Per frame and
entity with a `Body`:

1. apply input (for `PlatformerControl`) and gravity (`gravity × gravity_scale`, water values
   inside water) to the velocity; clamp to `max_fall_speed`;
2. add the speed of what the entity stands on (conveyor, moving platform, another body);
3. move along X: resolve against solid bodies (pushing, §4.5), then against the grid (walls,
   slopes, steps);
4. move along Y, resolve against the grid (floors, ceilings, jump-through) and against the tops
   of solid bodies (standing on them);
5. update `on_ground`, `on_slope`, `in_water`, `on_ladder`, and send material events.

Entities are updated in the order of their slot in the entity table, which is deterministic.

### 4.2 Sensors

- Two floor sensors at the bottom corners of the hitbox (inset by 1 px), reading the height
  profile of the cell below; the higher result wins.
- Two ceiling sensors at the top corners, using vflipped profiles.
- Wall sensors at mid height and at `step_height` above the feet on each side.

### 4.3 Rules

- **Slopes**: the feet follow the height profile. Ground speed is multiplied by `slope_speed`
  for the slope's steepness when walking up. Surfaces steeper than `max_walk_angle` make the
  entity slide down.
- **Slope snapping**: while on the ground and walking down a surface, the feet stay on it if it
  is at most `snap_distance` below; otherwise the entity is falling.
- **Steps**: a rise up to `step_height` is climbed without jumping.
- **Jump-through**: see §3.4. `drop_through` ignores jump-through cells for 8 frames.
- **Ceiling slopes** turn upward speed into sideways speed along the surface instead of
  stopping it.
- **Ladders**: up/down on a ladder cell attaches the entity; gravity is off while attached;
  jump detaches.
- **Water**: water gravity and fall speed apply while the hitbox centre is in a water cell.

### 4.4 Parameters

| Component | Property | Type | Default |
|---|---|---|---|
| `Body` | `hitbox` | rect relative to the anchor | per class |
| `Body` | `gravity_scale` | fix | 1.0 |
| `Body` | `max_speed`, `accel`, `friction` | fix | per class |
| `Body` | `slope_speed` | 3 × fix (1:1, 1:2, 1:4) | 0.75, 0.9, 1.0 |
| `Body` | `max_walk_angle` | u8 | 32 (45°) |
| `Body` | `step_height` | int px | 8 |
| `Body` | `snap_distance` | int px | 4 |
| `PlatformerControl` | `run_speed` | fix | 2.0 |
| `PlatformerControl` | `jump_speed` | fix | 5.3 (≈ 3.5 tiles high at gravity 0.25) |
| `PlatformerControl` | `jump_cut` | fix | 0.5 (factor on upward speed when the button is released) |
| `PlatformerControl` | `coyote_frames`, `jump_buffer_frames` | int | 6, 6 |
| `PlatformerControl` | `drop_through` | bool | true |
| `PlatformerControl` | `climb_speed` | fix | 1.0 |
| `Body` | `solid` | bool | false (true: other solid bodies cannot overlap it, §4.5) |
| `Body` | `weight` | fix | 1.0 (player 1.0; a light crate 0.5, a heavy block 4.0) |
| `Body` | `pushable` | bool | false |
| `Body` | `can_push` | bool | true for the player, false otherwise |
| `Body` | `push_start_frames` | int | 0 (frames of leaning before a push starts; e.g. 8 for heavy blocks) |
| `Body` | `push_max_speed` | fix | 1.0 (upper limit while being pushed) |

### 4.5 Entity against entity

All entity hitboxes are axis aligned.

**Overlap without solidity.** Bodies with `solid = false` (the default: enemies, pickups,
projectiles) overlap freely. Overlaps call the script events `touch(other)` on both sides and
`hit(other, damage)` where attack boxes are involved.

**Solid bodies.** Two bodies that are both `solid` never overlap. Resolution happens in the X
step (§4.1, step 3) before the grid, and in the Y step for standing on top. A contact counts
when the vertical overlap of the two hitboxes is more than 2 px (so grazing a corner does not
push).

**Pushing.** When a body A moves horizontally into a solid body B:

- If B is not `pushable`, or A has `can_push = false`, B acts like a wall for A.
- Otherwise both move together with a **shared speed**, like an inelastic collision weighted by
  `weight`:

  ```
  v = (wA · vA + wB · vB) / (wA + wB)          (clamped to B's push_max_speed)
  ```

  A standing B (vB = 0) is therefore pushed with resistance: a player of weight 1 pushes a
  crate of weight 1 at half speed, a block of weight 4 at a fifth of the speed. Two bodies
  walking into each other with equal weight and speed stop. A keeps applying its input every
  frame, so a steady push gives a steady, slower speed.
- With `push_start_frames > 0` the push only starts after A has pressed against B for that many
  consecutive frames; until then B acts like a wall. This gives heavy blocks the "lean in first"
  feel.
- B is then moved by the grid rules like any body: it follows slopes and steps, falls off
  ledges, stops at walls. If B cannot move (wall, or another non-pushable body), A stops too.
- **Chains**: if B touches a third solid body C in the direction of the push, C joins: the
  weights of all bodies in the chain are added for the shared speed. Chains are limited to 4
  bodies; a longer chain acts like a wall.
- **Loads**: bodies standing on B are carried with it (step 2 of §4.1), and their weight is
  added to B's while B is pushed. A crate with another crate on it is harder to push.
- When the push ends, B keeps its speed and slows down with its own `friction` (and the
  material: on ice it slides on).
- Pushing is horizontal only. Bodies do not push each other vertically: a body landing on a
  solid body stands on it; a solid body cannot be pushed up by something below it.
- Events: `push(other)` on A and `pushed(by)` on B while the push lasts, `push_end()` on both
  when it ends. Scripts can use them for sounds, effort animations and puzzle checks.

**Order.** Pushes are resolved while the pushing entity is updated, in slot order (§4.1). A
body that was already moved by a push this frame is not updated a second time; its own update
uses the speed it was given. This keeps the result independent of how entities are stored
beyond the slot order and identical on every target.

### 4.6 Reachability check

The editor MAY simulate jumps with the room's physics from every standing spot and mark floors
and doors that cannot be reached. This is a tool feature; it does not change the data.

## 5. Objects

Objects live in the `objects` layer.

- The Tiled `type` (`class` from Tiled 1.9 on) is the Mondskript class name.
- Custom properties are values for that class's `var(...)` fields and override its
  `defaultproperties`. Unknown property names are an error.
- A point object marks the **anchor**: bottom centre, where the feet are. A rectangle is used
  for area classes.
- The Tiled object ID is the object's **persistent ID** (`u16`). State that must survive saving
  (defeated, collected, opened, block broken) is stored per persistent ID. The editor MUST keep
  IDs stable when objects are moved, copied (new ID) or deleted (ID not reused).

### Engine classes

| Class | Shape | Properties |
|---|---|---|
| `PlayerStart` | point | `facing` (`left`, `right`) |
| `Door` | rectangle | `target_room`, `target_door` (object name in the target room), `direction` (`north`, `south`, `east`, `west`), `panel` (a sprite: the door is drawn as a panel that swings open as the player comes near, [runtime-core.md](runtime-core.md) §12) |
| `CameraZone` | rectangle | camera bounds while the player is inside; `lock_x`, `lock_y` (bool) |
| `KillZone` | rectangle | `respawn` (`last_ground`, `checkpoint`) |
| `Trigger` | rectangle | `event` (`on_enter`, `on_leave`, `on_use`), `call` (script expression), `once` (bool) |
| `SavePoint` | point | – |
| `Pickup` | point | `item`, `persistent` (bool) |
| `MovingPlatform` | rectangle, with a polyline object named by `path` | `path`, `speed`, `wait_frames`, `loop` |
| `Breakable` | rectangle (whole cells) | `cell` (collision cell after breaking), `by` (attack kinds), `drop` |
| `PushBlock` | rectangle | solid, pushed by the player as long as they push, falling where nothing holds it (§3.9): `push_frames` |
| `PressurePlate` | rectangle (on a floor) | pressed by a push block on it or the player standing on it (§3.9): `gate` (the name of the Gate it opens) |
| `Gate` | rectangle (whole cells) | solid while closed; slides up while a plate holds it open (§3.9) |
| `Puddle` | rectangle | water that mirrors the room ([runtime-core.md](runtime-core.md) §12, "Puddles"): its top edge is the water line. The room leaves the pixels of the puddle empty in every layer; the mirrored background layer shows there, `squash` rows above the water line per line of the puddle (3), wavering by `ripple` pixels (1), blended over the backdrop (the water's colour) at `strength` sixteenths (10). The player and the actors over it are mirrored too. Needs a free background: a room of at most two layers, no light layer |
| `Light` | point | a light of the room (§1, "Light"; up to 4, flickering together: every one after the first takes the first's properties and only its own place): `colour` (`#ffc080`), `radius` (px, 72, at most 255), `height` (px out of the wall toward the viewer, 12), `intensity` (1.0), `flicker` (0 to 1: how far the intensity wavers, 0), `flicker_speed` (frames per step of the flicker, 6), `gain` (the light layer's strength, 0.5), `shadows` (true) |

Game classes (enemies, NPCs, lanterns) are written in Mondskript and used the same way.

**Kinds** ([../plans/layered-sprites.md](../plans/layered-sprites.md) §5): an object without a
class but with the property `kind` (a record's name, any table) is an object of the class the
record's first `attach` field names. Its record's fields, by name, say how it looks: `look`
(sprite; none: the class's), `palette` (name; the object's own property `palette` comes
first), `extra` (sprite: a layer drawn over it in step, the animation of the same name at the
same step, in front of it) at `extra_at` (name: a point of its frame; without it, its anchor).
The class's first `record<S>` variable starts as that record (a property of the variable's
name still wins). The cooker keeps such an object (one with neither class nor kind is
dropped with a warning). In a script, `layer_hide(#extra)` and `layer_show(#extra, ...)` hide
and show its extra layer (snapshot state); the rest of the kind is worked out from the room
object whenever the room is entered.

## 6. Validation

The cooker MUST report as errors: layers without role, unknown object classes or properties,
diagonal flips in collision layers, multi-cell slopes out of order, doors whose target room or
door does not exist, more than one `PlayerStart` per room. It SHOULD warn about: rooms without
`CameraZone` (the whole room is used), unreachable floors (if the check ran), `KillZone`
missing below open bottom edges.

## 7. Cooked form

Chunks written per room (see [pack-format.md](pack-format.md) for the container). All values
little endian; offsets are from the start of the chunk.

```
'ROOM'  struct BmRoomHeader {              // 44 bytes
          u16 width_px, height_px;
          u8  collision_cell;              // the map's tile size in pixels
          u8  layer_count;                 // graphics layers, at most 3
          u16 flags;                       // bit 0: edges_solid
          fix gravity, max_fall_speed, water_gravity, water_max_fall_speed;
          u16 music;                       // string ID, 0xFFFF = none
          u16 object_count;
          u8  area;                        // area group
          u8  dep_count;                   // 3
          u16 tiles_chunk, palette_chunk;  // in the area group
          u16 collision_chunk, objects_chunk;
          u16 vram_layout_chunk;           // VLAY of the area
          u16 room_id;                     // string ID
          u16 lite_chunk;                  // LITE of the room's light, 0: not lit
        }
        struct BmRoomLayer {               // 12 bytes, layer_count times
          u8  role;                        // 0 background, 1 main, 2 foreground, 3 light layer
          u8  priority;                    // 0 front .. 3 back: background 2, main 1, foreground 0
          u16 map_chunk;                   // TMAP
          fix parallax_x, parallax_y;
        }
        u16 deps[dep_count];               // chunk indices needed from other groups: VLAY, PALE, TILE
                                           // then zeros to a multiple of 4
'TMAP'  u16 width, height;                 // in 8x8 cells
        u8  format;                        // 0 GBA map entries, 1 SNES map entries
        u8  reserved; u16 reserved;
        u16 entries[width * height];       // row major. GBA: tile number in bits 0-9, 0x400 mirrored
                                           // left-right, 0x800 mirrored top-bottom, palette bank << 12
'LITE'  u8  mode;                         // 1 palette, 2 layer
        u8  count;                        // palette: levels (8); layer: normals (15), bit 7 fitted
        u8  first_bank, bank_count;       // palette: 0 and the area's banks; layer: the regions' banks
        s16 x, y;                         // the light, room pixels
        u8  radius, height;               // px
        u16 colour;                       // BGR555
        u8  ambient, flicker;             // 0..255 = 0..1
        u8  flicker_speed;                // frames per step
        u8  intensity, gain;              // 64 = 1.0
        u8  shadows;
        u8  more;                         // lights after the first: their s16 x, y at the chunk's end
        u8  0;                            // 20 bytes
        palette: u8 level[bank_count * 16]           // per palette entry, 0 = ambient only
                 u8 light[levels], padded to 2        // count bit 7: each level's light (255 = 1.0),
                                                      // else level / (levels - 1)
        layer:   { s8 x, y, z; u8 0; } normal[15]    // 127 = 1.0
                 { s16 x, y, z; u16 0; } region[bank_count]   // falloff × direction, 4096 = 1.0
        fitted:  u16 level[bank_count][15]           // 4096 = 1.0
        stream (count bit 6): u16 rows, u16 stride (bank_count × 16), u16 band width, u16 0,
                 u16 colour[rows][stride]            // BGR555, a row per room pixel row and one more
        hybrid (count bit 5): u16 rows, u16 stride (streamed banks × 16), u16 band width,
                 u16 fixed_banks,
                 u16 colour[rows][stride]            // the streamed banks, divided by the weight
                 u16 fixed[fixed_banks × 16]         // BGR555, the other banks' colours
                 u8  weight[rows], padded to 4       // 255 = full light (the light's own row)
'COLL'  u16 width, height;                 // in cells
        u8  cell_size; u8 pad[3];
        u8  cells[width * height][2];      // row major, §3.2
'SHAP'  u8 count; u8 pad[3]; { u8 heights[16]; u8 angle; u8 pad[3]; } [count]
'OBJS'  u16 count; u16 reserved;
        { u16 class;                       // string ID of the class name
          u16 persistent_id;               // Tiled object ID
          s16 x, y; u16 w, h;              // anchor (points) or top-left (rectangles)
          u16 name;                        // string ID
          u16 property_count;
          u32 properties_offset;           // to property_count pairs of u16 string IDs (key, value)
        } [count]                          // 20 bytes each
```

Objects are in the order of the Tiled file. An object without class or name has the id of the
empty string there. A room with any of the properties `attach`, `effect`, `effect_a`, `effect_b`, `title`,
`acoustics`, `echo`, `echo_feedback`, `echo_damping`, `muffle` has one
more object at the end, of the class `@room`, persistent id 0, position and size 0, whose
properties are those and the map's `Class.variable` properties: that is how the runtime finds the room's own scripts. Coordinates are Tiled's, rounded to whole pixels; `w` and `h` are 0 for a
point. Property values are stored as their text (`true`, `12`, `1.5`, a name), pairs sorted
by key; the runtime converts them when it creates the object
([runtime-core.md](runtime-core.md) §8). The layers of a room are stored in the order of the
Tiled file.

Chunks of the area (pack-format.md §2): `VLAY`, `PALE` and `TILE`:

```
'PALE'  u8 kind;                           // 0 background, 1 sprites
        u8 first_bank, bank_count, reserved;
        u16 colours[bank_count * 16];      // entry 0 of bank 0 is the backdrop colour
'TILE'  u16 count; u8 bpp;                 // 4
        u8 format;                         // 0 GBA (linear, low nibble = left pixel), 1 SNES planar
        u8 data[count * 32];               // tile 0 is always the empty tile
```

The string table (`STRS`, group 0) holds class names, object names, property keys and values,
room IDs and song names.

## History

- 0.2: what is built; details of the cooked form.
- 0.1: first draft.
- 0.1.1: solid bodies, pushing with weight-based resistance, chains and loads (§4.5).
- 0.1.2: room property `area`; dependency list in `ROOM`.
- 0.1.3: exact layouts of `ROOM`, `TMAP`, `OBJS`, `PALE` and `TILE` as implemented by the cooker.
- 0.2.1: room properties `attach` and `Class.variable`; the `@room` object in `OBJS`.
- 0.2.2: room properties `effect`, `effect_a`, `effect_b`, `title`.
- 0.2.3: room acoustics: `acoustics`, `echo`, `echo_feedback`, `echo_damping`, `muffle`.
- 0.2.4: light: the `Light` object (up to 4, flickering together), room properties `lighting` and `ambient`, tileset property
  `normal`, `LITE` (in the place of `ROOM`'s reserved field), the light layer (role 3);
  `light_index` (shared or fitted levels of light, bit 7 of `LITE`'s count), `light_directions`;
  `light_index = stream` (bit 6), `light_normals`, `light_band`; `light_index = hybrid` (bit 5),
  `light_stream_banks`; `light_bands`, `light_normals` 3 to 7; `light_levels`, `light_curve`
  (bit 7 of `LITE`'s count in mode palette: a light per level), `light_dither`; emissive pixels
  (black in the normal map); the `Puddle` object.
