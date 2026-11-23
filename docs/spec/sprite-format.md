# Sprite format

Version 3, draft.

A sprite is a source asset ([asset-model.md](asset-model.md)): a **sheet image** plus a
**sprite file** (`.bmsprite`, XML) that says where the frames are and how they are played.
The file name without its extension is the sprite's **name**; the game finds sprites by name.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<sprite version="1" image="player.png">
 <hitbox x="-5" y="-30" w="10" h="30"/>
 <frame x="0" y="0" w="16" h="32" ax="8" ay="32"/>
 <frame x="16" y="0" w="16" h="32" ax="8" ay="32">
  <hitbox x="-5" y="-20" w="10" h="20"/>
 </frame>
 <animation name="idle" loop="1">
  <step frame="0" duration="40"/>
  <step frame="1" duration="40"/>
 </animation>
</sprite>
```

| Element | Attributes | Meaning |
|---|---|---|
| `sprite` | `version` (1), `image` | the sheet, relative to the sprite file. Transparent pixels (alpha below 128, or magenta `#ff00ff`) are not drawn |
| `hitbox` (in `sprite`) | `x`, `y`, `w`, `h` | the hitbox of every frame that has none of its own, relative to the anchor |
| `frame` | `x`, `y`, `w`, `h`; `ax`, `ay` | a rectangle of the sheet (1 to 128 pixels each way) and the **anchor** inside it: the point that is placed at the object's position. For actors that is the feet (bottom centre), as in rooms ([room-format.md](room-format.md) §5). Frames are numbered in file order from 0 |
| `hitbox` (in `frame`) | `x`, `y`, `w`, `h` | this frame's own hitbox |
| `box` (in `sprite` or `frame`) | `kind` (`hurt`, `hit`, `guard`), `name`, `x`, `y`, `w`, `h` | a box for fighting ([../plans/hitboxes.md](../plans/hitboxes.md) §2): `hurt` where the figure can be hit, `hit` where it hits while a script arms it, `guard` where a hit is blocked. Relative to the anchor, x and y from -128 to 127, w and h up to 255. `hit` and `guard` boxes have a name; boxes of one name belong together. In `sprite`: the boxes of every frame that has none of that kind of its own. At most 4 of a kind per frame and 8 names per sprite |
| `animation` | `name`, `loop` (1 or 0) | a named sequence. An animation that does not loop stays on its last step and reports that it has finished |
| `step` | `frame`, `duration` | show a frame for `duration` game frames (60 per second on the GBA), at least 1 |
| `palette` | `name`, `image` (optional) | another palette of the same pixels ([../plans/layered-sprites.md](../plans/layered-sprites.md) §2): the sheet's colours (slots) take the colours their pixels have in `image` (a sheet of the same size), or those its `swap` children give. Not `default`, which is the sheet's own |
| `swap` (in `palette`) | `from`, `to` (`#rrggbb`) | the slot of colour `from` takes colour `to` |

Colour overrides of the sheet image (asset-model.md, Overrides) apply to sprites too.

## Cooking

For the GBA profile a sprite becomes one `SPRT` chunk ([pack-format.md](pack-format.md)):

- **Colours**: one palette bank of 16 (15 plus transparency), sorted from dark to light. More
  than 15 colours in the frames is an error (`does-not-fit`).
- **Pieces**: every frame is cut into 8×8 tiles from its top-left corner and covered with
  hardware sprites of the 12 OBJ sizes (meta sprite), choosing for each uncovered tile the size
  that covers the most pixels for the tiles it costs. Empty parts of a frame cost nothing.
- **Tiles**: 16 colours, 1D mapping (the tiles of a piece follow each other). A piece with the
  same pixels as an earlier one (in any frame) uses that one's tiles.
- The report says how many tiles and colours the sprite takes, the most hardware sprites a
  frame needs and its sprite cycles on the most expensive line (PLAN.md §16).

```
u16 name (string id)   u16 frames   u16 animations   u16 tiles   u16 pieces   u16 steps
u32 offset of: frames, pieces, animations, steps, palette, tiles      (from the chunk start)
u32 offset of: box index, boxes, box names   u16 boxes   u8 box names   u8 flags (bit 0: the tiles are packed)
u16 palettes   u16 0

frame (12 bytes):     u16 first piece, u8 piece count, u8 box count,
                      s8 hitbox x, y, u8 hitbox w, h,
                      s8 bounds x, y, u8 bounds w, h        (of the drawn pixels)
piece (6 bytes):      s8 dx, dy (top left, relative to the anchor),
                      u8 shape (bits 0-1: square, wide, tall) | size << 2, u8 0, u16 first tile
animation (8 bytes):  u16 name (string id), u16 first step, u16 step count, u8 loop, u8 0
step (4 bytes):       u16 frame, u16 duration
palette:              palettes × 16 × u16 BGR555 (entry 0 is transparent and written as 0), the
                      default first; then palettes × u16 string id of their names ("default"
                      first)
tiles:                32 bytes each: 8 rows of 4 bytes, the left pixel of each pair in the low
                      four bits. The tiles of a piece follow each other row by row. Packed
                      (flags bit 0): the same bytes as LZ77 below, then a byte of padding if
                      the length is odd.
box index:            u16 first box of each frame
box (6 bytes):        s8 x, y, u8 w, h (relative to the anchor), u8 kind (0 hurt, 1 hit,
                      2 guard), u8 name (index into the box names, 0xFF none)
box names:            u16 string id each
```

Shape and size give the piece's pixels as on the hardware: square 8, 16, 32, 64; wide 16×8,
32×8, 32×16, 64×32; tall 8×16, 8×32, 16×32, 32×64. Shape 3 does not exist and such a piece is
not drawn. Everything is little endian and the chunk is padded to four bytes. The runtime
does not use the bounds.

The header is 56 bytes. A chunk of version 1 has the 36 bytes up to the tiles' offset only,
with its frames at offset 36, and no boxes; one of version 2 the 52 bytes up to the flags, and
one palette. The runtime tells them apart by where the frames start (at least 52: version 2,
at least 56: version 3) and reads a version 1 chunk as one without boxes, a version 1 or 2
chunk as one with the default palette alone. A
frame's boxes are its own of a kind, else the sprite's of that kind, worked out by the cooker.

A reader MUST check that every piece, step and frame reference lies inside its table before
using the chunk.

## At run time

- A sheet is loaded by copying its tiles into sprite memory and a palette into an OBJ bank,
  when a room shows it; a sheet can be in several banks at once, one per palette in use
  ([runtime-core.md](runtime-core.md) §9).
- Mirroring (facing left) flips every piece and mirrors its position, the hitbox and the
  boxes around the anchor; a sprite needs only frames that face right.
- The position in an animation (animation, step, frames shown) is plain data and part of the
  game state ([state.md](state.md)), so save states and link transfers continue animations
  where they were.
- The engine's player uses the sprite named `player` with the animations `idle`, `run`,
  `jump`, `fall` and `climb` (missing ones fall back to the first). A scripted object uses
  the sprite its class names, or the one named like the class (ignoring case), and its script
  chooses the animation (`play_anim`). An unscripted object with a sprite named like its
  class shows that sprite's first animation.
- Where sheets go in sprite memory, how the animation state advances, the order in which
  hardware sprites are given out and how a frame is placed on screen are in
  [runtime-core.md](runtime-core.md) §9.

## History

- 1: first version.
- 1.1: tile bytes, piece sizes; the run-time rules moved to runtime-core.md.
- 2: hurt, hit and guard boxes in the file and in `SPRT` (box index, boxes, box names).
- 2.1: packed tiles (the flags byte, which was 0).
- 3: palettes: `palette` and `swap` in the file, a palette table with names in `SPRT`.

## Packed tiles

The cooker packs a sprite's tiles when that is smaller (4-bit tiles are mostly transparent):
LZ77 with a window of 512 bytes, so that the runtime unpacks with 512 bytes of history and
nothing more. Items in groups of 8 behind a flags byte (bit `i` for item `i`, from the lowest):
0 a byte as it is; 1 a copy, two bytes `(length - 3) << 4 | (distance - 1) >> 8`,
`(distance - 1) & 255`: `length` 3 to 18 bytes from `distance` 1 to 512 back. It ends where the
tiles are all there (tiles × 32 bytes). The runtime unpacks a sheet when it loads it, a tile at
a time into sprite memory; packed data that reads past the chunk, copies from before its start
or makes more than the tiles is refused. The test level's sprites take 11 KB less.
