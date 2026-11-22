# World map

Version 1.

The world map says where the rooms of a game lie on a grid of screens, as in Metroid or
Castlevania. The game draws it on a map screen (`ui_map`) and as a small map in the HUD; the
cells the player has been in are explored and kept in the save game. The editor's **World**
tab places the rooms. Implemented in `tools/cooker/world_asset.h` (format, layout, chunk),
`src/world/map.h` (runtime) and `editor/world_panel.h` (the tab).

## 1. Source: `.bmworld`

One file per project, in an asset folder ([project-format.md](project-format.md)); `bmcook`
takes it like any other asset. Without one the pack has no map.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<world version="1" cell_w="240" cell_h="160">
 <room id="test_room" x="0" y="0"/>
 <room id="debug_lobby" hidden="true"/>
</world>
```

| Element | Attribute | Default | Meaning |
|---|---|---|---|
| `world` | `version` | 1 | other versions are rejected |
| | `cell_w`, `cell_h` | 240, 160 | pixels of a room that make one map cell (one GBA screen); at least 8 |
| | `minimap` | `true` | the HUD shows a small map around the player (section 4) |
| `room` | `id` | – | a room id ([room-format.md](room-format.md) §1) |
| | `x`, `y` | – | the cell of the room's top left corner; both or neither. Without them the room is placed by its doors (section 2) |
| | `hidden` | `false` | the room is never on the map (debug rooms, a title screen) |

Rooms the file does not name are placed by their doors too. A room the file names that is not
part of a cook is ignored (the cooker reports it). The editor writes the file with the rooms in
its order and only the attributes that are set.

## 2. Layout

The cooker (and the editor, with the same code) lays the map out from the file and the rooms:

- **Size.** A room covers `round(width / cell_w)` x `round(height / cell_h)` cells, at least 1
  each way: a room of 1024 x 192 pixels is 4 x 1 cells. A pixel `p` of a room `size` pixels
  and `n` cells across lies in cell `p * n / size` (the last cell for the rest).
- **Placed by the file**: at its `x`, `y`.
- **Without any placed room** the first room of the cook (not hidden) is at (0, 0).
- **Placed by doors.** Repeatedly, until nothing changes: for each placed room in cook order
  and each of its doors at an edge whose target room is not placed and not hidden and whose
  `target_door` is at an edge of that room too, the target room is placed so that its door's
  cell lies next to this door's cell on the door's side. If that would overlap a placed room,
  this door does not place it (another may).
- **The side of a door**: `direction` `east` or `west` say it; otherwise a door within 16 pixels
  of the top edge is on the north side, of the bottom edge on the south side (unless its
  `direction` is `north`: a door in the back wall, gone through with Up, runtime-core.md §6),
  of the left or right edge on that side. Any other door is inside the room (no side): it does
  not place rooms and makes no gap in a wall.
- **The cell of a door**: the cell its middle is in, moved to the edge of its side.
- **Cells.** Each cell of a room has walls on the sides where the room ends, and a door on the
  side of each door at an edge in it. Cells inside a room are open to each other.
- **Warnings** (in the report and the World tab): a door at an edge whose target room is on the
  map but not in the cell next to it on that side; two rooms that overlap. Rooms that are not on
  the map and not hidden are reported as information.
- **Limits**: at most 64 rooms and 512 cells on a map (the explored cells are a bit each in the
  save game), at most 255 cells across a room. More is an error.

## 3. Cooked form: `WRLD`

One chunk, group 0, category level. Little endian.

```
'WRLD'  u8  version;                 // 1
        u8  flags;                   // bit 0: the HUD's small map
        u16 room_count;
        u16 cell_w, cell_h;          // pixels per cell
        s16 min_x, min_y;            // the map's bounds: the top left cell of all rooms
        u16 cols, rows;              // and its size in cells
        u16 cell_count;              // cells of all rooms
        u16 reserved;
        struct {                     // 12 bytes, room_count times, in the order of the cook
          u16 room;                  // the pack's room index (the order of the ROOM chunks)
          s16 x, y;                  // its top left cell
          u8  w, h;                  // cells
          u16 first_cell;            // its cells' place in the cell table
          u8  area;                  // the area group of the room (pack-format.md §2)
          u8  reserved;
        } rooms[room_count];
        u8 cells[cell_count];        // per room, row by row: walls (bits 0-3: north, east,
                                     // south, west) and doors in them (bits 4-7, the same order)
                                     // then zeros to a multiple of 4
```

Only rooms on the map have an entry. The runtime ignores a chunk with another version, more
than 64 rooms or 512 cells, or cells outside the table.

## 4. In the game

- **The current room** is found when a room is entered (by its pack index); a room without an
  entry is not on the map.
- **Explored.** In every game frame, at the end of step 9 of [runtime-core.md](runtime-core.md)
  §3 (after the scripted objects, the doors and a fall out of the room), the cell of the current room the player's feet are in
  (x, y - 1, by the rule of section 2 with the room's size, clamped to the room) is explored:
  bit `cell % 8` of `mapExplored[cell / 8]`, cells numbered by the cell table. Its 64 bytes are
  in the save game and the link state ([state.md](state.md)).
- **Revealed.** `map_reveal(#room_id)` sets the room's bit in `mapRevealed` (8 bytes, by its
  entry's number; save game, link): its cells are shown without being explored (a map station).
- **The look of a cell**: explored, it is filled with the room colour, lighter for the room the
  player is in; revealed only, grey; otherwise it is not drawn. The walls are drawn as lines on
  the cell's edges, a door as four pixels in the middle of its wall in orange; the player's
  cell has a yellow square in the middle. The UI layer ([runtime-core.md](runtime-core.md) §11)
  makes these 8 x 8 tiles when they are drawn and keeps them in its glyph cache (as the codes
  U+E000 and up), colours 9 to 13 of the UI palette.
- **The map screen**: `ui_map(x, y, w, h)` fills w x h cells of the UI layer from (x, y) with
  the frame's inside and draws the map into them: centred on the player's cell, or, if the map
  is narrower (lower) than the rectangle, the whole map in its middle that way. The player's
  marker is shown while `(frame / 16) % 2 == 0`: a screen that calls `ui_map` again every few
  frames makes it blink. `map_percent()` is the explored cells of all, in percent (rounded
  down).
- **The HUD's small map** (if the chunk's flag is set): a frame of 7 x 5 cells at (23, 0) with 5
  x 3 cells of the map inside, the player's cell in the middle, marked (not blinking). It is
  drawn with the rest of the HUD (runtime-core.md §11) when the player's cell changed, but not
  in the frame a room was entered (that frame loads the room: on the GBA there is no time for
  more), and all of it when the layer was drawn anew. In a room that is not on the map the
  place stays empty.
- **A pack pushed over the link** (a changed world file): the chunk is found again by the
  pack's data and CRC. The explored bits keep their numbers; if the cells were renumbered (a
  room grew, rooms moved in the file's order) they now mark other cells.

The test level's map: `assets/test_room/world.bmworld` places `test_room` at (0, 0); the second
room and the boss hall follow by their doors; the debug rooms are hidden. Its Start menu has a
**Map** page (`screens/map_menu.ms`); the boss's health bar moved below the player's (the top
right is the small map's).

## 5. Editor: the World tab

The rooms of the project on the grid as the cooker lays them out: rooms the file places solid,
rooms placed by their doors lighter and marked "(doors)", a colour per area; each cell's walls
and doors as in the game; a red line from a door whose room is not next to it on the map, a
green one from a door inside a room (in the back wall) to its room. Dragging a room places it
(the file gets its `x` and `y`); dragging the ground moves the view, the wheel zooms. The list
shows every room of the project: where it is, whether by its doors, hidden or not on the map.
For the selected room: **Not on the map (hidden)**; **Put on the map** (a room that is not on
it: right of the map, to be dragged where it belongs); **Place by its doors** (its `x` and `y`
are removed). **Fix all where they are** writes the places of all rooms on the map into the
file, so that changing a door later does not move them. The toolbar has **New world map** (a
project without one: `world.bmworld` in the first asset folder, the first room at (0, 0)), the
HUD's small map, and the cell size. Problems (section 2) are listed. Every change is written at
once and, with Live, pushed into the running game.

## History

- 1: first version.
