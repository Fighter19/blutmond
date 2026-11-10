# Room format

Version 0.1, draft.

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
at run time through `collision.set(cell_x, cell_y, cell)`. The runtime stores such changes per
persistent ID of the object that made them, so they survive saving.

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
| `Door` | rectangle | `target_room`, `target_door` (object name in the target room), `direction` (`north`, `south`, `east`, `west`) |
| `CameraZone` | rectangle | camera bounds while the player is inside; `lock_x`, `lock_y` (bool) |
| `KillZone` | rectangle | `respawn` (`last_ground`, `checkpoint`) |
| `Trigger` | rectangle | `event` (`on_enter`, `on_leave`, `on_use`), `call` (script expression), `once` (bool) |
| `SavePoint` | point | – |
| `Pickup` | point | `item`, `persistent` (bool) |
| `MovingPlatform` | rectangle, with a polyline object named by `path` | `path`, `speed`, `wait_frames`, `loop` |
| `Breakable` | rectangle (whole cells) | `cell` (collision cell after breaking), `by` (attack kinds), `drop` |
| `PushBlock` | rectangle | solid and pushable body for puzzles: `weight`, `push_start_frames`, `push_max_speed`, `friction` |

Game classes (enemies, NPCs, lanterns) are written in Mondskript and used the same way.

## 6. Validation

The cooker MUST report as errors: layers without role, unknown object classes or properties,
diagonal flips in collision layers, multi-cell slopes out of order, doors whose target room or
door does not exist, more than one `PlayerStart` per room. It SHOULD warn about: rooms without
`CameraZone` (the whole room is used), unreachable floors (if the check ran), `KillZone`
missing below open bottom edges.

## 7. Cooked form

Chunks written per room (see [pack-format.md](pack-format.md) for the container):

```
'ROOM'  struct BmRoomHeader {
          u16 width_px, height_px;
          u8  collision_cell;        // 8 or 16
          u8  layer_count;
          u16 flags;                 // bit 0: edges_solid
          fix gravity, max_fall_speed, water_gravity, water_max_fall_speed;
          u16 music_id;              // 0xFFFF = none
          u16 object_count;
          BmRoomLayer layers[layer_count];  // role, priority, parallax (fix), map chunk index
        }
'COLL'  u16 width, height (cells); u8 cell_size; u8 pad[3];
        u8 cells[width * height][2];        // row major, §3.2
'SHAP'  u8 count; u8 pad[3]; { u8 heights[16]; u8 angle; u8 pad[3]; } [count]
'OBJS'  { u16 class_id; u16 persistent_id; s16 x, y; u16 w, h;
          u16 props_offset; u16 props_size; } [object_count]
        followed by the property blobs (class field layout, see state.md)
```

## History

- 0.1: first draft.
- 0.1.1: solid bodies, pushing with weight-based resistance, chains and loads (§4.5).
