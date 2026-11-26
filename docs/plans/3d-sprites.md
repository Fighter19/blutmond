# 3D sprites

Sprites drawn by Blender: a character is modelled once, posed once for every frame, rendered
and turned into the engine's pixels, palette and normal map. The first is **Mitsuki**
(`assets/mitsuki/`), a playable character with every animation of the test level's player.

Built: the workflow below, Mitsuki, and a pack of the test level with her as the player.
Open: the points at the end.

## 1. The workflow

```
assets/mitsuki/mitsuki.py ──Blender──▶ passes ──pixel step──▶ sources/mitsuki.png, _n.png, .bmsprite
   (or mitsuki.blend)      render.py            pixelize.py       │
                                                                   └─split_sprite.py─▶ mitsuki_legs, mitsuki_torso,
                                                                                      player.bmfigure, mitsuki_face
```

One command does all of it:

```
python3 tools/sprite3d/make_sprite.py assets/mitsuki/mitsuki.py \
    --layout assets/sources/player.bmsprite --sink 2 --out assets/mitsuki --name mitsuki \
    --split 13 --figure player --save-blend assets/mitsuki/mitsuki.blend --preview mitsuki_x4.png
```

It needs Blender 4.0 or later (`--blender`, `$BLENDER` or `blender` on the path; on Ubuntu
`sudo apt install blender`), and numpy and PIL for the pixel step. Blender runs in the
background (`blender -b`); nothing in it needs numpy, so the distributions' Blender works as
well as blender.org's. Mitsuki's 43 frames and portrait take about five seconds.

| Step | Where | What |
|---|---|---|
| Model | the character's script (`build(Rig)`), `tools/sprite3d/rig.py` | joints, parts, materials, poses, locators, palettes, the portrait |
| Render | `tools/sprite3d/render.py`, in Blender | one orthographic Cycles render a frame, 8 × 8 samples a pixel, passes: material index, normal, depth |
| Pixels | `tools/sprite3d/pixelize.py` | the sprite's pixels from the passes: material, light band, outline, normal map |
| Sprite | `tools/sprite3d/make_sprite.py` | the sheet in the layout's places, the `.bmsprite` (boxes and points measured), the split, the figure, the portrait |

### Model (rig.py)

The world: x forward (a sprite faces right), y away from the viewer (her right, -y, is the near
side), z up, **one unit a pixel**. The ground is z = 0 and (0, 0, 0) lands on a frame's anchor.

- **Joints** are empties hanging on each other. In the rest pose none is turned, so parts are
  written in world coordinates and follow their joint.
- **Parts** are ellipsoids, tubes (tapered, rounded ends, flattened with `squash`) and boxes;
  planes can cut a part (the hime cut is a sphere of hair cut above the eyes). Each is rigid on
  its joint: no skinning, the PlayStation way, which is plenty at 30 pixels.
- **Materials** are palette ramps (dark to light) with the `bands` where the light steps from
  one colour to the next. Materials may share colours (her eyes, scarf and lance are the same
  reds); the palette is the colours used, at most 15. `weight` and `min_cover` let small parts
  win their pixel: an eye of half a pixel, a blade a third of a pixel wide.
- **Poses** turn joints by `swing` (forward and back in the picture's plane: an arm hanging
  down, swung 90, points ahead), `spread` (sideways) and `twist`, move the root, and show
  groups of parts (the katana, the lance). Pose i is timeline frame i + 1, keyed constant.
- **Locators** are empties whose place is measured in every frame (the hand: the sprite's
  `hand` points for aiming).
- **Palettes** are colour swaps, written into the sprite (her `steel`: the coat when the chain
  mail is worn).
- The **portrait** is one more render of a pose: closer (`zoom` pixels a unit), from more in
  front (`yaw`), 32 × 32, for the dialogue box. It can `show` groups no sprite frame shows:
  Mitsuki's far eye, which from the side would peek out past the edge of her face.

Everything the later steps need is kept in the scene (`scene["sprite3d"]`), so the `.blend`
that `--save-blend` writes can be opened in Blender, its joints posed by hand on the
timeline, its parts moved or remodelled, and rendered as it is:
`make_sprite.py assets/mitsuki/mitsuki.blend ...`. The script stays the source the repository
regenerates from; an edited `.blend` becomes the source instead (say so next to it).

### Render (render.py)

The camera is orthographic, turned `yaw` degrees from the side toward her front (Mitsuki: 22)
and `pitch` degrees down (8), so the sprite has a three-quarter look and mirrors well. For
each frame of the layout it is placed so that the world's origin falls on the frame's anchor
(on the ground row: the anchor plus the layout's sink) and the frame is `supersample` (8)
times larger than the sprite. Cycles renders one sample a pixel with a box filter of 0.01, so
every render pixel is exactly one ray: material index, normal (world space) and depth, written
by the compositor as EXR and handed on as raw floats.

### Pixels (pixelize.py)

For each sprite pixel (64 render pixels):

1. **Drawn or not**: at least half its samples hit the figure, or a thin material has its
   `min_cover`.
2. **Material**: the one with the most samples, each counted `weight` times.
3. **Colour**: the mean normal of that material's samples, lit by the character's light
   (camera space), cut at the material's bands into its ramp. The light is fixed in the
   picture, so it is as an artist would shade: from the top left, a little from the front.
4. **Inner lines**: where a part lies more than `inner_lines` units (3) behind its neighbour
   (an arm in front of the coat), the farther pixel takes the outline colour.
5. **Outline**: the pixels next to the figure (left, right, above, below) take the outline
   colour.
6. **Normal map**: the mean normal (x right, y down, z toward the viewer: lighting.md), the
   outline's pointing out of the figure. In a lit room the cooker gives the palette slots
   their normals from it (sprite-format.md, `normal`).

### Sprite (make_sprite.py)

The **layout** is a sprite whose frames (rectangles, anchors, boxes, points), animations and
palettes the new one takes over: Mitsuki takes the player's (`assets/sources/player.bmsprite`),
so frame i of the player is her pose i and every animation the engine and the scripts ask for
is there. Hit boxes named after a material (`blade`) are measured around that material's
pixels, one pixel more each way, so the katana's arcs hit where they are drawn; points named
after a locator (`hand`) go where it lies. Then `tools/split_sprite.py` cuts the sheet at the
waist into legs and torso (a drawn normal map is split with it) and the figure `player` puts
them together, as for the test level's player (layered-sprites.md §4).

## 2. Mitsuki

A kuudere: cool, quiet, hard to read, and cute anyway.

| | |
|---|---|
| Hair | silver, a hime cut (a straight fringe, side locks), long down her back, an ahoge that will not lie down, a gold crescent pin |
| Face | big eyes set back on the sides of her head, half-lidded under level lashes: white, a crimson iris, a darker pupil, a glint; no mouth to speak of |
| Clothes | a navy greatcoat, open over a white shirt with a red ribbon, cuffs too big for her, a red scarf trailing behind, dark tights, boots with white tops |
| Arms | a katana (the sword's swings), a lance with a red lacquered shaft (the thrusts) |
| Colours | 15: outline, three of hair, two of skin, two reds, three of coat, boot, two golds, steel (the eyes' white is the hair's light) |
| Frames | the player's 43: idle (breathing, the hair and scarf settle), run, jump, fall, climb, the swing, the lance up and down, the low sweep, the arcs up and down, the thrust ahead, push, aim (9 directions), sitting on the bench (her feet do not reach the ground) |
| Portrait | `mitsuki_face`, the dialogue box's Hero in her pack |

Files in `assets/mitsuki/`: `mitsuki.py` (the model and the poses), `mitsuki.blend` (the same
scene, written by `--save-blend`, for opening in Blender), `sources/` (the whole sprite and its
normal map, the layout's frames), `mitsuki_legs` and `mitsuki_torso` (with `_n` normal maps),
`player.bmfigure`, `mitsuki_face`, `data/speakers.bmdata`.

**Playing her**: the build cooks `mitsuki_level_gba.bmpk`, the test level with her figure
instead of the player's and her portrait and name in the dialogue (`BM_MITSUKI_LEVEL_ASSETS` in
CMakeLists.txt: the test level's assets without the player's sprites and speakers, with hers):

```
build/game build/mitsuki_level_gba.bmpk
```

Her figure is named `player`, so the engine takes her without a change. The test level's own
pack, the tests and their pictures keep the old player. Her sheets take 553 tiles against the
old player's 388 (17.7 KB of sprite memory against 12.4 KB): she is wider, outlined, and her
hair moves.

## 3. Open

- **More colour slots for the light**: with 15 colours a lit sprite has no slot left to split
  a colour by its normals (lighting.md), so in lit rooms each colour has one normal. A palette
  of 11 or 12 colours would let the cooker spend the rest on light.
- **A cartridge image** (`game.gba` with her pack): the cartridge's pack list in CMakeLists.txt
  would take `BM_MITSUKI_LEVEL_ASSETS`; not built here (no ARM toolchain where this was made).
- **Raised arms**: her head is wide (she is small, her proportions are a chibi's) and her
  arms short, so an arm raised straight up (aiming up, climbing) mostly lies over her hair;
  it reads, but less than the other directions. Longer arms for those poses, or a turned
  head, would help.
- **Editor**: a button in the Animation panel that runs make_sprite.py for a sprite that has
  a character script or `.blend` beside it.
- **Other characters**: an enemy has its own layout (its `.bmsprite`); a character script
  with as many poses as that layout has frames is all it takes.
