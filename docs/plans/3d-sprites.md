# 3D sprites

Sprites drawn by Blender: a character is modelled once, posed once for every frame, rendered
and turned into the engine's pixels, palette and normal map. The first is **Mitsuki**
(`assets/mitsuki/`), a playable character with every animation of the test level's player.
Rooms are made the same way: her bedroom (`assets/mitsuki/home/`), where her prologue starts.

Built: the workflow below, Mitsuki, her room and her prologue (section 3), and the test level
with her as a character to choose.
Open: the points at the end.

## 1. The workflow

```
assets/mitsuki/mitsuki.py ──Blender──▶ passes ──pixel step──▶ sources/mitsuki.png, _n.png, .bmsprite
   (or mitsuki.blend)      render.py            pixelize.py       │
                                                                   └─split_sprite.py─▶ mitsuki_legs, mitsuki_torso,
                                                                                      player_mitsuki.bmfigure, mitsuki_face
```

One command does all of it:

```
python3 tools/sprite3d/make_sprite.py assets/mitsuki/mitsuki.py \
    --layout assets/mitsuki/layout.bmsprite --sink 2 --out assets/mitsuki --name mitsuki \
    --split 13 --figure player_mitsuki --save-blend assets/mitsuki/mitsuki.blend --preview mitsuki_x4.png
```

It needs Blender 4.0 or later (`--blender`, `$BLENDER` or `blender` on the path; on Ubuntu
`sudo apt install blender`), and numpy and PIL for the pixel step. Blender runs in the
background (`blender -b`); nothing in it needs numpy, so the distributions' Blender works as
well as blender.org's. Mitsuki's 50 frames and portrait take about five seconds. (Blender
4.0's ray tracer now and then crashes setting up a scene; the tools try up to three times.)

| Step | Where | What |
|---|---|---|
| Model | the character's script (`build(Rig)`), `tools/sprite3d/rig.py` | joints, parts, materials, poses, locators, palettes, the portrait |
| Render | `tools/sprite3d/render.py`, in Blender | one orthographic Cycles render a frame, 8 × 8 samples a pixel, passes: material index, normal, depth |
| Pixels | `tools/sprite3d/pixelize.py` | the sprite's pixels from the passes: material, light band, outline, normal map |
| Sprite | `tools/sprite3d/make_sprite.py` | the sheet in the layout's places, the `.bmsprite` (boxes and points measured), the split, the figure, the portrait (`--face-only yes`: only that) |
| Room | `tools/sprite3d/make_room.py` | a room's picture in one render: its tiles, their normal map, the Tiled map with its properties and objects |

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
  groups of parts (the katana, the lance). Pose i is timeline frame i + 1, keyed constant. A
  pose can have a camera angle of its own (`view=(yaw, pitch)`): Mitsuki climbs seen from
  behind and sits seen from the front.
- **Locators** are empties whose place is measured in every frame (the hand: the sprite's
  `hand` points for aiming).
- **Palettes** are colour swaps, written into the sprite (her `steel`: the coat when the chain
  mail is worn).
- A part can hang on a **joint of its own that never turns** (Mitsuki's `prop`): it stays
  where it is in the world whatever she does (her phone on the bed).
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
(on the ground row: the anchor plus the layout's sink); the frame is rendered 16 pixels
larger each way than the layout's and `supersample` (8) times larger than the sprite, and
later cut to what is drawn, so nothing that reaches past the layout's rectangle (hair flying
up as she falls) is cut off. Cycles renders one sample a pixel with a box filter of 0.01, so
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
palettes the new one takes over: Mitsuki's (`assets/mitsuki/layout.bmsprite`) is the player's
(`assets/sources/player.bmsprite`) with her bed's frames after it, so frame i of the player is
her pose i and every animation the engine and the scripts ask for is there. The frames' rectangles are each cut to the picture (the anchor, boxes and points
keep their places on the figure) and laid out in rows of a sheet 256 pixels wide. Hit boxes named after a material (`blade`) are measured around that material's
pixels, one pixel more each way, so the katana's arcs hit where they are drawn; points named
after a locator (`hand`) go where it lies. Then `tools/split_sprite.py` cuts the sheet at the
waist into legs and torso (a drawn normal map is split with it) and the figure `player` puts
them together, as for the test level's player (layered-sprites.md §4).

### Rooms (make_room.py)

```
python3 tools/sprite3d/make_room.py assets/mitsuki/home/home.py --out assets/mitsuki/home \
    --save-blend assets/mitsuki/home/mitsuki_home.blend
```

A room's script builds it with the same `Rig` (walls, furniture: boxes, ellipsoids, tubes on
one joint, one pose) and says in `ROOM` what the room is: its size, the row the floor's front
edge lands on (the player's feet, the sprites' sink below the collision's top), the solid rows,
the map's properties and its objects. The camera looks straight at the back wall (`yaw` 0)
and a little down (`pitch`: Mitsuki's room 14 degrees, so the floor shows as a strip), and the
picture is one job of the render and pixel steps above: materials, light bands, inner lines
(`inner_lines` larger than for a figure: going up the floor, each pixel is 4 units further
away). Materials marked `emissive` (the night sky, the moon, the eyes outside) are black in the
normal map: a lit room draws them as they are and shades the rest by its `Light` from the
normals. The picture is cut into 16 x 16 tiles, each once: `<name>_tiles.png`,
`<name>_tiles_n.png` and `<name>.tmx` (the background layer; the main layer empty).

With `lighting = palette` every (colour, light level) pair is a palette entry: Mitsuki's room,
23 colours, fits the GBA's 15 banks with 3 levels and no dither, so a room script keeps few
colours (two of a ramp where one would do, the stars the moon's) and draws what the room's
light does not reach (her attic) `emissive`: one entry a colour. A lit colour is at most as
drawn (ambient plus light, at most 1): draw a room as its brightest light shows it, and let
the darker ones darken it.

**Scale.** Draw a room to its characters' scale and look at them together (Mitsuki: 31
pixels, 1.55 m, so a pixel is 5 cm; her room's ceiling 2.8 m, its bed 2 m, its window 1.3 m):
`home.py` says where its objects and her bed frames go, and one picture with her sprites put in
(at the PlayerStart, lying in the bed, in front of the dresser) shows whether it holds.
Objects of a room drawn by the same tools (`props.py`: the katana, the switch, the thing at
the window) are rendered from the room's angle at the place `on_screen` gives them. A sprite
can glow in part as a tile can: an `emissive` material is black in its normal map, and the
cooker draws those colours as they are (sprite-format.md).

## 2. Mitsuki

A kuudere: cool, quiet, hard to read, and cute anyway.

| | |
|---|---|
| Hair | silver, a hime cut (a straight fringe, side locks), long down her back, an ahoge that will not lie down, a gold crescent pin |
| Face | big eyes set back on the sides of her head, half-lidded under level lashes: white, a crimson iris, a darker pupil, a glint; no mouth to speak of |
| Clothes | a navy greatcoat, open over a white shirt with a red ribbon, cuffs too big for her, a red scarf trailing behind, dark tights, boots with white tops |
| Arms | a katana (the sword's swings), a lance with a red lacquered shaft (the thrusts) |
| Colours | 15: outline, three of hair, two of skin, two reds, three of coat, boot, two golds, steel (the eyes' white is the hair's light) |
| Frames | 50: the player's 43, idle (breathing, the hair and scarf settle), run, jump, fall, climb (seen from behind), the swing, the lance up and down, the low sweep, the arcs up and down, the thrust ahead, push, aim (9 directions), sitting on the bench (seen from the front, her right leg crossed over her left, her hands in her lap; her feet do not reach the ground); then 7 in her bed at home for her prologue, drawn from where she stands in front of it and from the room's own angle: asleep under the blanket (eyes closed: a line instead), the phone beside her lighting up and buzzing, sitting up with it at her ear, sitting on the bed's edge (`sleep`, `ring`, `call`, `wake`) |
| Portrait | `mitsuki_face`: her lines in the dialogue box while she is the player |

Files in `assets/mitsuki/`: `mitsuki.py` (the model and the poses), `layout.bmsprite` (her frames:
the player's, then the bed's), `mitsuki.blend` (the same
scene, written by `--save-blend`, for opening in Blender), `sources/` (the whole sprite and its
normal map, the layout's frames), `mitsuki_legs` and `mitsuki_torso` (with `_n` normal maps),
`player_mitsuki.bmfigure` (her figure), `mitsuki_face`, `speaker.bmdata` (her `Speaker`
record: name, portrait and voice in the dialogue box), `voice_mitsuki.bmsong` (her voice: a
quiet, cool sine higher than the hero's blip with a faint glassy glint above it, written by
`tools/make_test_songs.py` with the other voices).

**Playing her**: she is in the test level as a playable character to choose (Start, System,
Character): a figure named `player_<name>` can be the player (sprite-format.md, Figures), the
data table `Character` (`assets/test_room/data/characters.bmdata`: a name for the menu, the
figure and the speaker of the player's lines) lists who can be chosen, the screen `CharacterMenu` lists them, and the choice is an
option kept with the language (runtime-core.md). The player of the test level stays the
default, so the tests and their pictures keep it. Her sheets take 493 tiles against the old
player's 388 (15.8 KB of sprite memory against 12.4 KB): she is wider, outlined, and her hair
moves; the cooker keeps room for the largest playable character in every room.

The multiboot images have no choice of character: their code leaves it out
(`src/hal/gba/character_off.c`), their pack leaves out Mitsuki, the table and the menu, and a
pack fitted for one over the link (runtime-abi.md §0) leaves them out too
(`src/link/live_master.c`), so a room is not left out for them.

## 3. Her prologue

Her room at home (`assets/mitsuki/home/`, section 1, "Rooms"): an attic room at night, cut
open at the front like a doll's house, the roof's gable cut open above it (unlit, the stars
past it); her bed with lilac-white sheets (the colours of her hair, so the blanket in her bed
frames matches), a window with the moon and something with red eyes peeking in over the sill,
red curtains, a rug, a low dresser with a plush rabbit (red-eyed too, at her head's height:
standing in front of it, half its head shows over hers), a light switch by the foot of the
bed and a lamp on the ceiling; through a door frame a little hall with her katana on its rack,
shoes, and the door out. One `Light` at the lamp: off, the moon's dim blue light; on, warm and
bright (`set_light`).

**The scene** (`prologue.ms`, the room's `attach`): entered at her bed (the room's
`PlayerStart`, x 46), it makes Mitsuki the player if another character is, turns her to the
right (`owner_turn`: her bed frames are drawn facing that way), and plays: asleep; the phone
rings three times and she sleeps on, a fourth and she answers. **Hina**, her friend, in a
panic: does she see them too, the things with teeth? Mitsuki, flat: it is three in the
morning; did Hina eat cheese before bed again? Then she sees one at her own window ("It's
staring at me. Rude."; it blinks while they talk), keeps calm ("Someone has to be"), and
agrees to meet her at the park, under the bench by the lamp: "Don't get eaten. Finding a new
friend sounds tiring." She sits on the edge of the bed (the thing at the window ducks away),
walks to the switch and flips it: the room goes warm and bright, and from now on the
cottage's window in the street is lit (dark while her light is off: its colours are the
street's `glow_switch`, turned by `cottage.ms` through the palette, no sprite over them; the
street's lamp glass, `glow_light`, flickers with its light). She says she'll bring
her katana, and the player has her. The katana (`Katana`, `katana.bmdata`, with its moves Iai
and Crescent in `commands.bmdata`) lies on its rack on the way to the door (walked under, it is
taken and equipped); the door (`needs = Katana`) opens only with it, and she says so if she
tries without. Out through it is the rural street, at the cottage's door (Up there leads back
in, where she is awake).

**Up in front of things**: the switch turns the light on and off; the rabbit gets a word,
by turns "Usagi-san. Guard the room while I'm out." and "...You were facing the bed a minute
ago."; the bed is one ending: she lies down ("...Monsters are a tomorrow problem."), the phone
rings again ("MITSUKI!! THEY'RE EATING THE BENCH!!"), "Zzz...", **The end?** — and she gets up
again.

| | |
|---|---|
| Starting it | Start, System, Character, Prologue (the Character record's `prologue`, `prologue_x`, `prologue_y`: a teleport there); on the PC `build/game --room mitsuki_home` |
| Objects | `HomeProp` (`home_prop.ms`, one sprite: `props.py`) by its property `Kind`: the katana, the switch, the thing at the window (staring, blinking while the dialogue box is open, ducking away); the street's `Cottage` attachment (`cottage.ms`) sets its window's glow |
| Story flags | `MitsukiLight` (her light: the switch, the street's window), `MonsterGone` (set when she gets up): `flag`, `set_flag` (runtime-core.md, API 143-144), kept in the save game; the window through `set_glow` (145) |
| Hina | only her portrait (`hina.py`, `--face-only yes`): chestnut twin tails, wide green eyes, mouth open, a drop of sweat, a yellow hoodie, the phone at her ear; her voice a quick shrill blip (`voice_hina`) |
| Sounds | the phone's ring, a buzzing two-note trill (`phone_ring`); the switch's click (`light_switch`); from `tools/make_test_songs.py` |
| Texts | the lines (`lines.bmdata`), German and French in `lang/` (a language may come in several files, pack-format.md §3.2) |
| Multiboot | none of it: `assets/mitsuki/` and the room are left out of the multiboot pack, and their images leave out the story functions (`src/hal/gba/story_off.c`) and the light (`light_off.c`); a pack fitted for one over the link brings a room's scripts only with that room (runtime-abi.md §0) |

### Mitsuki's park (`assets/mitsuki/park`)

West of the rural street (its door at the west edge), a park at night drawn as the street is
(`tools/make_park.py`: trees, hedges, an iron railing, a swing set, two gas lamps whose glass
flickers with their light, `glow_light`; lit through the palettes, 4 levels): the bench at its
west end, out of sight from where the street leads in.

**The scene** (`park.ms`, the room's `attach`, directing Hina, the nightmare and the close-up
through story flags `Park*`): the music (`park_dread`, dynamic: drones and wind; a heartbeat
and a far music box; the heart faster, a saw pulsing; on every beat, a high tremolo) grows
tenser the nearer she comes to the bench (intensity 0 to 3 by the distance). Under the bench
only Hina's green eyes, blinking. Close to it Hina storms out into Mitsuki's arms (drawn in
front of her, `draw_in_front`), and the music turns warm with an ache in it (`park_reunion`, F
major leaning on D minor); Mitsuki rocks her, pats her head while she sobs; lets go, takes her
hand and leads her to the bench (seen from the side); they sit close, shoulder to shoulder,
their knees turned to each other, Mitsuki holding Hina's hand in her lap (her head turned to
her, Hina's face relaxed), a close-up of their hands shown over the scene for a while. Hina
opens up, slowly, about what has weighed on her since "that day" (never said what), then leans
in, her head in Mitsuki's lap, her face turned a little toward us; Mitsuki strokes her hair. Then the nightmare
rises out of the ground, hushing, reaches for her and takes her; she reaches out for Mitsuki
and is carried off into the night, screaming off-screen; Mitsuki is left reaching after her.
After it (`ParkDone`) the park stays tense and Hina gone.

| | |
|---|---|
| Starting it | through the street's west door; on the PC `build/game --room park` |
| Before the monsters | until the nightmare took Hina (`ParkDone`) Mitsuki walks at 60 % (`uneasy.ms`, attached to her room, the street and the park: `add_speed`), so the park's music builds as she nears the bench; the street's knight is not there yet (`cottage.ms`, the street's attachment, spawns it after), and the first time she comes near it she has a word for it (once: the flag `MitsukiKnight`) |
| Hina | her full body (`hina.py`, 16 frames: hiding, the leap, the rocked hug, sobbing, led, on the bench, held up); a relaxed face for the bench (lids over her pupils, level brows, a soft smile: group `relaxed`); her soft portrait `hina_soft` |
| Mitsuki | her frames for the scene in `mitsuki.py` (hug rocking, pat, leading, on the bench: `sit_hand`, `sit_leaned`, `caress`; reaching out) |
| The nightmare | a shadow, emissive (`nightmare.py`): rising, hushing, reaching, holding, leaping away |
| On the bench | one scene, `pair.py`: both models seated (thighs touching at the knees, shoulders touching), their arms found by search; drawn for the close-up and for both sprites (Mitsuki's frames 55, 59 to 61, Hina's 9, 10, 14, 15), each sprite rendered with the other in the scene as a `holdout` material (hiding what is behind it, its pixels left empty; the nearer one draws the line where they meet). `bench.py` draws both together to look at (`--preview`, `--save-blend`) |
| The close-up | `hands.py`: that scene near, their hands built as signed-distance shapes (metaballs skinned to bones), each hand a material `apart` (a line where one lies on the other, rig.py); a panel (`--clip yes`) |
| Sprite memory | the room's sprites fill it with the player's; those not drawn for a while (the close-up, the nightmare) make room for the dialogue's portraits, least recently drawn first (runtime-core.md) |
| Multiboot | none of it; a pack fitted over the link brings a room's sprites (named like its objects' classes) only with that room |

### The search in the outskirts (`assets/mitsuki/outskirts`)

West of the park, two rooms drawn as the park is (`tools/make_outskirts.py`), where Mitsuki
searches for Hina after the nightmare took her; their attachment `Search` (`search.ms`, which
room by the property `Search.Room`):

- **The riverside**: a path along a river at night (an embankment, an iron railing, the water
  with the moon's glints, the far bank's trees, a willow, two lamps); bats. The first time she
  comes she calls for Hina, and nothing answers; the first bats get a word from her.
- **The shrine path**: dark woods, two stone lanterns, a wooden gate at the west end with a
  rope across it: closed (a wall). A knight. A little way in her phone rings: Hina, somewhere
  dark and round, "like the inside of a Christmas ball. But wrong. The glass is cold... and it
  breathes."; the call drops. Then it starts to snow (the story flag `Snowing`; outside it
  snows from now on: the engine's `set_snow`, runtime-core.md, called by the rooms'
  attachments; the rooms' colours turn colder, leaves and grass frosted, snow lies on the
  paths and walls), and she puts it together: "A Christmas ball. Snow. ...The park." At the gate:
  "The way's closed. She's not here."

### The snowman (`assets/mitsuki/snowman`)

Back in the snowy park a snowman stands by the path (`snowman.py`: three balls of snow, coal
eyes, a carrot nose, stick arms, a red scarf, and in his hands a Christmas ball: clear glass,
a silver cap and ring, and in it a snowy diorama). Up beside him the first time:
"Who builds a snowman in the middle of the night?" The second time she grabs the ball; he holds
on, grows to 200 % and fights (`snowman.ms`, health 30, the music `frost_waltz`):

| Attack | |
|---|---|
| A stomp | he jumps at her, landing where she stood (striking in the air); a ripple runs through the ground to where she is, and a pillar of ice breaks out there. The ripple and the pillar breaking out strike |
| Snowballs | rolled along the ground: jump over them |
| Frost breath | once his newest pillar is out, he draws breath and a beam grows out along the ground; it stops at the nearest pillar in front of him: stand behind one |
| Icicles | from half his health on, instead of the snowballs: fast, at her knees, always two close, a pause, two close |

Beaten, a shadow leaves him and he is a happy snowman again (`SnowmanDone`, saved).

How it fits:

- **Grown by the PPU**: all his frames are rendered at one size; grown, the engine draws him at
  200 % (`set_scale`, runtime-core.md: affine sprites; a whole number, so each pixel is 2 x 2
  and his outline stays as even as drawn). The grown frames' boxes are made as
  large in the sheet (`snowman_sheet.py`), so the hits need nothing.
- **The Christmas ball**: glass a renderer of 15 colours does not do, so `snowman_sheet.py`
  draws what is seen in it, each pose rendered again without it: his body behind it tinted
  violet, where nothing of him is behind every other pixel left empty (the park through the
  glass), a diorama (a floor of snow, a tree under snow, Hina: a yellow hoodie, flakes), a rim
  light on the lit side, a shine, an outline all round. It glows (its colours' normals black:
  the park's light does not darken it), in colours of its own.
- **One sheet**: the pack has its 28 sprites, so his fight's things are frames of his sheet
  (the snowball, the ripple, the pillar rising and standing, an icicle, the shadow, the beam).
  183 tiles: in the park, besides Mitsuki's figure and the doors' sheet, 287 are left.
- **The beam**: four lengths of 32 pixels each, one tile row high (two rows would be covered
  with 64 x 32 pieces, half empty), its middle one column repeated, so its pieces are shared; a
  beam longer than 128 pixels (a frame's most) is two, the far one beyond the near one.
- **One class**: `Frost` does each thing by its record (`things.ms`, `things.bmdata`): the
  pack's 48 classes. The snowman reckons where the pillars are (the ripple goes where she stands,
  the same reckoning as his) and when they are out, and stops the beam there.
- **Four places**: the park's own objects (Hina, the snowman, two doors) take four of the eight,
  so the things live short: at most two pillars (each stands 300 frames), the beam's two parts,
  two icicles in the air (they fly about a screen), a snowball or two.
- **Hits**: a hit holds while the animation it was armed in plays (hits.h), so each is armed
  right after its animation starts (the jump, the breath, each length of the beam, the pillar
  rising).

## 4. Open

- **More colour slots for the light**: with 15 colours a lit sprite has no slot left to split
  a colour by its normals (lighting.md), so in lit rooms each colour has one normal. A palette
  of 11 or 12 colours would let the cooker spend the rest on light.
- **Raised arms**: her head is wide (she is small, her proportions are a chibi's) and her
  arms short, so an arm raised straight up (aiming up, climbing) mostly lies over her hair;
  it reads, but less than the other directions. Longer arms for those poses, or a turned
  head, would help.
- **Editor**: a button in the Animation panel that runs make_sprite.py for a sprite that has
  a character script or `.blend` beside it.
- **Fades and the camera**: the prologue starts and ends as a cut; fades, a letterbox and a
  directed camera (dialogue.md §6) would let it breathe, and a skip.
- **Other characters**: an enemy has its own layout (its `.bmsprite`); a character script
  with as many poses as that layout has frames is all it takes.
