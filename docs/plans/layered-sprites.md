# Plan: layered sprites and variants

Status: steps 1 to 6 built. Palettes: `palette` (a second image or `swap`s) in the sprite file,
a palette table in `SPRT` version 3 ([../spec/sprite-format.md](../spec/sprite-format.md)), the
room object's property `palette`, `set_palette` and `flash` in scripts. Sheets: every sheet's
data opened once per pack, sprite memory and banks given out room by room per (sheet, palette)
pair, the player's sheet staying ([../spec/runtime-core.md](../spec/runtime-core.md) §9); the
cooker's report says per room what it loads (`sprite-memory`). The test level's Gatehouse has a
bat and a red Fast Bat, one sheet in two banks. Where it differs from sections 2 and 3: banks 0
to 12 are given out (13 is the flash, as before; the UI uses a background bank), bank 0 is the
player's; nothing is freed inside a room, only when a room is entered; colour rules (`ramp`,
`tint`), `tint` and `fade` and streamed animations are not built; a sheet that does not fit is
not drawn (and counted) rather than drawn as the placeholder.

Step 3 built: points in frames (`SPRT` version 4), figures (`.bmfigure`, `FIGR`) with layers in
`own` and `sync` mode, `behind` and a point's order, the pose and drawing order
([../spec/sprite-format.md](../spec/sprite-format.md) "Figures", runtime-core.md §9). The
player is the figure `player`: legs and torso split from the whole drawing by
`tools/split_sprite.py`, pixel for pixel the old sprite in every pose. Where it differs: only
the player is a figure yet (objects keep one sprite; 2 layers for them come with step 5); a
missing point falls back to the sprite's (the cooker fills it in), not to an animation's; each
of the player's sheets takes a bank of its own (the legs and torso of the test level: banks 0
and 1), so 11 are left for rooms; boxes come from the root layer only; the angle of a point is
read but nothing uses it yet; the figure's state is the `own` layers' animation states (6
bytes each), sheets and palettes are fixed by the figure file.

Step 4 built: direction sets (`directions`, `from`, `to` on an animation; nine aiming frames of
the test level's player with a `hand` point each), `aim` layers, and for the player's
attachments `sin`, `cos`, `angle_to_point`, `layer_aim`, `layer_turn` (the engine turns a
layer toward an angle: `layer_track` toward a target waits for handles, step 6),
`layer_angle`, `layer_play`, `layer_show`, `layer_hide`, `layer_palette`,
`layer_point_x/y/angle`, `spawn_at_point` (the new object's `angle`, an actor variable) and
`fly_angle` (runtime-core.md §7, §9). Where it differs: angles in scripts are world angles (0
to the right), not angles from forward; any layer whose animation is a direction set is
aimed, so `layer_play(#torso, #aim)` makes the torso aim without an `aim` mode; phases
(`aim_cast`) are a second direction set the script plays; `angle_to(target)` waits for
handles. A layer's state is 13 bytes of snapshot.

Step 5 built: a room object with `kind` and no class ([../spec/room-format.md](../spec/room-format.md)
§5): the record's first `attach` field is its class, `look`, `palette`, `extra` and `extra_at`
how it looks, and the class's first `record<S>` variable starts as the record. The test level's
Guard Room has an Iron Bat: `kind = IronBat`, a Fast Bat in red with an armour sheet (two
frames) drawn over its body at the bat's point `body`, and 12 health; no class, no new body
sheet. Equipment that shows: an armour's `look_palette` recolours the player's layers (the
chain mail: the coat in steel). Where it differs: the fields are found by name in any table
(no `EnemyKind` structure the engine knows; the test level adds them to `Enemy`); an object
has one extra layer, not a figure of its own (objects as figures, with `figure look`, are left
for later); a weapon layer filled by the equipped weapon is not built (the test level's
weapons are drawn into the player's frames); the Data tab does nothing new (a kind is a record
like any other, made there by copying a row).

Step 6 built: optional handles in Mondskript (`Actor?`, `null`, `if a := h { } else { }`;
[../spec/mondskript.md](../spec/mondskript.md) §11 "Handles"), handles with serials at run
time, `nearest_enemy`, `distance_to`, `angle_to`, `owner_face`, `layer_track`, `marker_show`
and `marker_hide`, `self` (runtime-core.md §7). The test pack's `LockOn` (Select locks on and
lets go, B shoots a `Spark` from the hand) and `Circler` (a bat flying a circle round the
player): the torso goes through all nine directions on both facings while the legs stand or
run, a snapshot taken mid-turn goes on byte for byte, a shot from the hand strikes the bat, and
the lock ends when the bat is gone. Where it differs: handles are of `Actor` only, not of a
class (`Bat?`); `t.alive()` is the check itself; the lock and the bolt are in the test pack,
not in the test level's spells (whose casts stay at the player's anchor), and there is no lock
button of its own (Select, the second quick slot's, here); the marker is a sprite named by the
script, drawn at the target's middle, not at a point `centre`. Step 7 is open. Milestone M18
in [../PLAN.md](../PLAN.md).

## Dependencies

Steps refer to section 9.

| Step | Needs |
|---|---|
| 1 to 3 (palettes and banks, sheets per room, points and layers) | nothing from other plans. Step 3 changes which animation the player's layers play, which [attachable-scripts.md](attachable-scripts.md) step 7 moves into the player's script: whichever comes second adapts |
| 4 (direction sets, angles, `spawn_at_point`) | `spawn` from attachable-scripts.md step 5; `sin`/`cos` shared with [ui.md](ui.md) step 5 |
| 5 (enemy kinds as records, equipment that shows) | [structures.md](structures.md) steps 2 and 4. The `kind` property of room objects is built here; attachable-scripts.md step 8 adds `attach` lists on top of it |
| 6 (the lock, spells from the hand) | attachable-scripts.md step 3, [hitboxes.md](hitboxes.md) step 4, and in the language: optional handles and the first query (specified in mondskript.md, not built) |
| 7 (figure view) | nothing more |

Both this plan and hitboxes.md change the sprite format to version 2; do that once. Needed
by: [first-items.md](first-items.md) steps 6 and 7 for aimed spells and variants,
[dialogue.md](dialogue.md) for portraits with palettes (plain sprites work without it).

## 1. What this is for

Today one object shows one sprite with one palette, and every sheet of a pack is loaded at
once: at most 8 sheets, each with the palette bank of its number
([../spec/runtime-core.md](../spec/runtime-core.md) §9). This plan adds:

- **palette variants**: the same drawing in other colours (a red bat, a poisoned player, a
  white flash when struck) without storing the tiles again;
- **layers**: a figure made of several sprites that are drawn together (body and weapon,
  legs and upper body, an enemy and its armour), each with its own animation;
- **variants as data**: "Red Armoured Bat" is a row in a table, not a new class or sheet;
- **direction sets**: one layer choosing its frame from an angle, so an arm can point at a
  target.

The example this plan must make possible: the player locks on to an enemy; while running,
jumping or standing, the upper body turns to the angle of the lock so the hand points at
the enemy, and the spell leaves from the hand in that direction.

## 2. Palettes

### Variants of one sprite

```xml
<sprite version="2" image="bat.png">
 <palette name="red"    image="bat_red.png"/>          <!-- the same pixels, other colours -->
 <palette name="frost">
  <swap from="#7a2c2c" to="#2c5c7a"/>                  <!-- or single colours replaced -->
 </palette>
 <palette name="ghost" ramp="grey" tint="#80a0ff"/>    <!-- or a rule -->
 ...
```

- The first palette is the sheet's own, named `default`. Every other one must map onto the
  same 15 colour slots: with a second image, the cooker pairs colours by position and
  reports a pixel pair that disagrees.
- Cooked: `SPRT` holds a palette table (name, 16 colours each) instead of one palette.
  32 bytes per variant, no tiles.
- A script chooses with `set_palette(name)`; a room object with a property `palette`; a
  variant record with a field (section 5).

### Banks on demand

There are 16 sprite palette banks. Instead of "bank = sheet number", a bank is given out
when a (sheet, palette) pair is first shown in a room and freed when nothing shows it:

| Banks | Use |
|---|---|
| 0 to 11 | given out on demand, counted per user |
| 12 | the flash palette (all white), shared |
| 13 | UI ([ui.md](ui.md)) |
| 14, 15 | the built-in figure and placeholder, as now |

A room that needs a thirteenth pair shows that figure with the pair's default palette (or
the nearest bank already loaded for that sheet) and reports it over the link; the editor's
room report counts the pairs a room can need, so this is seen before it happens.

### Palette effects

Short changes that need no script per frame: `flash(frames)` (the white bank for a moment
when struck), `tint(palette, frames)` (poison, frozen), `fade(palette, frames)` (a bank that
is blended toward another one, a few colours per frame). The first two only switch which
bank the figure's pieces use; `fade` needs a bank of its own.

## 3. Sheets loaded when needed

Layers and variants multiply sheets, so "all sheets of the pack, at most 8" has to go:

- Sheets are loaded into sprite tile memory when a room needs them (its objects' classes,
  their figures and variants, the player's equipment) and freed when no longer used, with
  the same counting as the banks. The player's sheets stay.
- The limit becomes the memory: 1024 tiles. The room report shows what a room loads and
  what is left; a sheet that does not fit is reported and its figure drawn as the
  placeholder.
- Large figures can mark animations as **streamed**: only the tiles of the frame shown are
  in memory, copied when the frame changes (at most a set number of tiles per frame). Not
  needed for the first version; named here because layers make it likely.

## 4. Figures and layers

A **figure** is what an object shows: up to 4 layers, each a sprite with its own animation
and palette.

```
# player.bmfigure
figure Player
layer Legs    sprite=player_legs                       # the root: placed at the object
layer Torso   sprite=player_torso  at=Legs.waist
layer Weapon  sprite=none          at=Torso.hand       # filled by the equipped weapon
layer Aura    sprite=none          at=Legs.centre  behind
```

### Points

A layer is placed at a **point** of another layer's current frame:

```xml
<frame x="0" y="0" w="24" h="24" ax="12" ay="24">
 <point name="waist" x="0" y="-14"/>
 <point name="centre" x="0" y="-16"/>
</frame>
<frame ...>                       <!-- a torso frame aiming up and forward -->
 <point name="hand" x="11" y="-9" angle="224" order="front"/>
</frame>
```

- Up to 4 points per frame, in pixels from the anchor, mirrored with the figure. A point
  missing on a frame falls back to the animation's, then the sprite's.
- `angle` (256 steps per turn, 0 = forward, counting clockwise on screen) is handed to
  whatever is attached there: a weapon layer can pick its own direction frame from it, a
  projectile starts with it.
- `order` says whether what hangs there is drawn in front of or behind this layer on this
  frame. That is how an arm goes behind the body when the figure turns away.
- So when the legs bob in the run animation, the waist point bobs, and torso, hand and
  weapon follow without any script.

### How a layer animates

Each layer has one of three modes:

| Mode | The frame shown is | For |
|---|---|---|
| own | its own animation state (as every sprite today) | legs and torso doing different things; a flapping cape |
| sync | the same step number as its parent's animation | an outfit or armour drawn frame for frame over the body; an enemy's armour layer |
| aim | chosen from an angle (section 6) | a torso or arm that points; a turret |

### Drawing and cost

- Layers are drawn in their order, moved by `behind` and by a point's `order`. All layers
  of a figure share its priority against the backgrounds.
- Each layer costs its pieces as hardware sprites. Limits to start with: 4 layers for the
  player, 2 for other objects, 12 pieces per figure; the 48 hardware sprites the game uses
  become 64. The editor's figure view shows pieces, tiles and banks of the whole figure and
  the worst line.
- Boxes ([hitboxes.md](hitboxes.md)) come from every layer's current frame: the body's
  hurt boxes from the legs and torso, the blade's hit boxes from the weapon layer, already
  at the right place because the layer is.

### Cooked

`SPRT` version 2 also gets, per frame, a point list (`s8 x, y; u8 angle; u8 name and
order`) and a table of point names. A `FIGR` chunk per figure: its layers with sprite name,
parent point, mode, default palette and order.

## 5. Variants as data

A variant is a record that names a class's look and numbers:

```
struct EnemyKind {
  text    title;
  attach  behaviour;            // the class: Bat
  figure  look;                 // bat.bmfigure
  name    palette = #default;
  sprite  extra;                // an additional layer: armour, a crown, wings
  name    extra_at;             // the point it hangs on
  int     health; int attack; int defence;
  Element weak; Element resists;
  record<Item> drop; int drop_chance;
}

record Bat          { behaviour = Bat  look = "bat"  health = 4  weak = Holy }
record RedBat : Bat { title = "Red Bat"  palette = #red  health = 8  attack = 2 }
record IronBat : RedBat { extra = "bat_armour"  extra_at = #body  defence = 3 }
```

- A room object gets a property `kind` (`kind = IronBat`) instead of a class; the runtime
  creates the class the record names, sets up figure, palette and extra layer, and hands
  the record to the script (`var record<EnemyKind> Kind`). The script reads its numbers
  from it, so one `Bat` class serves every bat.
- An extra layer in `sync` mode is drawn over the same frames as the body, so an armoured
  variant needs one more sheet with the armour only.
- A layer can be taken off at run time (`layer_hide(#Extra)` when the armour breaks), which
  makes "variant" and "state of the enemy" the same mechanism.
- The same goes for the player: a weapon record names the sprite for the `Weapon` layer; a
  piece of armour may name a palette for `Torso` and `Legs`. Equipment that shows costs one
  field.

## 6. Angles, aiming and the lock

### Direction sets

An animation can be a **direction set**: its steps are not played in time, each stands for
a direction.

```xml
<animation name="aim" directions="9" from="192" to="64"/>   <!-- straight up ... straight down -->
```

- `from` and `to` are angles (256 per turn, 0 = forward, 192 = up, 64 = down); the steps
  are spread evenly between them. Nine steps over the front half-turn are 22.5 degrees
  apart. Only directions in front of the figure are drawn; a target behind it turns the
  figure round first, and mirroring does the rest.
- A layer in `aim` mode shows the step nearest to its angle. Each of those frames has its
  own `hand` point and point angle.
- A direction set can have **phases**: `aim_cast` with the same directions is the casting
  pose; switching from `aim` to `aim_cast` keeps the direction.

### Angles in scripts

New API entries, all whole numbers and table based, so they are the same on every machine:

| Function | Result |
|---|---|
| `angle_to(target)`, `angle_to_point(x, y)` | the angle from the figure (or from one of its points) to there, 0 to 255 |
| `sin(angle)`, `cos(angle)` | `fix` (also in [ui.md](ui.md) §5) |
| `layer_aim(layer, angle)` | sets an `aim` layer's angle |
| `layer_track(layer, target, speed)` | the engine turns the layer toward the target every frame by at most `speed` steps; no script per frame |
| `layer_point_x/y(layer, point)`, `layer_point_angle(layer, point)` | where a point is right now, in room pixels, and its angle |
| `layer_play(layer, anim)`, `layer_show(layer, sprite)`, `layer_hide(layer)`, `layer_palette(layer, palette)` | the rest of the layer control |
| `spawn_at_point(class, layer, point)` | creates an object at a point, with the point's angle as its direction |
| `fly_angle(angle, speed)` | for the object created: velocity from an angle ([hitboxes.md](hitboxes.md) §5) |

### The lock

An attachment, like every other ability ([attachable-scripts.md](attachable-scripts.md)):

```
class LockOn extends Attachment;
var Actor? Target;

event press(name button) {
  if button == #Lock { Target = nearest_enemy(96, #InFront); refresh(); }
}
event tick() {
  if t := Target {
    if !t.alive() || distance_to(t) > 128 { Target = null; refresh(); }
  }
}
function refresh() {
  if t := Target {
    owner_face(t);
    layer_play(#Torso, #aim);
    layer_track(#Torso, t, 6);            // 6 steps of 256 per frame: a quick, smooth turn
    marker_show(t, #LockRing);
  } else {
    layer_play(#Torso, #idle);            // back to the torso's ordinary animation
    marker_hide();
  }
  send(#LockChanged, 0);
}
```

and the spell reads where the hand is:

```
class SpellBolt extends Attachment;
event press(name button) {
  if button == #Spell && pay(Item.cost) { gotostate Cast; }
}
state Cast { begin:
  layer_play(#Torso, #aim_cast);          // keeps the direction the lock gave it
  sleep(6);
  spawn_at_point(Item.shot, #Torso, #hand);
  sleep(8);
  layer_play(#Torso, #aim);
  gotostate Ready;
}
```

What happens, frame by frame: the legs layer goes on with `run` or `jump` from the player's
movement as today. The engine turns the torso layer's angle toward the target and shows
the nearest of the nine frames. That frame's `hand` point places the weapon layer and, at
the cast, the bolt, which starts with the point's angle. Without a lock the torso's `aim`
angle is 0 (straight ahead) and the same spell script works unchanged.

- `nearest_enemy(range, flags)` is the first of the queries
  ([../spec/mondskript.md](../spec/mondskript.md) §6), and `Actor?` the first optional
  handle; both come with this plan in the small form the example uses.
- The lock marker is a sprite the engine draws at the target's `centre` point.
- The player's five movement animations then belong to the legs layer; a figure with one
  layer behaves exactly as now.

## 7. State

Per figure, flags `snapshot`: for each layer the sprite (by name hash), palette, mode,
animation state (4 values as today) or angle, track target and speed, shown or hidden;
palette effect and its time. About 16 bytes per layer: 64 for the player, 32 per object.
Bank and tile assignments are `derived` and rebuilt after loading, as video memory is.

## 8. Creating them: the editor

**Figure view** in the Animation tab:

- The layers of a figure as a list (sprite, parent point, mode, palette, order), composed
  in the preview. Each layer's animation can be chosen and played on its own, so run legs
  under an aiming torso can be judged.
- **Points**: shown as crosshairs with a small arrow for the angle; drag to move, turn the
  arrow to set the angle, copy to following frames, onion skin. A layer attached to a point
  moves live while the point is dragged.
- **Direction sets**: a dial. Turning it shows the frame the angle picks and where the hand
  points; a line from the hand point along its angle shows whether the drawing and the
  angle agree. A "drag a target around" mode shows the whole lock behaviour without the
  game.
- **Palettes**: the variants of a sprite side by side on the same frame; a colour of a
  variant can be picked and changed (written as a `swap`), and a ramp can be shifted as a
  whole. Importing a recoloured image makes a variant from it.
- **Budget**: hardware sprites, tiles, banks and the worst line for the composed figure,
  in the same colours as the hardware window.

**Drawing the layers.** A layered figure is drawn as one sheet per layer with the same
grid; the cooker cuts each on its own. For a body drawn whole, a helper splits a sheet into
two layers by a mask image (legs below, torso above) and puts a `waist` point on every
frame at the cut, which gives a first version to refine.

**Data tab.** An `EnemyKind` row shows a thumbnail of the composed variant. A new variant
is "duplicate row, pick palette".

**Running game.** The State tab lists each figure's layers; the room view can show points
next to the boxes.

## 9. Steps

1. Palette table in `SPRT`, banks on demand, `set_palette`, room property, `flash`.
   Test: a red and a default bat in one room use one sheet and two banks.
2. Sheets loaded per room with counting; the room report's sprite memory lines.
3. Points in frames; figures with layers in `own` and `sync` mode; `FIGR`; drawing order.
   The player as legs and torso. Test: the composed player is pixel for pixel the old one
   where the drawings are the same.
4. Direction sets, `aim` mode, the angle functions, `layer_track`, `spawn_at_point`.
5. `EnemyKind` records and the `kind` property; extra layers; equipment that shows.
6. Optional handles and `nearest_enemy`; the lock attachment, the marker; the demo's
   spells cast from the hand.
7. The figure view: layers, points, dial, palettes, budget.

Done when: in the demo the player locks on to a bat that flies in a circle; the torso and
hand follow it through all nine directions while the legs run and jump, on both facings;
the bolt leaves from the hand and strikes the bat; a snapshot taken mid-turn continues on
the other machine with the same checksums; and "Iron Bat" (red palette, armour layer,
other numbers) was made in the Data tab without a new class or a new body sheet.

## 10. Open questions

- Nine directions over the front half-turn is a guess at what looks smooth at this size;
  the dial in the editor is there to find out. A sprite could instead rotate its arm with
  an affine hardware sprite (smooth, but blurry at low resolution and limited to 32
  matrices); the point's angle would feed that too. Not planned for the first version.
- Whether `sync` layers should share piece layouts with the body to save hardware sprites
  when both are small. Only worth it if the budgets get tight.
- Palette variants of **tilesets** (the same room at night) are the background's version of
  this and are not covered here.
