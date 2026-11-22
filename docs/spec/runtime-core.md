# The runtime core: rules of the game

Version 0.1. Describes what the code does (`src/world/`, `apps/game/main.c`), in enough detail
to write a second implementation whose game state is the same, frame for frame. That is not a
nicety: the input sync ([link-protocol.md](link-protocol.md) §10) and save states moving
between machines ([state.md](state.md)) depend on it.

Where another document has the byte layouts (rooms, sprites, songs, scripts), this one only
says what the runtime does with them. Parts of [room-format.md](room-format.md) describe
rules that are planned and not built; where the two differ, this document is what runs.

## 1. Numbers

- All arithmetic is integer. No floating point anywhere in the runtime.
- **fix** is a signed 32-bit number with 8 fraction bits (24.8). `FIX(v) = v * 256`.
  `INT(f) = f >> 8` with an arithmetic shift, so it rounds toward minus infinity.
- `fixMul(a, b) = (int64(a) * b) >> 8`, arithmetic shift.
- `floorDiv(v, d)` rounds toward minus infinity (`d > 0`).
- C integer division (`/`) truncates toward zero; where a rule below says "divided by", it is
  that.
- Positions are in room pixels, x to the right, y down.

## 2. The frame

One call of the main loop (`apps/game/main.c`) per screen refresh:

1. Wait for the vertical blank (not when catching up in the input sync).
2. If a state was put into the inbox, load it.
3. Service the link (`bmLiveFrame`): port re-alignment, a pending reset, the packets received
   since the last frame in order, a stop asked for by the debugger, the input report, video
   snapshot parts. While a video snapshot is being sent the game stands still.
4. Read the keys (the autopilot's, the pad's, or the other side's in the input sync).
5. Report the keys if asked to (input sync), with a state checksum on frames divisible by 64.
6. `bmGameFrame(keys)` (section 3).
7. Write a snapshot if this frame was asked for.

Keys are a bit mask: A 1, B 2, Select 4, Start 8, Right 16, Left 32, Up 64, Down 128, R 256,
L 512.

## 3. One game frame

`bmGameFrame(keys)`, in this order:

1. `frame += 1`. `pressed = keys & ~keysPrevious`; `keysPrevious = keys`.
2. Player input: `dx = Right - Left`, `dy = Down - Up` (each 0 or 1), jump pressed = A in
   `pressed`, jump held = A in `keys`.
3. **Body step** (section 5). It returns events.
4. Decrement `hurtTimer`, `doorCooldown`, `saveCooldown` if not 0.
5. If the body jumped: play the effect `jump`.
6. If it touched a death material: **respawn** (section 6). Else if a damage material:
   **hurt** with damage 1.
7. **Engine objects** in room order, skipping objects that are scripted (section 8). Player
   box: `[px - 6, px + 6) x [py - 28, py)`, with `px = INT(player.x)`, `py = INT(player.y)`
   as they are after step 6. Boxes overlap when they share at least one pixel.
   - `SavePoint`: if `saveCooldown == 0`, the player is on the ground and overlaps the box
     `[x - 8, x + 8) x [y - 32, y)`: write the save game, `saveCooldown = 120`. Go on.
   - `KillZone` (needs a width and a height): on overlap respawn, and stop looking at
     objects.
   - `Door` (needs a width and a height): on overlap, if `doorCooldown == 0`, **go through**
     it (section 6) and stop looking at objects. If the cooldown is not 0 the door is skipped
     and the next object is looked at.
   - Other classes: nothing.
8. **Scripted objects** (section 8). Then, if one of them called `enter_door` and
   `doorCooldown == 0`, go through that door.
9. If `INT(player.y) > room height + 64`: respawn.
10. Player animation (section 9), camera (section 7), sprites (section 9), sound
    ([sound-format.md](sound-format.md)).

## 4. Collision

The room's collision grid ([room-format.md](room-format.md) §7, `COLL`): 2 bytes per cell,
cell size 16 or 8 pixels. Byte 0: shape in bits 0-3, hflip 0x10, vflip 0x20, solid top 0x40,
solid sides 0x80. Byte 1: material in bits 0-3.

**Outside the grid** a cell is empty. If the room's flag bit 0 (solid edges) is set, cells
left of, right of and above the grid are full, solid on top and sides; cells below the grid
stay empty.

**Height profiles.** The solid part of a shape per pixel column 0-15, measured from the
bottom of the cell:

| Shape | Columns 0 to 15 |
|---|---|
| 0 empty | 0 x16 |
| 1 full | 16 x16 |
| 2 half top | 8 x16 |
| 3 half bottom | 8 x16 |
| 4 half left | 16 x8, 0 x8 |
| 5 slope 1:1 | 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 |
| 6 slope 1:2 a | 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 8 |
| 7 slope 1:2 b | 8 9 9 10 10 11 11 12 12 13 13 14 14 15 15 16 |
| 8 slope 1:4 a | 0 0 0 1 1 1 1 2 2 2 2 3 3 3 3 4 |
| 9 slope 1:4 b | 4 4 4 5 5 5 5 6 6 6 6 7 7 7 7 8 |
| 10 slope 1:4 c | 8 8 8 9 9 9 9 10 10 10 10 11 11 11 11 12 |
| 11 slope 1:4 d | 12 12 12 13 13 13 13 14 14 14 14 15 15 15 15 16 |
| 12 slope 2:1 low | 2 4 6 8 10 12 14 16, then 16 x8 |
| 13 slope 2:1 high | 0 x8, then 2 4 6 8 10 12 14 16 |
| 14, 15 | 0 x16 (custom shapes are not implemented) |

With hflip the column is `15 - column`. In a grid of 8-pixel cells the column inside the cell
is doubled before the lookup and the height halved (truncating).

**`floor(x, yFrom, yTo, jumpThrough)`**: the topmost floor surface in pixel column `x`. For
each cell row from `floorDiv(yFrom, cell)` to `floorDiv(yTo, cell)`:

- skip the cell if its shape is empty, if it has vflip, if it is not solid on top, or if it
  is not solid on its sides and `jumpThrough` is false;
- its height `h` is 0 for shape 2 (half top), else the profile; skip if 0;
- `surface = row * cell + cell - h`. If `yFrom <= surface <= yTo` that is the result.

No result is "none". Note what follows: a cell that is solid on top only is a jump-through
platform, a vflipped cell has no floor at all, and neither has a half top.

**`solid(x, y)`**: whether the pixel blocks sideways and upward movement. False if the cell
is not solid on its sides or empty. Otherwise with `ly = y - row * cell` and `h` the profile
height: for a vflipped cell or a half top, `ly < h`; else `ly >= cell - h`.

**`material(x, y)`**: bits 0-3 of the cell's byte 1. 0 normal, 1 water, 2 damage, 3 death,
6 ladder. 4, 5, 7 and 8 (conveyor, ice, bounce, sticky) have numbers but no effect.

## 5. The player's body

State: `x`, `y` (fix, the feet: bottom centre of the box), `vx`, `vy` (fix per frame),
`onGround`, `climbing`, `inWater`, `jumpCutDone`, `facing` (-1 or 1), `coyote`, `jumpBuffer`,
`dropThrough` (frames), `safeX`, `safeY` (pixels).

Constants:

| Name | Value |
|---|---|
| half width, height | 6, 28 pixels |
| run speed | 2.0 |
| acceleration on the ground / in the air | 0.5 / 0.25 |
| friction | 0.5 |
| jump speed | 1357/256 (about 5.3) |
| jump cut | 0.5 |
| climb speed | 1.0 |
| step height, snap distance | 8, 4 pixels |
| coyote frames, jump buffer frames | 6, 6 |
| drop-through frames | 8 |

Gravity and the fastest fall, in air and in water, come from the room (`ROOM` chunk).

`approach(v, target, step)` moves `v` toward `target` by at most `step`.

**Floor sensors** are the two columns `px - 5` and `px + 4`; `findFloor(yFrom, yTo, jt)` is
the smaller `floor()` of the two (none if both are none).

**Wall test** `wall(x, feet)`: `solid(x, y)` for `y = feet - 9`, then every 7 pixels upward
while `y > feet - 28`; and for `y = feet - 27`.

One step, with `px = INT(x)`, `feet = INT(y)` and `wasOnGround = onGround` taken at the
start:

1. **Surroundings.** `centre = feet - 14`. `inWater` = material at `(px, centre)` is water.
   `onLadder` = ladder at `(px, centre)` or at `(px, feet - 1)`. `ladderBelow` = ladder at
   `(px, feet + 1)`.
2. If `dx != 0`: `facing = dx`. If jump was pressed: `jumpBuffer = 6`. If `dropThrough != 0`:
   decrement it.
3. **Ladders.** If not climbing and (`dy < 0` and `onLadder`, or `dy > 0` and (`onLadder` or
   `ladderBelow`) and jump was not pressed): start climbing; if `dy > 0` and `ladderBelow`,
   `dropThrough = 8`. Then, if climbing and neither `onLadder` nor `ladderBelow`: stop
   climbing.
4. **While climbing:** `vx = 0`, `vy = dy * 1.0`. If `jumpBuffer != 0`: stop climbing,
   `jumpBuffer = 0`, `vy = -fixMul(jump speed, 0.75)`, `jumpCutDone = false`, event *jumped*.
   Else if `dx != 0`: stop climbing.
5. **If not climbing** (also right after step 4 ended it):
   - `target = dx * 2.0`, halved (`/ 2`) in water. With `dx != 0`: `vx = approach(vx, target,
     wasOnGround ? 0.5 : 0.25)`. Without: `approach(vx, 0, 0.5)` on the ground,
     `approach(vx, 0, 0.125)` in the air.
   - If `jumpBuffer != 0` and (`wasOnGround` or `coyote != 0`): if `dy > 0`, `wasOnGround`
     and `findFloor(feet, feet, false)` is none (standing on a jump-through platform only):
     `dropThrough = 8`. Otherwise `vy = -jump speed`, `jumpCutDone = false`, event *jumped*.
     In both cases `onGround = false`, `wasOnGround = false`, `coyote = 0`, `jumpBuffer = 0`.
   - If jump is not held, `vy < 0` and not `jumpCutDone`: `vy = fixMul(vy, 0.5)`,
     `jumpCutDone = true`.
   - `vy += gravity` (the room's, in water the water's), then `vy = min(vy, fastest fall)`.
6. If `jumpBuffer != 0`: decrement it.
7. **Sideways.** `nx = x + vx`. If `vx != 0`: `edge = INT(nx) + 5` when moving right,
   `INT(nx) - 6` when moving left. If `wall(edge, feet)`: with `cellX = floorDiv(edge, cell) *
   cell`, `nx = FIX(cellX - 6)` moving right or `FIX(cellX + cell + 6)` moving left; `vx = 0`.
   `x = nx`, `px = INT(x)`.
8. **Up and down.** `ny = y + vy`.
   - `vy < 0`: `head = INT(ny) - 28`. For the columns `px - 5`, `px`, `px + 4` in this order:
     if `solid(column, head)`: `ny = FIX((floorDiv(head, cell) + 1) * cell + 28)`, `vy = 0`,
     stop looking. In any case `onGround = false`.
   - `vy >= 0`: `newFeet = INT(ny)`. `from = wasOnGround ? feet - 8 : feet`.
     `to = newFeet + (wasOnGround and not climbing ? 4 : 0)`.
     `f = findFloor(from, to, false)`. If `dropThrough == 0`: `j = findFloor(feet, to, true)`,
     and `f = j` if `j` is above `f`. If there is a floor: `ny = FIX(f)`, `vy = 0`, event
     *landed* if not `wasOnGround`, `onGround = true`, and if climbing, `dropThrough == 0` and
     `dy > 0`: stop climbing. Without a floor: `onGround = false`.
   - `y = ny`, `feet = INT(y)`.
9. `coyote = 6` if `onGround`, else decrement it if not 0.
10. **Materials.** At `(px, feet - 1)` and at `(px, feet - 14)`: damage gives the event
    *damage*, death the event *death*. If `onGround` and the materials at `(px, feet - 1)`
    and `(px, feet)` are both normal: `safeX = px`, `safeY = feet`.

`place(x, y)` sets the body to `FIX(x)`, `FIX(y)` with everything else 0, `facing = 1`,
`safeX = x`, `safeY = y`.

## 6. Hurt, respawn, doors, start

- **Hurt(damage)**: nothing if `damage <= 0` or `hurtTimer != 0`. Else play the effect `hurt`,
  `hurts += 1`, `hurtTimer = 60`, `vy = -3.0`, `vx = -facing * 2.0`.
- **Respawn**: `place(safeX, safeY)` keeping `facing`; `deaths += 1`; `hurtTimer = 60`.
- **Going through a door**: the door's property `target_room` names a room id. If there is no
  such room, nothing happens. Otherwise enter that room with the scripted objects starting
  fresh; `roomChanges += 1`; `doorCooldown = 20`. Arrival: if the property `target_door` names
  a `Door` object in the new room (by object name): `y = door.y + door.h`, and `x = door.x -
  12` if `door.x + door.w / 2 > room width / 2`, else `door.x + door.w + 12`. Else the first
  `PlayerStart`. Else (32, 32). The player is placed there keeping `facing` and `vx`.
- **Start**: enter the first room (or the one asked for by id) fresh; the random seed is 1;
  place the player at the first `PlayerStart` (facing left if its property `facing` is
  `left`), else at (32, 32).
- **Engine classes** are recognised by the exact class name: `PlayerStart`, `CameraZone`,
  `KillZone`, `Door`, `Trigger`, `SavePoint`. `Trigger` does nothing yet. Properties other
  than those named here are ignored.
- At most 64 objects of a room are read; further ones do not exist for the runtime.
- **Save game**: the `save` fields of the state ([state.md](state.md)) at offset 0 of the
  save memory. Loading one sets `vx = vy = 0` and enters the stored room with the scripted
  objects fresh.

## 7. Camera and the room on screen

**Camera.** With `cx = INT(player.x)`, `cy = INT(player.y) - 14`: the zone is the first
`CameraZone` object (in room order) that contains `(cx, cy)`, else the whole room. Target
`(cx - 120, cy - 80)`, clamped to the zone (first the upper limit `zone end - screen size`,
then the lower limit `zone start`), then to the room (`0 .. max(0, room size - screen
size)`). The screen is 240 x 160.

**Video memory** when a room is shown:

- Background palette: the `PALE` chunk's colours to background colour index `first_bank * 16
  + i`.
- Tiles: the `TILE` chunk (4 bits per pixel) to character block 0 from tile 0, so map entries
  use the chunk's tile numbers unchanged.
- Layer *i* (at most 3) is background *i*: character block 0, size 512 x 256, screen blocks
  `26 + 2i` and `26 + 2i + 1`, the priority from the `ROOM` chunk.

**Map streaming.** A layer's map can be larger than the 64 x 32 cells of its two screen
blocks, so only a window around the screen is kept there, as a ring:

- Layer scroll `lx = (int64(cameraX) * parallaxX) >> 8`, the same for y. Window origin
  `wx = floorDiv(lx, 8)`, `wy = floorDiv(ly, 8)`. The window is 32 x 22 cells.
- Map cell `(x, y)` is stored at ring position `hx = x & 63`, `hy = y & 31`: screen block
  `26 + 2i + (hx >= 32 ? 1 : 0)`, entry `hy * 32 + (hx & 31)`. Cells outside the map are
  entry 0.
- When the window moves by less than its size, only the columns and rows that come into view
  are written; otherwise the whole window.
- The background's scroll registers get `lx & 511` and `ly & 255`.

What the screen shows therefore depends only on the camera position, not on how it got there.

## 8. Scripted objects

A room object whose class name equals the name of a class in the pack's script module
([mondskript.md](mondskript.md)) is scripted; the engine then leaves it alone, also for
engine classes. The first 8 such objects in room order exist; further ones do not.

**Per object:** class, interpreter state (state, wait, place to continue, timer, 8 variables),
`x`, `y` (fix), `facing`, `hidden`, `touching`, animation state, and derived from the room:
`home` and the sprite sheet.

- `home = FIX(obj.x + obj.w / 2), FIX(obj.y + obj.h)`: a point as it is, a rectangle at its
  bottom centre.
- Sprite sheet: the one named by the class's `sprite`, else the one named like the class;
  names compare without regard to ASCII case. None is allowed.

**Entering a room fresh** (a room change, a start, a loaded save game; also after a state
load if the stored objects do not fit the room or the module): for each object in order:
variables to the class's defaults; then object properties named like a variable declared `var(Category)` (bool:
`true` or `1`; int: decimal; fix: decimal with up to 4 fraction digits, value `whole * 256 +
(fraction * 256 + scale / 2) / scale`; name and text: the string id); then values kept for
`save` variables; position to `home`, `facing = 1`, visible, animation 0 from its start; then
the event `spawn`.

**Each frame**, for each object in order: the event `tick`; the event `touch` if it touches
the player now and did not in the frame before; one step of its state code. After all
objects: one animation tick for each object that has a sheet.

**Touching the player**: the object's box and the player's box share a pixel. The object's
box is, with `ox = INT(x)`, `oy = INT(y)`: for an object with a width and height
`[ox - w/2, +w) x [oy - h, +h)`; else, with a sheet, the hitbox of the frame showing (mirrored
when `facing < 0`: `x = -x - w`), relative to `(ox, oy)`; else `[ox - 8, +16) x [oy - 16,
+16)`.

**`save` variables** are kept in a table of 16 entries, key `(room index & 0xFF) << 24 |
persistent id << 8 | variable index`, written whenever such a variable is assigned. A 17th
value is dropped and counts as a script error.

**What scripts call** (the numbers are the API indices in bytecode):

| # | Function | What it does |
|---|---|---|
| 0 | `sleep(n)` | waits `n` frames, `n` limited to 1..65535 |
| 1 | `finish_anim()` | waits until the animation has finished; not at all if the object has no sheet, the animation loops or does not exist |
| 2 | `play_anim(name)` | switches to the animation of that name (ASCII case ignored) from its start; nothing if it is the one playing or unknown |
| 3 | `anim_done()` | whether a non-looping animation reached its end |
| 4, 5 | `player_x()`, `player_y()` | the player's `x`, `y` (fix, the feet) |
| 6 | `player_distance()` | `dist(INT(player.x - x), INT(player.y - y) - 14)` in pixels |
| 7 | `move_toward_player(speed)` | `move_toward(player.x, player.y - FIX(14), speed)` without a result |
| 8 | `move_toward(tx, ty, speed)` | with `dx = tx - x`, `dy = ty - y`, `d = dist(dx, dy)` (all fix): nothing and 0 if `speed <= 0`; if `d <= speed` the object is put on the target, result 1; else `x += int64(dx) * speed / d`, the same for y, result 0 |
| 9 | `hurt_player(damage)` | section 6 |
| 10 | `enter_door()` | the player goes through this object as a door after all objects had their turn |
| 11, 12 | `hide()`, `show()` | not drawn / drawn |
| 13 | `face_player()` | `facing = -1` if `player.x < x`, else 1 |
| 14 | `log(n)` | prints on the PC, nothing on a device |
| 15 | `random(n)` | `seed = seed * 1664525 + 1013904223` (32 bits); result `(seed >> 8) % n`, or 0 if `n <= 0` (the seed still advances) |
| 16 | `touching_player()` | as above |
| 17, 18 | `give_item(name)`, `has_item(name)` | an item is the 32-bit FNV-1a hash of its name, the same key as a record of that name; `give_item` puts one into the inventory ([state.md](state.md) §4) unless the player has one already, `has_item` is true if they have at least one |
| 19 | `abs(n)` | |
| 20 | `play_sound(name)` | starts that effect |
| 21 | `play_music(name)` | starts that song unless it is the one playing |
| 22 | `record_get(table, key, field)` | a value of a record, or of the structure's defaults if the key is not in the table; 0 if there is no table ([data-format.md](data-format.md) §4). The compiler writes these three for `record<S>`; `field` is `offset \| type << 16` |
| 23 | `record_count(table)` | the number of records of that structure; 0 without a table |
| 24 | `record_at(table, index)` | the key of the record at that place in key order; 0 outside |
| 25 | `item_count(record)` | how many of that record the inventory holds |
| 26 | `give(record, amount)` | adds up to 255 of a kind (a new kind needs one of the 64 entries); returns how many were added |
| 27 | `take(record, amount)` | takes them away if there are that many (true), else changes nothing (false); an entry at 0 is removed |
| 28 | `stat(value)` | the player's own value of a stat: the enum value's number picks one of 16 (`statBase`); 0 outside |
| 29 | `add_stat(value, amount)` | adds to it, clamped to a 16-bit signed number |
| 30 | `attach(slot, class)` | attaches the Attachment class of that name to the player in slot 0 to 7, replacing what is there (which gets `detached()`); the new one gets `attached()` at once. False if there is no such class or slot ([mondskript.md](mondskript.md) §11, "Attachments") |
| 31 | `attach_for(slot, class, frames)` | the same, detached by itself after that many frames (at the end of a frame's work) |
| 32 | `detach(slot)` | `detached()`, then the slot is empty |
| 33 | `impulse(vx, vy)` | added to the player's speed once (fix) |
| 34 | `room_attach(slot, class)` | like `attach`, in the room's slot 0 to 3; the room detaches it when it is left |
| 35 | `room_detach(slot)` | like `detach`, for the room's slot |

`dist(a, b) = max(|a|, |b|) + min(|a|, |b|) / 2`.

Actor variables by index: 0 `x`, 1 `y`, 2 `home_x`, 3 `home_y`, 4 `facing`. Writing `facing`
stores -1 for a negative value, else 1. `home_x` and `home_y` cannot be written.

## 9. Sprites on screen

**Loading**, once per pack: the `SPRT` chunks in pack order, at most 8. Each sheet's tiles go
to sprite tile memory after the previous sheet's (the first at tile 0), its 16 colours to
sprite palette bank = its number. A sheet that is invalid or would pass tile 1024 is skipped.
After the sheets come the built-in figure (8 tiles, bank 14) and the placeholder frame (4
tiles, bank 15). Sprite tiles are mapped one-dimensionally.

**Animation state**: `anim`, `finished`, `step`, `timer`.

- `play(anim)`: nothing if it is the animation playing; else `step = 0`, `timer = 0`,
  `finished = 0`.
- `tick`: nothing if the animation does not exist or has no steps. If `step >= count`:
  `step = 0`. `timer += 1`; if `timer < duration of the step`: done. Else `timer = 0` and: the
  next step if there is one; else `step = 0` if the animation loops; else `finished = 1`.
- The frame shown is the step's frame.

**The player**, with a sheet named exactly `player`: animations `idle`, `run`, `jump`, `fall`,
`climb` (a missing one is animation 0). Climbing: `climb`, ticking only while `vx` or `vy` is
not 0. Else in the air: `jump` if `vy < 0`, else `fall`. Else `run` if `|vx| > 32/256`. Else
`idle`. Play, then tick.

**Drawing a frame** at screen position `(sx, sy)` = position minus camera: for each piece,
`x = hflip ? sx - dx - w : sx + dx`, `y = sy + dy`, with the piece's size from the hardware
table (shape 0: 8, 16, 32, 64 square; shape 1 wide: 16x8, 32x8, 32x16, 64x32; shape 2 tall:
8x16, 8x32, 16x32, 32x64). A piece entirely off the screen takes no hardware sprite. Tile =
the sheet's first tile + the piece's tile; palette bank = the sheet's; priority 1.

**Order of the 48 hardware sprites used:**

1. The player: its frame, mirrored when `facing < 0`, at most 8 pieces; not drawn while
   `hurtTimer & 4` is not 0. Without a `player` sheet: the built-in 16 x 32 figure at
   `(px - 8, py - 32)`.
2. Scripted objects in order, unless hidden: their frame at `(INT(x), INT(y))`, mirrored when
   `facing < 0`. Without a sheet, and only for objects without width and height: the
   placeholder at `(INT(x) - 8, INT(y) - 16)`.
3. Unscripted objects of other classes and `SavePoint`s with a class name: with a sheet named
   like the class, animation 0 at time `frame` (the frame whose step contains `frame modulo
   the animation's total duration`), never mirrored; else the placeholder at `(x - 8, y)` for
   an object with a size, `(x - 8, y - 16)` for a point.
4. The rest are hidden.

## 10. What is not specified here

Sound ([sound-format.md](sound-format.md)), the script interpreter
([mondskript.md](mondskript.md)), the link ([link-protocol.md](link-protocol.md)) and the
hardware ([gba-backend.md](gba-backend.md)) have their own documents. The autopilot
(`apps/game/autopilot.c`) is a test player, not part of the core.
