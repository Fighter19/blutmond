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

1. `frame += 1`. `pressed = keys & ~keysPrevious`; `keysPrevious = keys`. If no screen is open,
   Start is pressed and no dialogue is shown: open the Screen class named `StartMenu`, if
   there is one (that press is no press for it). If a screen is open: the screens' frame
   (section 11), the UI layer, sprites and sound, and nothing else: the room stands still.
2. Player input: `dx = Right - Left`, `dy = Down - Up` (each 0 or 1), jump pressed = A in
   `pressed`, jump held = A in `keys`.
3. The player's attachments (`press`/`release`, `tick`, state code). If `lockTimer != 0`:
   `dx = 0`, jump not pressed, `lockTimer -= 1`. **Body step** (section 5). It returns
   events.
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
     it (section 6) and stop looking at objects. A door with `direction` north (in the back
     wall) only when Up is pressed this frame; for a scripted one, Up pressed while the player
     overlaps it makes it hear `touch()` again this frame, and its `enter_door` counts only
     with Up pressed this frame. If the cooldown is not 0 the door is skipped
     and the next object is looked at.
   - Other classes: nothing.
8. **Scripted objects** (section 8). Then, if one of them called `enter_door` and
   `doorCooldown == 0`, go through that door.
9. If `INT(player.y) > room height + 64`: respawn. Then the cell of the world map the player
   is in is explored ([world-format.md](world-format.md) §4).
10. Player animation (section 9). If `animHeld` is 2 and `lockTimer == 0`, or 1 and the
    animation finished, `animHeld = 0`. While `animHeld != 0` the animation only ticks;
    otherwise the engine chooses it from the body.
11. **Hits** (section 10).
12. Camera (section 7), sprites (section 9), sound ([sound-format.md](sound-format.md)).

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
`dropThrough` (frames), `safeX`, `safeY` (pixels), `flipped`, `airJumpsLeft`, `wallSide`
(-1, 0, 1).

**Capabilities** (the player's stats, read before each body step; first-items.md §5): `air
jumps` = the stat AirJumps (number 5), 0 to 255; `wall slide` = the stat WallSlide (number 7)
above 0. Both 0 / off unless something adds to them, so the steps below are those of a body
without them.

**Moving while locked** (`lockGround`, `lockAir`, game.h): while `lockTimer` holds the player
there is no jump, and on the ground by `lockGround`: 0 slide (no direction: the speed fades
with friction), 1 stop (`vx = 0`, no direction), 2 keep (no direction, `coast`: no friction),
3 walk (as usual); in the air by `lockAir`: 0 steer (as usual), 1 keep (no direction, `coast`:
no air drag), 2 drift (no direction: air drag). After the body step `facing` is put back as it
was (an attack goes where it was aimed). `owner_lock` sets slide and steer, `player_lock`
(a scene) slide and drift, `lock_move(ground, air)` after it what the script wants (the test
level's swings: the weapon record's `ground_move`, `air_move`).

**Gravity flipped** (`flipped`, `flip_gravity`): gravity pulls up. The box stays `[y - height,
y]`, but the feet are its top: everything below is mirrored (jumping goes down, the gravity and
the fastest fall point up, a "floor" is the underside of anything solid from below, found
pixel by pixel from the feet up to where they get to; the head's check is at the box's bottom;
walls are checked from step height below the feet). Jump-through platforms work mirrored: as
a floor counts only by its top, a platform counts only by its underside (a platform pixel with
none below it, `bmCollisionSolidPlatform`), and only from the feet on (not from step height
past them), not while `dropThrough`; standing under one with nothing solid from below above
the feet, Up (toward the feet) + jump drops through it. No ladders, and no safe position is
noted. Placing the body (a door, a teleport, a
respawn) ends it; so does leaving the room above its top by 64 (a respawn). The sprite is drawn
upside down with its anchor at the box's top.

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
| wall slide speed, wall jump push | 1.0, 2.0 |

Gravity and the fastest fall, in air and in water, come from the room (`ROOM` chunk).

`approach(v, target, step)` moves `v` toward `target` by at most `step`.

**Floor sensors** are the two columns `px - 6` and `px + 5`, the box's outermost (inset ones
let a jump onto a block's corner sink into it); `findFloor(yFrom, yTo, jt)` is
the smaller `floor()` of the two (none if both are none).

**Wall test** `wall(x, feet)`: `solid(x, y)` for `y = feet - 9` (in the air `feet - 1`: no
stepping up there), then every 7 pixels upward while `y > feet - 28`; and for `y = feet - 27`.

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
4. **While climbing:** `airJumpsLeft = air jumps`, `vx = 0`, `vy = dy * 1.0`. If `jumpBuffer != 0`: stop climbing,
   `jumpBuffer = 0`, `vy = -fixMul(jump speed, 0.75)`, `jumpCutDone = false`, event *jumped*.
   Else if `dx != 0`: stop climbing.
5. **If not climbing** (also right after step 4 ended it):
   - `target = dx * 2.0`, halved (`/ 2`) in water. With `dx != 0`: `vx = approach(vx, target,
     wasOnGround ? 0.5 : 0.25)`. Without: `approach(vx, 0, 0.5)` on the ground,
     `approach(vx, 0, 0.125)` in the air.
   - If `wasOnGround` or `inWater`: `airJumpsLeft = air jumps` (not at a wall: a jump in the
     air spent before a wall jump stays spent until the ground).
   - If `jumpBuffer != 0` and (`wasOnGround` or `coyote != 0`): if `dy > 0`, `wasOnGround`
     and `findFloor(feet, feet, false)` is none (standing on a jump-through platform only):
     `dropThrough = 8`. Otherwise `vy = -jump speed`, `jumpCutDone = false`, event *jumped*.
     In both cases `onGround = false`, `wasOnGround = false`, `coyote = 0`, `jumpBuffer = 0`.
     Else if `jumpBuffer != 0`, wall slide and (`wallTouch != 0` or `wallCoyote != 0`): a jump
     off the wall, with `side = wallTouch` if it is not 0, else `wallCoyoteSide`: `vy = -jump
     speed`, `vx = -side * 2.0`, `facing = -side`, `jumpCutDone = false`, `jumpBuffer = 0`,
     `wallSide = wallTouch = wallCoyote = 0`, events *jumped* and *wall jumped* (before a
     jump in the air: a wall at hand never costs one). Else if `jumpBuffer
     != 0` and `airJumpsLeft != 0`: `vy = -jump speed`, `airJumpsLeft -= 1`, `jumpCutDone =
     false`, `jumpBuffer = 0`, events *jumped* and *air jumped*.
   - If jump is not held, `vy < 0` and not `jumpCutDone`: `vy = fixMul(vy, 0.5)`,
     `jumpCutDone = true`.
   - `vy += gravity` (the room's, in water the water's), then `vy = min(vy, fastest fall)`;
     with wall slide and `wallSide != 0` (as the last step left it), `vy = min(vy, 1.0)`.
6. If `jumpBuffer != 0`: decrement it.
7. **Sideways.** `nx = x + vx`. If `vx != 0`: `edge = INT(nx) + 5` when moving right,
   `INT(nx) - 6` when moving left. If `wall(edge, feet)`: with `cellX = floorDiv(edge, cell) *
   cell`, `nx = FIX(cellX - 6)` moving right or `FIX(cellX + cell + 6)` moving left; `vx = 0`.
   `x = nx`, `px = INT(x)`. Then `wallSide = 0`, and with wall slide, not `wasOnGround`, not
   climbing, not in water, `dx != 0` and `vy >= 0`: if `wall(dx > 0 ? px + 6 : px - 7, feet)`
   (as in the air), `wallSide = dx`. Then `wallTouch = 0`, and with wall slide, not
   `wasOnGround`, not climbing, not in water: for the side `dx` (if 0: `facing`) and then the
   other, the first with `wall(px + 6 + d, feet)` (right) or `wall(px - 7 - d, feet)` (left)
   for some `d` of 0, 1, 2 (as in the air) gives `wallTouch` (1 right, -1 left), whether the
   body rises or falls and whatever is held. If `wallTouch != 0`: `wallCoyote = 6`,
   `wallCoyoteSide = wallTouch`; else if `wallCoyote != 0`: decrement it.
8. **Up and down.** `ny = y + vy`.
   - `vy < 0`: `head = INT(ny) - 28`. For the columns `px - 6`, `px`, `px + 5` in this order:
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
`safeX = x`, `safeY = y`. The grapple moves the player with `place` but keeps `safeX` and
`safeY`: an anchor (or a ledge before they stand on it) is no safe place.

## 6. Hurt, respawn, doors, start

- **Strike(amount, fromX, element, striker)**: nothing if `amount <= 0` or `hurtTimer != 0`.
  `amount -=` the stat Defence (number 3), at least 1; if `element` is 1 to 3 and
  `resists[element] != 0`, `amount = max(1, amount / 2)`. Then the player's attachments in
  slot order get `damage_in(amount)` (each the amount the one before left; `set_damage(n)`
  changes it, `damage_is(element)` tells the element); if it is 0 or less now, nothing
  happens (no strike: false). Then play the
  effect `hurt`, `hurts += 1`, `hurtTimer = 60`, `vy = -3.0`, `vx = away * 2.0` with `away =
  -1` if `fromX > x`, 1 if `fromX < x`, else `-facing`; the player's hits end (section 10);
  `health -= min(amount, health)`; `striker` (a figure number of section 10, 0xFF for none) is
  the figure striking the player while each of the player's attachments in slot order gets
  `hit(amount)` (`strike_back`), 0xFF again after; then if `health <= 0`: respawn and `health`
  = the most. A hit strikes with its element and source; `hurt_player` with element 0 and the
  calling object; a hazard with neither.
- **Mana**: starts at the most (20 + the stat MaxMana, number 1); at the start of a frame
  (after the frame count goes up) `mana += 1` if `frame % 90 == 0` and it is below the most.
- **Speed**: while `speedPercent != 0` the body step runs with `runSpeed * max(10, 100 +
  speedPercent) / 100`.
- **Quick slots**: in a frame without a screen and without dialogue, L pressed while neither R
  nor Select is held uses `quick[0]`, Select pressed while neither L nor R is held `quick[1]`,
  as `use_item` does; before the attachments' frame.
- **Return** (`returnPending`): first thing in the next frame after the frame count and before
  mana: every screen is closed and a teleport to `savedRoom`, `savedX`, `savedY` begins
  (section 12, "Teleport"). `savedRoom`, `savedX`, `savedY` are the start room
  and the player's place at the start, and the room and the save point's place whenever a save
  is written at a save point.
- **The most health**: 30 + the stat MaxHealth (number 0), at least 1. The stats' numbers are
  a convention with the game's `enum Stat` (first-items.md §1).
- **Dialogue** (`say(speaker, line)`, and with a portrait `say_line`, `say_as`): `dialogue =
  1` and the string ids of the speaker's name, the line and the portrait (a sprite's name,
  0xFFFF none) are kept; unless a screen is open, the UI layer's rows 15 to 19 are cleared, a
  frame of 30 x 5 cells is drawn there, the speaker's text at (1 + p, 16) and the line wrapped
  into (27 - p) x 2 cells at (2 + p, 17), with p = 5 if the portrait names a sprite sheet of
  the pack, else 0, and the layer is shown. While `dialogue != 0` and no screen is open, that
  sheet's first animation (its frame at the game frame) is drawn first of all sprites, with
  its anchor at (8, 124) and priority 0, in front of the box. `say_line(record)` reads the
  record's fields `speaker` (a record) and `says` (a text), and of the speaker `name` (a text)
  and `portrait` (a sprite), by their names in whichever table holds them;
  `say_as(speaker, line)` the speaker's two, and of the speaker also `voice` (a sound).
- **The line appears** letter by letter: `dialogueShown` letters of `dialogueLength`
  (characters of the line as wrapped: spaces and line breaks count) are drawn. In each game
  frame without a screen, after the buttons, one more appears; when its number n is even, a
  voice is set and the character is a letter or digit (A-Z, a-z, 0-9, or any code from U+00C0),
  the voice plays as an effect `(code * 7) % 5 - 2` semitones higher. The text is 2 rows of
  `26 - indent` cells from (2 + indent, 17); a line longer than that **scrolls**: rows before
  `dialogueRow` (snapshot) are laid out but not drawn, and `dialogueLength` counts the
  characters up to the box's last row. When they are all there and more of the line follows,
  a filled triangle (U+25BC, a glyph of the engine's font) at (28, 18) says so, bobbing: every
  16 frames (`frame / 16` odd) it is the same a pixel lower (U+E800); A then scrolls the text up a row (`dialogueRow + 1`, the box drawn
  again with what was shown) and the rest goes on appearing. A pressed while the box's rows are
  not all there shows them at once (no jump); pressed when they are and nothing follows, it
  closes the box. Drawn again after the layer was cleared, as far as it had come. A pressed in a frame while `dialogue != 0` (step 3 of section 3): the rows
  are cleared, `dialogue = 0`, and that A is no jump.
- **Hurt(damage)** (hazards, `hurt_player`): Strike(damage, `x + facing`), so the push is
  backwards.
- **Health** starts at the most and is kept in the save game.
- **Respawn**: `place(safeX, safeY)` keeping `facing`; `deaths += 1`; `hurtTimer = 60`.
- **A room's music**: entering it, its `music` property names a song that starts as music
  (the intensity set to 15 first; the song already playing goes on, fading back to full
  volume); `none` fades the music away in 120 frames, then it stops; no property: the music
  goes on.
- **The walk through a door** with a panel (section 12, "Doors"): going through it (an overlap
  or `enter_door`) while no walk is under way starts one instead: `doorSeq` = its object + 1,
  `doorTimer = 0`. At the start of each frame while it lasts: A, B or Start pressed skips it,
  then the buttons count as none (nothing is pressed, held or let go for the game, the menus,
  the attachments), `doorTimer += 1` (but while it is 0 only once the player stands: on the
  ground, climbing or in water; a jump into a door comes down first, the door still shut),
  `hurtTimer` at least 2. A respawn ends the walk. The body gets, after a lock: while
  `doorTimer` is 0 no direction and no jump (the body step: falling); then for a
  side door (`direction` east or west) the direction that way and no jump, and instead of the
  body step the player walks: `facing` that way, `vx` = the running speed that way, `vy = 0`,
  `x += vx` (walls and the room's edges do not stop it: out through the door); for another door
  no direction (the player stands). After the scripted objects (step 8): if skipped, or
  `doorTimer` reached 30 (side) or 20 (other), `doorSeq = 0` and the player goes through it.
- **Going through a door**: the door's property `target_room` names a room id. If there is no
  such room, nothing happens. Otherwise enter that room with the scripted objects starting
  fresh; `roomChanges += 1`; `doorCooldown = 20`. Arrival: if the property `target_door` names
  a `Door` object in the new room (by object name): `y = door.y + door.h`, and `x = door.x -
  12` if `door.x + door.w / 2 > room width / 2`, else `door.x + door.w + 12`; if it has a panel
  it closes behind the player: `doorArrive` = its object + 1, `doorArriveTimer = 16`, counted
  down at the start of each frame (`doorArrive = 0` at 0). Else the first `PlayerStart`. Else
  (32, 32). The player is placed there keeping `facing` and `vx`.
- **Start**: enter the first room (or the one asked for by id) fresh; the random seed is 1;
  place the player at the first `PlayerStart` (facing left if its property `facing` is
  `left`), else at (32, 32).
- **Engine classes** are recognised by the exact class name: `PlayerStart`, `CameraZone`,
  `KillZone`, `Door`, `Trigger`, `SavePoint`. `Trigger` does nothing yet. Properties other
  than those named here are ignored.
- At most 64 objects of a room are read; further ones do not exist for the runtime.
- **Options** at offset 1024 of the save memory: u32 `BMOP`, u8 language (pack-format.md
  §3.2), three zero bytes. Read when the game starts (an unknown language or none: 0), written
  when the language is changed.
- **Save game**: the `save` fields of the state ([state.md](state.md)) at offset 0 of the
  save memory. Loading one sets `vx = vy = 0`, enters the stored room with the scripted
  objects fresh, and equips again (`equip`) what each equipment slot holds.

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
- Background 3 belongs to the UI layer (`src/ui/ui.h`): character block 2, screen block 25,
  priority 0, background palette bank 15, shown only while the UI shows something. Rooms use
  palette banks 0 to 14.

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

**Objects made at run time.** `spawn(class, x, y)`: the Actor class with that name; the place
is a destroyed one after the room's objects (not the object whose script is running), else the
next one, at most 8 in all; else false. The new one starts as a fresh object of the room would,
at `(x, y)` with `home` there, sheet from its class, no room object (it has no `save`
variables and no properties); `spawn_with(class, record)` does that at the caller's anchor and
sets the first variable of type record to the record. Then the event `spawn`. `destroy()`: the
object is gone (hidden, no events, no state code, no hits). The state keeps for each object
whether it was made at run time and whether it is gone; a state loaded in the room brings them
back after the room's objects; entering a room drops them.

**Each frame**, for each object in order (skipping those gone): the event `tick`; the event `touch` if it touches
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
| 28 | `stat(value)` | a stat of the player: the enum value's number picks one of 16; `statBase + statBonus + statEquip` (below); 0 outside |
| 29 | `add_stat(value, amount)` | adds to it, clamped to a 16-bit signed number |
| 30 | `attach(slot, class)` | attaches the Attachment class of that name to the player in slot 0 to 7, replacing what is there (which gets `detached()`); the new one gets `attached()` at once. False if there is no such class or slot ([mondskript.md](mondskript.md) §11, "Attachments") |
| 31 | `attach_for(slot, class, frames)` | the same, detached by itself after that many frames (at the end of a frame's work) |
| 32 | `detach(slot)` | `detached()`, then the slot is empty |
| 33 | `impulse(vx, vy)` | added to the player's speed once (fix) |
| 34 | `room_attach(slot, class)` | like `attach`, in the room's slot 0 to 3; the room detaches it when it is left |
| 35 | `room_detach(slot)` | like `detach`, for the room's slot |
| 36 | `arm(box, damage, element)` | starts a hit of this figure with its hit boxes named `box` (section 10); arming the same name again replaces it with a new hit |
| 37 | `disarm(box)` | ends this figure's hits with that box name |
| 38 | `hit_box(reach, frames, damage, element)` | starts a hit of this figure with a rectangle `reach` (0..255) pixels ahead for `frames` (1..65535) frames, of that element (1 to 3; others 0); a negative `reach` (to -255): that far on both sides |
| 39 | `equip(slot, record)` | slot 0 to 7, record 0 empties it: false if the player has none of the record, or if its structure has roles and `slot` is not the one its role `equip` gives (data-format.md §6; nothing changes). Else `equipped[slot] = record`; the class its first field of type `attach` names (its table found by the key among all `DTAB` chunks) is attached in the player's slot of the same number, its first variable of type record set to the record before `attached()`; without such a class the slot is emptied |
| 40 | `owner_lock(frames)` | from a player's attachment: `lockTimer = frames` (0..65535) |
| 41 | `owner_play_anim(name)` | from a player's attachment: the player's sheet's animation of that name from its start; `animHeld = 2` if `lockTimer != 0`, else 1. Unknown: nothing |
| 42 | `owner_busy()` | from a player's attachment: `lockTimer != 0` |
| 43-49 | `ui_frame(x, y, w, h)`, `ui_label(x, y, text)`, `ui_number(x, y, value, digits)`, `ui_text(x, y, w, text)`, `ui_gauge(x, y, w, percent)`, `ui_clear(x, y, w, h)`, `ui_show(on)` | the UI layer (section 11): a frame, a text in the language shown, a right-aligned number, a text wrapped down to the last row, a gauge, empty cells, the layer on or off |
| 50, 51 | `open_screen(class)`, `close_screen()` | section 11 |
| 52-55 | `language()`, `set_language(i)`, `language_count()`, `language_name(i)` | the language texts are shown in (an option, section 6); setting it redraws every open screen; a name is a string id |
| 56 | `equipped(slot)` | the record in equipment slot 0 to 7, 0 if none |
| 57, 58 | `record_text(record, name)`, `record_int(record, name)` | the field of that name in the record's table, whichever structure (data-format.md §4); 0xFFFF / 0 if there is none |
| 59, 60, 68 | `heal(n)`, `max_health()`, `player_health()` | health += n up to the most; the most (30 + stat 0); the player's health |
| 61, 62 | `spawn(class, x, y)`, `spawn_with(class, record)` | section 8, "Objects made at run time" |
| 63 | `destroy()` | the object is gone (section 8) |
| 64 | `floor_y()` | `FIX` of the top of the first floor at or below `(INT(x), INT(y) - 1)` (jump-through floors count), the room's height if there is none |
| 65, 66 | `say(speaker, line)`, `dialogue_done()` | the dialogue box (section 6) |
| 67 | `player_lock(frames)` | `lockTimer = frames` (0..65535) |
| 69 | `add_resist(element, n)` | element 1 to 3: `resists[element] += n` (0..255); 0 and others: nothing |
| 70 | `set_affinity(weak, resists)` | object only: `affinity = (weak in 1..3 ? 1 << weak : 0) \| (resists in 1..3 ? 16 << resists : 0)` |
| 71 | `strike_back(amount)` | if the figure striking the player now (section 6) is an object that can be struck (section 10): it is struck for `amount` without an element (its affinity applies, `health -=`, 20 frames invulnerable, its hits end, `hit(amount)`); true. Else false |
| 72 | `add_speed(percent)` | `speedPercent += percent` (-90..300) |
| 73-76 | `mana()`, `max_mana()`, `restore_mana(n)`, `spend_mana(n)` | the player's mana; the most (20 + stat 1, at least 0); mana += n up to the most; if `n >= 0` and `mana >= n`: mana -= n and true, else false |
| 77 | `use_item(record)` | false if the player has none of it or its first `attach` field names no Attachment class. Else that class is attached in slot 7 for 1 frame, its first record variable set to the record (it gets `attached()` at once); true |
| 78 | `return_to_save()` | `returnPending = 1` (section 6, "Return") |
| 79, 80 | `quick_slot(i)`, `set_quick_slot(i, record)` | `quick[i]` for i 0 to 3 (0 otherwise); set it |
| 81 | `set_knockback(percent)` | object only: `steady = 100 - percent` (percent clamped to 0..100) |
| 82, 83 | `say_line(record)`, `say_as(speaker, line)` | the dialogue box with a portrait (section 6, "Dialogue") |
| 84 | `fade_music(percent, frames)` | the music's volume to `percent * 256 / 100` (0..100) in `frames` (0..65535); at 0 it stops (sound-format.md, "Dynamic music") |
| 85 | `music_intensity(level)` | the sound engine's intensity (0..15): which layers of the music are heard |
| 86 | `teleport(room, x, y)` | a teleport (section 12) to the room of that id (`#here`: this one) at `x`, `y`; false if there is no such room or a teleport is under way |
| 87 | `screen_effect(name, a, b)` | the room's effect now (section 12): `#none`, `#wave`, `#spotlight`, `#pulse`, `#gradient`; `a`, `b` 0 for the defaults |
| 93 | `lock_move(ground, air)` | for the lock just set (`owner_lock`): `lockGround`, `lockAir` (section 5, "Moving while locked"); out of range: slide, steer |
| 89 | `aim(name)` | the figure's next `hit_box` lies `#Up` above its head, `#Down` below its feet, `#Low` ahead in the lower half of its body, or `#Ahead` (section 10) |
| 90 | `held(name)` | `#Up`, `#Down`, `#Left`, `#Right`, `#Fwd`, `#Back` (by `facing`) or a game button is held now |
| 91 | `on_ground()` | the player stands |
| 92 | `bounce(speed)` | the player's `vy = -speed` (`+speed` with gravity flipped), not on the ground |
| 94 | `ui_map(x, y, w, h)` | the world map into w x h cells of the UI layer ([world-format.md](world-format.md) §4) |
| 95 | `map_percent()` | explored cells of all, in percent |
| 96 | `map_reveal(room)` | the room (its id as a name) is revealed; false if it is not on the map |
| 97 | `set_damage(n)` | in `damage_in` or `damage_out` (section 6, 10): the amount the next filter gets and the strike uses |
| 98 | `damage_is(element)` | in a filter: the damage is of that element |
| 99 | `owner_velocity(vx, vy)` | from one of the player's attachments: the player's `vx`, `vy` (fix) are these now; nothing from anything else |
| 100 | `hit_option(name, n)` | the next hit of the figure: `#Pierce`, `#Reflect`, `#Multi` n, `#Stop` n (section 10) |
| 101 | `hit_knock(x, y)` | the next hit's push (fix) |
| 102 | `fly(vx, vy)` | an object's speed, moved by the engine (section 10) |
| 103 | `fall(g)` | its gravity |
| 104 | `lifetime(frames)` | destroyed (or hidden) after that many frames; 0 for ever |
| 105 | `alive(#Class)` | how many objects of the class are there |
| 106 | `effect(#sprite)` | that sprite's first animation once at the object's anchor (section 12) |
| 107 | `set_palette(#name)` | the object's palette: one of its sprite by name; none of that name: 0 |
| 108 | `flash(frames)` | drawn with bank 13 for that many frames |
| 109, 110 | `sin(a)`, `cos(a)` | fix from a table: `round(256 sin(2 pi a / 256))` for `a & 63` in the first quarter, the rest by symmetry |
| 111 | `angle_to_point(x, y)` | `bmAngleOf(INT(x - fx), INT(y - fy))` from the figure's anchor (an attachment: the player's): 0 for no distance; else within each eighth of a turn from the nearer axis `round(atan(t / 32) / 2 pi * 256)` for `t = (min * 32 + max / 2) / max`, mirrored into place |
| 112 | `layer_aim(#layer, a)` | the layer's `angle = a`, `speed = 0` |
| 113 | `layer_turn(#layer, a, speed)` | `goal = a`, `speed` (0 to 255; 0: `angle = a` now); every frame `angle` turns toward `goal` by at most `speed` the shorter way (half a turn: the positive way) |
| 114 | `layer_angle(#layer)` | its `angle` |
| 115 | `layer_play(#layer, #anim)` | an animation of the layer's sprite by name: played from its start, the layer `played` (its own state, whatever its mode); another name: no longer played (an `aim` layer back on its first direction set). On the root: as `owner_play_anim` |
| 116 | `layer_show(#layer, #sprite)` | the layer shows that sprite (`#default`: the figure's) and is no longer hidden |
| 117 | `layer_hide(#layer)` | not drawn; its pose (and what hangs on it) as before |
| 118 | `layer_palette(#layer, #palette)` | a palette of its sprite by name (`#default` or none of that name: the figure's) |
| 119-121 | `layer_point_x/y/angle(#layer, #point)` | where the point of the layer's current frame is (fix, room pixels) and its world angle; without it: the layer's anchor and 0 (128 facing left) |
| 122 | `spawn_at_point(#Class, #layer, #point)` | `spawn` at that place, the new object's `angle` the point's; it faces left if `64 < angle < 192` |
| 123 | `fly_angle(a, speed)` | `fly(speed * cos(a) >> 8, speed * sin(a) >> 8)` |

| 124 | `handle_valid(h)` | `h` not 0, `h & 255` is `1 +` an object that is not gone, and its `serial == h >> 8` (mondskript.md §11, "Handles") |
| 125 | `nearest_enemy(range, #how)` | the nearest object (by `dist` between the middles of the hitboxes) of another team, not hidden, with health above 0, within `range`; `#InFront`: only those on the side the caller faces (or straight above or below); a handle, or 0 |
| 126 | `distance_to(a)` | `dist` from the caller's middle to `a`'s (the player's middle: its anchor, half its height up) |
| 127 | `angle_to(a)` | `bmAngleOf` from the caller's middle to `a`'s |
| 128 | `owner_face(a)` | from an attachment: the player faces `a`'s side (nothing if straight above or below) |
| 129 | `layer_track(#layer, a, speed)` | the layer's `track = a`, `speed`; every frame (before it turns) its `goal` is the angle from the layer's anchor to `a`'s middle; once `a` is gone `track = 0` and the layer stays where it got to. `layer_aim` and `layer_turn` end it |
| 130 | `marker_show(a, #sprite)` | that sprite's first animation is drawn at `a`'s middle (at time `frame`, priority 0, before the effects: in front of everything but the portrait) until `marker_hide` or `a` is gone (`markerTarget`, `markerSprite`: snapshot state) |
| 131 | `marker_hide()` | no marker |
| 132 | `self()` | a handle to the calling object (0 from an attachment) |

Layer functions (112-122) work only from the player's attachments, on the player's figure (a
layer named by the figure file); from an object, or for a layer the figure does not have,
they do nothing and read 0.
| 88 | `flip_gravity()` | the player's gravity turns over or back (section 5, "Gravity flipped"): `flipped` toggled, `vy = 0`, not on the ground, no coyote time; true if it now pulls up |

`add_stat` (29) from one of the player's attachments changes `statBonus`, not `statBase`
(state.md §4): what an attachment adds while it is attached, not saved. The runtime notes it
per slot (`attachments.statOf`, `statAdded`: up to 4 stats a slot; a fifth is added but not
noted) and takes it away again when the slot is emptied, after `detached()`. `statEquip` is what the equipped records of structures with the role
`stat_source` add ([data-format.md](data-format.md) §6), worked out again after every
`equip`, a loaded state and a pushed pack. `stat` (28) reads `statBase + statBonus +
statEquip`.

`arm`, `disarm` and `hit_box` from one of the player's attachments act for the player and
remember the slot; from a room's attachment they do nothing.

`dist(a, b) = max(|a|, |b|) + min(|a|, |b|) / 2`.

Actor variables by index: 0 `x`, 1 `y`, 2 `home_x`, 3 `home_y`, 4 `facing`, 5 `health`, 6
`team`, 7 `weight` (1 when it starts), 8 `angle` (0 when it starts; `spawn_at_point` sets it).
An object made at run time keeps where it was made (`home_x`, `home_y`) in the state
(`spawnHomeX/Y`); before, a loaded state put its home where it was. Writing `facing` stores -1 for a negative value, else 1. `home_x` and `home_y` cannot
be written. `health` and `team` start at 0 when an object starts anew; `team` keeps the low 8
bits. An attachment of the player reads the player's `x`, `y`, `facing`, `health` and `team`
(1) and can write only `health`, clamped to 1..30; one of the room reads the camera's `x`,
`y`, `facing = 1`, `team = 1`, and 0 for the rest.


## 9. Sprites on screen

**Loading** ([../plans/layered-sprites.md](../plans/layered-sprites.md) §2, §3). Once per pack
every `SPRT` chunk is opened in pack order, at most 24 (an invalid one is skipped): what logic
reads (frames, boxes, animations) is there from then on. Sprite memory holds the built-in figure
(tiles 0-7, bank 14) and the placeholder frame (tiles 8-11, bank 15), then the player's sheets
(the sheets of its figure's layers in layer order, each once, in banks 0, 1, ... with the
palette its layer names; or the sheet `player` in bank 0), which stay; the rest is given out
room by room. When a room is entered, every other sheet is forgotten and the other banks up to
12 are free; then the room's objects' sheets (in the palettes
their objects have), door panels and the puzzle's are loaded. A sheet not loaded yet is loaded
the first time it is drawn (an effect, an object made at run time): its tiles after the last
loaded ones (a sheet that would pass tile 1024 is not drawn), and each (sheet, palette) pair
takes the lowest free bank of 0 to 12, its 16 colours that palette's. One that does not fit
(tiles or a bank) while sheets the room has not drawn are still there from earlier rooms:
those are forgotten, the pairs the room has drawn since it was entered are loaded again (at
new places: what was given out earlier in that frame may show other tiles for one frame), then
this one. Still no room: a bank with another palette of the sheet; none: not drawn. What did
not fit is counted (`spriteMisses`). When a room is entered, the sheets it needs (its objects',
the puzzle's, the grapple's if it has anchors, the extra layers of kinds) are counted first;
only if they do not fit beside what is loaded is everything but the player's forgotten. Which sheet is where is derived, not state. Sprite tiles are mapped
one-dimensionally.

**Animation state**: `anim`, `finished`, `step`, `timer`.

- `play(anim)`: nothing if it is the animation playing; else `step = 0`, `timer = 0`,
  `finished = 0`.
- `tick`: nothing if the animation does not exist or has no steps. If `step >= count`:
  `step = 0`. `timer += 1`; if `timer < duration of the step`: done. Else `timer = 0` and: the
  next step if there is one; else `step = 0` if the animation loops; else `finished = 1`.
- The frame shown is the step's frame.

**The player** is the figure named `player` (FIGR, sprite-format.md "Figures") if the pack has
one whose root layer's sprite is there; else the sheet named exactly `player`. Its sheet below
is the root layer's: animations `idle`, `run`, `jump`, `fall`,
`climb`, `push` (a missing one is animation 0; `push`: `run`). Climbing: `climb`, ticking only
while `vx` or `vy` is not 0. Else in the air: `jump` if `vy < 0`, else `fall`. Else `push` while
pushing a push block (room-format.md §3.9). Else `run` if `|vx| > 32/256`. Else `idle`. Play,
then tick. Then each layer of the figure, in order: its `angle` turns toward `goal` by `speed`
(if not 0); then, but for the root: an `own` layer that no script plays plays the animation of
its sprite named like the root's current one (nothing if it has none); an `own` or `aim` layer
or one a script plays (`layer_play`) ticks its own state (a direction set does not change); a
`sync` layer has no state of its own. Each layer's state (`playerLayers`: animation state,
sprite shown instead, palette, angle, goal, speed, hidden and played) is snapshot state; an
`aim` layer starts on its sprite's first direction set.

**A figure's pose** (`bmFigurePose`), layer by layer in order. The root is at the figure's
position with its animation state. Another layer starts at its parent's place; a `sync` layer
shows the animation of its sprite named like the parent's current one at the parent's step
(no such animation: the layer is not drawn), an `own` layer its own state; if the layer names a
point and the parent's current frame has it, the layer is moved by the point (mirrored with the
figure: `x` negated facing left, `y` negated upside down) and takes its angle. A layer whose
animation is a **direction set** shows the step whose angle is nearest to its own angle as
drawn: the world angle `a` turned into the drawing's, `128 - a` facing left, then `256 - a`
upside down (a tie: the earlier step). A layer without
a sprite is not drawn; what hangs on it is placed at its place. **Order**, back to front: each
layer in front of all before it, or, if it is `behind` or its point says `behind` on this
frame, right behind its parent.

**Drawing a frame** at screen position `(sx, sy)` = position minus camera: for each piece,
`x = hflip ? sx - dx - w : sx + dx`, `y = sy + dy`, with the piece's size from the hardware
table (shape 0: 8, 16, 32, 64 square; shape 1 wide: 16x8, 32x8, 32x16, 64x32; shape 2 tall:
8x16, 8x32, 16x32, 32x64). A piece entirely off the screen takes no hardware sprite. Tile =
the sheet's first tile + the piece's tile; palette bank = the bank of the sheet in the
figure's palette (an object's `palette`: the room object's property `palette`, a palette of its
sprite by name, or `set_palette`; else 0, the sheet's own); priority 1.

**Order of the 48 hardware sprites used:**

1. The player: its frame, mirrored when `facing < 0`, at most 8 pieces; a figure: its layers
   front to back, each at the figure's position plus its offset, in the bank of its sheet and
   palette (bmGameSheetBank), but hidden ones, 12 pieces in all; not drawn while
   `hurtTimer & 4` is not 0. Without a `player` sheet: the built-in 16 x 32 figure at
   `(px - 8, py - 32)`.
2. Scripted objects in order, unless hidden: their frame at `(INT(x), INT(y))`, mirrored when
   `facing < 0`. Without a sheet, and only for objects without width and height: the
   placeholder at `(INT(x) - 8, INT(y) - 16)`.
3. Door panels (section 12, "Doors"): affine sprites.
4. Unscripted objects of other classes and `SavePoint`s with a class name: with a sheet named
   like the class, animation 0 at time `frame` (the frame whose step contains `frame modulo
   the animation's total duration`), never mirrored; else the placeholder at `(x - 8, y)` for
   an object with a size, `(x - 8, y - 16)` for a point.
5. The rest are hidden.

## 10. Hits

Figures are numbered: 0 the player, `1 + i` scripted object `i`. A figure's **team** is 1 for
the player and the variable `team` for an object. **Hits**: 8 entries, each source figure
(0xFF: free), the attachment slot that made it (0xFF: none), box name (0xFFFF: a rectangle),
damage, element, the source's animation when armed, frames left, reach, the figures it
struck (a bit each), and its options: flags (1 pierce, 2 reflect), `multi` and its counter,
`stop`, `knockX`, `knockY` (fix). `arm` takes the entry of the same source and name if there
is one, else the first free; a hit started when all are used is dropped. A room entered fresh
has none. **Options** come from the source figure's next-hit options (`hit_option(#Pierce |
#Reflect, 0)`, `hit_option(#Multi | #Stop, n)` with n 0 to 255, `hit_knock(x, y)`; from an
object for itself, from the player's attachments for the player, nothing from the room's),
taken into the entry when it is armed (`arm` or `hit_box`) and set back to none.

**Each frame**, for each entry in order that is not free:

1. An armed hit (with a box name) ends if its source no longer exists, or its source has a
   sheet and shows another animation than the one it was armed in, or that animation does not
   loop and has finished.
2. Its rectangles: for a box name, the source's boxes of kind hit with that name on the frame
   showing (sprite-format.md, mirrored when `facing < 0`), relative to its anchor; none if it
   has no sheet. For a rectangle, by its aim (`aim()` before `hit_box`; per figure `aimNext`,
   taken into the entry when it is armed and set back to ahead), with `ax = INT(x)` of the
   source, its body box from `top` to `bottom` and `r = |reach|`:
   - ahead: `[ax, ax + reach)` (`[ax - reach, ax)` when facing left; `[ax - r, ax + r)` for a
     negative reach), over the height of the body box;
   - up: `[ax - 10, ax + 10)` x `[top - r, top + 8)`;
   - down: `[ax - 10, ax + 10)` x `[bottom - 8, bottom + r)`;
   - low: as ahead, over the lower half of the body box (`[top + h / 2, bottom)`).
   For an object, every rectangle is then stretched back along its speed: by `INT(vx)` to the
   left when `vx > 0` (to the right when less), the same with `vy` (a projectile does not pass
   through what lay between).
3. For each figure `f` in number order: skip the source, a figure of the same team, one
   already struck by this hit, and one that cannot be struck now (the player: `hurtTimer !=
   0`; an object: hidden, `health <= 0`, or invulnerable) unless the hit reflects and `f` is a
   projectile (an object made at run time whose `vx` or `vy` is not 0). Its hurt rectangles are
   its boxes of kind hurt on the frame showing, or its body box (the player's box of section 3,
   an object's box of section 8) if there are none; its guard rectangles its boxes of kind
   guard on the frame showing, none if the hit pierces. The hit's origin is the middle of the
   source's body box, for an object less `(INT(vx), INT(vy))` (where it was at the start of the
   frame), taken once per hit and frame. For each hit rectangle and hurt rectangle that
   overlap, in order, the line from the origin to the middle of their overlap (in half pixels)
   is tested against each guard rectangle: it crosses one if their bounds overlap and the
   rectangle's corners (right and bottom one half pixel inside) do not all lie strictly on one
   side of it. The first overlap whose line crosses none: `f` is struck. If there is none, but
   some overlap was found or a hit rectangle overlaps a guard rectangle, the hit is
   **blocked**: the bit of `f` is set, `f` gets `blocked(amount)` (the player: each of their
   attachments), the source object `guarded(amount)` and so does the attachment in the entry's
   slot (or the one that made the projectile), and a spark starts (`bmGameSpark`, not state: the
   pack's sprite `spark`, its first animation once, at the middle of the first overlap of a
   hit rectangle and a guard rectangle, else on the first guard rectangle's edge facing the
   origin at the origin's height kept inside it); then the next figure. Struck,
   `amount = max(1, damage)`: the bit of `f` is set; a hit that reflects and a projectile `f`: `f` turns (`vx`, `vy`, `facing` negated), takes the source's team, its maker
   becomes the source (and its attachment), its own hits' struck bits are cleared, and nothing
   more happens. A projectile's maker is the owner of its hits: if the hit is the
   player's, their attachments in slot order get `damage_out(amount)` (as `damage_in`, section
   6) and with 0 or less left nothing more happens; the player gets Strike
   (amount, `FIX(INT(source x))`, the hit's element, the source) (section 6); an object takes
   the element rule first: `amount * 3 / 2` if its `affinity` has bit `element` (weak),
   `amount / 2` if it has bit `4 + element` (resists), at least 1, element 0 unchanged; then
   `health -= amount`, 20 frames invulnerable, `knock = ±2.0 * (100 - steady) / 100` away
   from the source's `FIX(INT(x))` (+ if the object's `x` is not less), its hits end, and it
   gets `hit(amount)`. With `stop != 0`, `hitStop = max(hitStop, stop)`. A hit with its own push
   uses `knockX` instead of 2.0 for an object and `knockX`, `knockY` instead of 2.0, 3.0 for the
   player; a `#Multi` hit leaves an object invulnerable only `min(20, multi)` frames. Then the
   source hears of it: a source object gets `struck(amount)`, and the attachment in the entry's
   slot (for a projectile, the attachment that made it) gets `struck(amount)` too. If the entry
   is no longer the one it was (freed or replaced by those events), stop with it.
4. With `multi != 0`: `multiCount += 1`; when it reaches `multi`, it is 0 again and the struck
   bits are cleared (it strikes the same figures again).
5. A rectangle whose entry is still the same: `frames -= 1`, at 0 it is free.

Then every object's invulnerability goes down by 1 if not 0.

**The push** (`knock`), once a frame right after the scripted objects' frame (section 3): for
each object with `knock != 0`: if it is gone or hidden, `knock = 0`; else if the collision is
solid for sides at `(INT(x + knock) ± 6, INT(y) - 4)` or `(…, INT(y) - 12)` (+ when pushed
right), `knock = 0`; else `x += knock` and `knock` moves 0.25 towards 0 (to 0 at most).

**Hit stop**: while `hitStop != 0` at the start of a game frame (after the frame count and the
buttons), it goes down by 1 and the frame only draws the sprites, presents the UI layer and
mixes the sound: nothing moves.

**Objects the engine moves** (`fly(vx, vy)`, `fall(g)`, `lifetime(frames)`, from an object
for itself), once a frame after the scripted objects (step 8 of section 3), in index order,
not gone: if `life != 0` it goes down by 1, and at 0 the object is destroyed (made at run
time) or hidden, and nothing more. Else, if `vx`, `vy` or `fall` is not 0: `vy += fall`, both
clamped to ±16.0, `x += vx`, `y += vy`; if the collision is solid for sides at the middle of
its box (section 8), it gets `wall()`. **Made at run time** (`spawn`, `spawn_with`): its team
is its maker's (an object's `team`, the player's 1 from one of their attachments, 0 from the
room), and its maker and the maker's attachment slot are kept (`madeBy`, `madeBySlot`).
`alive(#Class)` counts the objects of that class that are not gone.

**The flash**: an object with a sheet whose `invulnerable > 12` (the first 8 frames after it
was struck, or while its `flash(frames)` lasts) is drawn with sprite palette bank 13, whose 16
colours are all white (31, 31, 31),
instead of its sheet's bank.

## 11. Screens and the UI layer

**The UI layer** (`src/ui/ui.h`): background 3, character block 2, screen block 25, priority 0,
background palette bank 15, 30 x 20 cells of 8 x 8. Tiles: 0 empty, 1-9 a frame's pieces, 10-18
a gauge cell from empty to full in eighths (red), 19-27 the same in the second gauge colour
(blue), 32-255 a cache of glyphs (pack-format.md §3.1), each
on a transparent ground or on a frame's inside, and of the world map's cells (world-format.md
§4); a glyph is made when it is drawn, a tile no cell shows is taken again when the cache is
full. Palette: 1 ink, 2 its shadow, 3 a frame's inside, 4 and 5 its border, 6-8 the gauges, 9-13
the map's colours. Texts are shown in the language chosen. The layer
is presentation: not in the state. After a state is loaded it is cleared and hidden, the blend
switched off, the dialogue box drawn again if `dialogue != 0`, and the screens redraw.

**Screens** (classes that extend `Screen`): a stack of at most 4 (`count`, per place class key,
interpreter state, 8 variables; snapshot). `open_screen(name)`: the Screen class with that name
(by the FNV-1a of its name) on top, fresh, false if there is none or the stack is full; the
first one darkens the room (BLDCNT: darken BG0-2, sprites and the backdrop, BLDY 9) and shows the
layer; the new one gets `opened()`. `close_screen()`: the top one gets `closed()`, then is
removed; if one is left, every open screen redraws (`opened()` from the bottom, the layer
cleared first); if none, the layer is cleared, hidden unless a dialogue is shown, and the blend
back to the UI's (below).

**The UI see-through** (`bmUiBlend`): whenever nothing else uses the colour effect (no screen
open, the room's effect none of spotlight, pulse, gradient or a teleport), BLDCNT is alpha
blending with BG3 (the UI layer) as first target and BG0-2, sprites and the backdrop as
second, EVA 16, EVB 8: every box and background of the UI shows what is behind it at half
strength added to its own colours, so the room shows through, dimmed, and white text and
frames stay white. A room effect that blends takes the effect over while it lasts (the UI is
opaque then), and an open screen darkens the room instead.

**The HUD and notices** (in a game frame without a screen, after the sprites, before the
layer is presented): unless the debug overlay is shown (it takes the place), a frame of 13 x 4
cells at (0, 0) with `HP` at (1, 1) and `MP` at (1, 2), gauges of 5 cells at (4, 1) (health of
the most, red) and (4, 2) (mana of the most, blue) and the numbers in 3 cells at (9, 1) and
(9, 2), drawn again only where a value changed, and the world map's small map at (23, 0) if
the pack's map has one ([world-format.md](world-format.md) §4); the layer is shown. When the layer was cleared
(a state loaded, the last screen closed, the overlay hidden) all of it is drawn again.
`give(record, n)` and `give_item(name)` that add something set `pickupSound`: at the start of
the next frame (after `returnPending`) the effect `pickup` plays, if the pack has it.
They also set the **notice**: the record (its
`title` is looked up later), the name as a fallback, the amount, 120 frames. Each such frame
counts it down; while the dialogue box is shown nothing more happens. In the frame it was set
nothing is drawn, in the next the title is looked up, in the one after a frame of `w x 3` cells
at (0, 17) is drawn with the title (or the name, `_` as spaces and the first letter upper case)
at (1, 18), followed by ` xN` if more than one came; `w` = its characters + 2, at most 30. At 0
the cells are cleared. Entering a room fresh (not when a state is loaded) ends a notice
(`noticeTimer = 1`); a room with the property `title` then sets its own (key 0, the title as
the name). (The steps keep the GBA in its frame: the frame an item comes has its
scripts' work, a title lookup and new glyphs cost time.)

**A screens' frame**: buttons that changed go to the top screen as `press(name)` or
`release(name)`, in the order Up, Down, Left, Right, A, B, Start, Select, L, R (names `#Up` ...
`#R`); a direction held with no direction newly pressed counts `repeat` up and is pressed again
at 15 and every 5 after (else `repeat = 0`). Then each screen from the bottom gets `tick()` and
one step of its state code. Variables a screen reads (`x`, `y`, `facing`, `health`, `team`) are
the player's; it writes none.

## 12. Screen effects and the teleport

`src/world/effects.h`. A room's effect comes from the map properties `effect`, `effect_a`,
`effect_b` (room-format.md) when the room is entered fresh, or from `screen_effect`; state
`fxKind`, `fxA`, `fxB` (0 for `a` or `b` takes the effect's default). After the sprites each
frame the effect is presented; the UI layer (BG3) never takes part.

| effect | a (default) | b (default) | what |
| --- | --- | --- | --- |
| `wave` | amplitude in px, 0..8 (6) | speed (3) | the room's backgrounds sway per line: HOFS of each = its scroll + `a * (sin(y * 4 + frame * b) + 64) / 128` (sin of 256 to a turn, -64..64), a line table per background (slots 0-2). The player's sprite moves with the line at its feet. Only 0..8 px to the right of the scroll: the map has spare columns only right of the screen |
| `spotlight` | radius in px, 8..120 (44) | darkness 0..16 (12) | window 0 a circle around the player (a WIN0H span per line, slot 0); outside it the room and sprites are darkened |
| `pulse` | strength 0..16 (8) | speed (2) | the room brightened, then darkened: `sin(frame * b) * a / 64` (BLDY) |
| `gradient` | darkness at the top 0..16 (10) | – | dusk: BLDY per line (slot 2) from `a` at the top to 0 at line 96, with a slight shimmer |

Line-table slots no longer used are switched off; the windows and blend are set each frame
while a screen is not open (with no effect of the room's: the UI see-through, section above).

**Teleport** (`bmEffectsTeleport`, `teleport`, the Return Feather): `teleTimer = 64`, the target
in `teleRoom`, `teleX`, `teleY`, the sound `teleport`. At the start of each frame, while it
lasts: at 64 the screens close; it counts down; `lockTimer` and `hurtTimer` are kept at least
2, the player's speed 0; at 32 the player is moved (another room entered as through a door,
`roomChanges += 1`; placed keeping `facing`; `doorCooldown = 20`; the camera follows). Shown
in place of the room's effect: the wave at 8 px times the phase (0 at the ends, 1 at 32), speed
6, and the room brightened up to 14 by the phase.

**Doors** (`bmEffectsDrawDoors`): a `Door` with the property `panel` (a sprite's name, its
anchor the hinge at the bottom left, its hitbox the panel's size, frame 0's) is drawn as a
panel turning about a vertical hinge, only scaled in x, so that it keeps standing on the
ground. How open it is, `open` 0..256: for the door being walked through (section 6) `doorTimer
* 256 / 12`, 256 from 12 on; for the door closing behind the player `doorArriveTimer * 256 / 16`;
any other is shut (0). The panel's angle `a` from the screen's x axis (256 to a turn):
`direction` east `64 + open * 56 / 256` (closed along the depth in the side wall, open swung
into the room, to the left), west `64 - open * 56 / 256`, else `open * 52 / 256` (closed facing
the screen, open turned away).
A point `u` along the panel and `v` up it is drawn at `(hinge x + u * xs, ground + v * scale)`
with `scale` = the door's height / the panel's and `xs = scale * cos a`, for east and west
`scale * (cos a + 0.4 sin a)` (a side wall's depth shows 0.4 wide, 102/256): the scale of an
affine sprite's matrix. The ground is the floor under the door's middle, searched from its top
to 32 px below its bottom (platforms count), else its bottom. The hinge is at its left for north
doors and `closed width` (`panel width * scale * 0.4`) left of its right edge for east and west.
Behind a north door the panel sprite's frame 1, if it has one, is drawn at the hinge with x
scale `scale` (the dark of the opening; after the panel in OAM, so behind it). Each door's
pieces share a matrix, the opening its own (the doors in object order, matrices from 0), double
size, priority 1. Doors more than 64 px off screen are skipped. The panel's sprite, the direction and
the ground are found when the room's sprites are bound.

**The light** (`src/world/light.h`, [room-format.md](room-format.md) §1 "Light"): when a room
is shown its `LITE` is read (none: not lit; the sprite banks then get their own colours back
if the last room was lit). Every frame after the effects:

- The intensity: `intensity × (1 + flicker × n)`, n smoothed value noise (-1..1) from a hash of
  `frame / flicker_speed` plus a second octave at twice the speed and a quarter of the weight.
  A pure function of the frame counter: nothing of the light is state.
- `palette`: every background bank with lit entries (all of them on entering) gets
  `colour × min(1, ambient + intensity × light colour × level / (levels - 1))`, per channel in
  sixteenths of 256, rounded.
- `layer`: on entering the room's banks are darkened to the ambient; every frame each bank of
  the light layer gets `intensity × gain × light colour × v` per value, v its fitted level or,
  with normals, `max(0, N·F)` (worked out once on entering). The light layer is
  shown and blended (first targets the UI layer and the light layer, second the rest, EVA 16,
  EVB 16: the UI's boxes are then added at full weight too) while no screen is open, no room
  effect and no teleport; else it is hidden. Behind the UI's boxes it is left out: windows 0
  and 1 cover them (`bmUiBoxes`: the bands of rows the UI draws in, merged to two), and inside
  them every layer shows but the light layer. Else a box is blended with the room where the
  light layer is empty and not at all where it is not, and the light shows through in patches.
- Only with `BM_LIGHT_EXPERIMENTAL` (CMake option, off): the light layer of normals and the
  streamed and hybrid ones below; without it a room lit so is not lit, and the GBA has no
  palette stream.
- `layer` with the light streamed: each frame the palette stream (`bmVideoSetPaletteStream`)
  is pointed at the row of the camera's top line, so line y of the screen is drawn with the
  light layer's banks of room row `cameraY + y`. The flicker is the light layer's blend weight:
  EVA = 16 × intensity / (intensity × (1 + flicker)); the UI layer is then not blended (one
  weight for both). Hidden, the stream is off. On the GBA the stream is DMA channel 3 in
  H-blank mode (the line table slot 1's channel, which lit rooms do not use) from the pack's
  rows: 112 colours a line in the torch hall, about 48,000 cycles a frame the CPU waits for
  it. The multiboot images have no light at all.
- `layer`, `hybrid`: the stream as above, but over the streamed banks only (32 colours a
  line in the torch hall), the fixed banks written once on entering the room. The light layer's
  blend weight is set per line: line table slot 0 writes BLDALPHA (EVA = the row's weight × the
  flicker, EVB 16) from a table of 160 entries built again only when the camera's row or the
  flicker changed (one division a frame). EVA has 17 steps, so the rounding error of each
  line is carried on to the next (error diffusion): neighbouring lines alternate between two
  steps and average out to the weight, rather than showing as horizontal bands. Lit rooms have no room effect that would use slot 0.
- Several lights (up to 4) flicker together: the tiles have their sum baked in, and a
  sprite slot sums `max(0, N·L) × reach` over the lights. A sheet whose every slot is
  emissive is written once. Shadow rays are kept for the last 8 places (4 px steps) they
  started from.
- Sprites: every sprite bank (0-12) in use is lit for one figure: the player's banks for the
  player (its middle 14 px above its feet), another bank for the nearest actor drawn with its
  sheet (8 px above its anchor), none: ambient only. The light reaching it is
  `(1 - d²/r²)²`, 0 beyond the radius or when a line from it to the light crosses a solid
  collision cell; each slot is `colour × min(1, ambient + light × max(0, N·L))` with L toward
  the light raised by its height, emissive slots as drawn, shiny slots plus `(N·H)^8` of the
  light's colour (H halfway between L and the viewer). A sheet without normals faces the
  viewer. A bank out of the light is written once while it stays out.
- Mode `palette`: the room's banks are shaded again only when the light's intensity changed
  since the last frame, so a light that does not flicker costs nothing after the first frame
  (the rural street's lamp: about 250 cycles a frame instead of 40,700 for its 15 banks). Pixels
  black in the normal map are emissive: their entries are written as drawn.
- The multiboot images have no light (their packs have no lit room; one pushed to them is shown
  unlit). On the GBA a torch hall's light costs about 37,000 cycles a frame (`bm_light_cycles`:
  the room's banks, the sprites', the blend).

**Puddles** (`reflect.h`): a room's first `Puddle` object (room-format.md §5) mirrors the room's
background layer. Where the room leaves the puddle empty in every layer, a free background (the
one after the room's layers, so a room of at most two layers without a light layer) shows that
layer's map and tiles, at its priority behind it (the lower background wins the tie), so nothing
else of it is seen. Line table slot 1 sets its vertical scroll per line: line d of the puddle (d
lines under the water line y) shows room row `y - 1 - d × squash`, wavering by `ripple` pixels
(a sine of d and the frame). Slot 0 sets BLDALPHA per line: the puddle's lines blend the mirror
(a first target, with the UI) over the backdrop, the water's colour, at `strength`, 16 -
strength / 2; the other lines keep the UI's weights. The player standing over the puddle is
drawn a second time, mirrored in the water line and squeezed by the same `squash`: the pieces
of its figure as semi-transparent affine sprites (matrix 31; the doors use the others) at
priority 2, behind the room's main layer, so only the puddle shows them. While a room effect or
a teleport uses the line tables the mirror is hidden. The multiboot images have no puddles
(light_off.c). On the GBA the rural street takes about 130 lines of a frame.

**Debug**: holding L and R and pressing Start gives the record `DebugMirror` if the pack has
it and the player has none (the menu does not open then). The test level's mirror teleports to
the debug lobby.

## 13. Commands

`src/world/commands.h`. A record of the structure named `Command` is a special move: its fields
`input` (name), `cost` (int, mana), `weapon` and `charm` (records), `behaviour` (attach),
`frames` (int), found by name. It is the player's while `equipped[0]` is its `weapon`, or while
its `charm` is equipped in slot 1 to 7.

**History** (in a game frame, before the attachments, also during dialogue): the d-pad's
direction as on a numpad (5 none, 6 right, 8 up, 3 down-right) is compared with the newest in
`cmdDirs`; if it differs it is added with `cmdTimes = frame & 0xFFFF` (a ring of 8:
`cmdHead`, `cmdCount` up to 8).

**Input**: `_`-separated tokens, the last a game button (`Attack`, `Jump`, `Dash`, `Spell`,
`Use`, attachments.h), the others directions: `Up` 8, `Down` 2, `Fwd` 6, `Back` 4, `UpFwd` 9,
`UpBack` 7, `DownFwd` 3, `DownBack` 1; mirrored (6 and 4 swap, and so on) if the newest
direction in the history points left (1, 4, 7), or if it points neither left nor right and
`facing < 0` (the body turns only after the commands). At least one direction, at most 8.

**Matching**, for a game button newly pressed while there is no dialogue, no lock, no teleport,
at least one direction in the history and the newest at most 10 frames old: going from the
newest entry back, each entry must be at most 10 frames older than the press (until the last
direction is found), then at most 12 older than the entry that matched the direction after
it; an entry equal to the direction wanted matches it and the next older direction is wanted;
others are passed over. All found: the input matches. Among the player's commands whose input
matches for this button, the one with most directions (the first of equals in table order).

**Doing it**: if `cost > mana`, nothing (the button does what it does). Else its `behaviour`
is attached in slot `BM_GAME_COMMAND_SLOT` (6) for `frames` frames (at least 1), its first
record variable the command; `mana -= cost`, `cmdLast` = its key, `cmdDone += 1`; the button is
not pressed for the attachments this frame (the weapon does not swing). Before it is
attached the player faces the way the input was read (the lock the behaviour takes would keep
the body from turning). The table and its
fields are found when a room is loaded.

## 14. What is not specified here

Sound ([sound-format.md](sound-format.md)), the script interpreter
([mondskript.md](mondskript.md)), the link ([link-protocol.md](link-protocol.md)) and the
hardware ([gba-backend.md](gba-backend.md)) have their own documents. The autopilot
(`apps/game/autopilot.c`) is a test player, not part of the core.
