# Sprite format

Version 1, draft.

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
| `animation` | `name`, `loop` (1 or 0) | a named sequence. An animation that does not loop stays on its last step and reports that it has finished |
| `step` | `frame`, `duration` | show a frame for `duration` game frames (60 per second on the GBA), at least 1 |

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

frame (12 bytes):     u16 first piece, u8 piece count, u8 0,
                      s8 hitbox x, y, u8 hitbox w, h,
                      s8 bounds x, y, u8 bounds w, h        (of the drawn pixels)
piece (6 bytes):      s8 dx, dy (top left, relative to the anchor),
                      u8 shape (bits 0-1: square, wide, tall) | size << 2, u8 0, u16 first tile
animation (8 bytes):  u16 name (string id), u16 first step, u16 step count, u8 loop, u8 0
step (4 bytes):       u16 frame, u16 duration
palette:              16 × u16 BGR555
tiles:                32 bytes each
```

A reader MUST check that every piece, step and frame reference lies inside its table before
using the chunk.

## At run time

- A sheet is loaded by copying its tiles into sprite memory and its palette into an OBJ bank;
  the sheets of a pack stay loaded across room changes.
- Mirroring (facing left) flips every piece and mirrors its position and the hitbox around the
  anchor; a sprite needs only frames that face right.
- The position in an animation (animation, step, frames shown) is plain data and part of the
  game state ([state.md](state.md)), so save states and link transfers continue animations
  where they were.
- Until classes are scripted, the engine's player uses the sprite named `player` with the
  animations `idle`, `run`, `jump`, `fall` and `climb` (missing ones fall back to the first),
  and an object is drawn with the sprite named like its class (ignoring case), playing its
  first animation.

## History

- 1: first version.
