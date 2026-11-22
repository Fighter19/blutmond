# Plan: hit and hurt boxes

Status: step 1 built: boxes in the sprite file and in `SPRT` version 2
([../spec/sprite-format.md](../spec/sprite-format.md)), read by the runtime
(`bmSpriteSheetBoxes`); the Animation tab lists the boxes a frame has (its own and the
sprite's it gets), adds, deletes and edits them by their numbers or with the mouse in the
preview (select, move, resize at edges and corners, draw a new one), and moves a box between
the frame and the whole sprite (its scope). Not yet: boxes on an animation, copying them to
other frames, onion skin (section 7).
Step 2 built ([../spec/runtime-core.md](../spec/runtime-core.md) §10, `src/world/hits.c`):
`arm`, `disarm`, `hit_box`, teams, the decision step once a frame, the struck list,
invulnerability, `hit(amount)` and `struck(amount)`; health for the player and objects. The
bat's bite is an armed box now, the test level has a training dummy, and a `Jab` attachment
(`tests/scripts/jab.ms`, in the test pack with wind only) strikes with a rectangle. Where it differs from sections 3 and 4: 8 hits at a
time, not 16; the struck list is a bit per figure (every figure, not 6); `hit_box(reach,
frames, damage)` is a rectangle ahead of the figure over its body's height (a script call
takes at most 3 arguments), not one placed by numbers; events carry only the amount, not the
target or the element; the damage rule is `max(1, damage)` for objects and `max(1, damage -
Defence)` for the player, without elements; the
player's hurt timer is their invulnerability, also against hazards. A first projectile (the
moon bat's fireball) is an object made at run time that moves itself and arms its own hit box:
not yet the engine movement, wall event, team inheritance and stretched test of step 4.
Steps 3 to 5 are open. Milestone M17 in [../PLAN.md](../PLAN.md). Fills in what
[attachable-scripts.md](attachable-scripts.md) §6 only names ("hit boxes"), and is what the
weapons and spells of [first-items.md](first-items.md) hit with.

## Dependencies

Steps refer to section 8.

| Step | Needs |
|---|---|
| 1 (boxes in the sprite file, drawing them in the editor) | nothing from other plans |
| 2 (deciding hits, arming) | health, and attachments as the source of a hit: [attachable-scripts.md](attachable-scripts.md) steps 3 and 5 (this step is the hit-box part of its step 5). The damage rule reads stats: [structures.md](structures.md) step 3 |
| 3 (guard, pierce, hit stop, knock) | `knock_back` from attachable-scripts.md step 7 for knocking the player |
| 4 (projectiles) | objects created at run time: attachable-scripts.md step 5. `fly_angle` and `aim_at` need the angle functions of [layered-sprites.md](layered-sprites.md) step 4 |
| 5 (live box view, timeline, frame data) | the Data tab column needs structures.md step 5 |

Boxes on several layers of one figure need layered-sprites.md step 3. Needed by:
first-items.md steps 2 and 6, layered-sprites.md step 6.

## 1. What this is for

Today a sprite frame has one box, used to tell whether the player touches an object
([../spec/sprite-format.md](../spec/sprite-format.md)). An attack needs more:

- where a figure **can be hit** changes during a move (it ducks, lunges, stretches an arm
  out);
- where it **hits** exists only on some frames, moves with the weapon, and may be several
  boxes at once;
- some frames **guard** (a shield, a parry);
- a hit must count **once per swing**, however many frames the boxes overlap;
- all of it must be visible while drawing the animation and while the game runs, or nobody
  can tune it.

## 2. Boxes

Three kinds, all rectangles in whole pixels relative to the frame's anchor, mirrored with
the figure's facing:

| Kind | Meaning |
|---|---|
| `hurt` | the figure can be hit here |
| `hit` | this hits, while it is armed (section 3) |
| `guard` | a hit that reaches this box first is blocked |

The box used for touching and for standing in the world stays what it is (`hitbox` in the
sprite file; called the **body box** from here on). A frame without `hurt` boxes uses the
body box as its hurt box, so nothing has to be drawn for simple enemies.

### In the sprite file

```xml
<frame x="64" y="0" w="48" h="32" ax="12" ay="32">
 <box kind="hurt" x="-5" y="-22" w="10" h="22"/>          <!-- ducked while lunging -->
 <box kind="hit" name="blade" x="6" y="-18" w="26" h="6"/>
 <box kind="hit" name="blade" x="28" y="-24" w="8" h="14"/>
</frame>
```

- Up to 4 boxes per kind and frame. A `hit` or `guard` box has a **name**; boxes with the
  same name belong together (a blade made of two rectangles). A sprite has at most 8 box
  names.
- Boxes can also be given on the sprite (for every frame) or on an animation (for every
  frame while that animation plays); the frame's own boxes of a kind replace those.
- Coordinates from -128 to 127, sizes up to 255: enough for a whip.

Because boxes are per frame, an animation's steps decide the timing: the frames with a
`blade` box are the active frames. Startup and recovery are no longer numbers that have to
match a drawing; they are the steps before and after.

### Cooked

`SPRT` version 2: a frame gains `u16 first box, u8 box count`, and the chunk a box table
(6 bytes each: `s8 x, y; u8 w, h; u8 kind; u8 name`) and a table of box names (string ids).
A version 1 chunk reads as "no boxes".

## 3. Arming

A `hit` box does nothing by being drawn. A script arms it and says what it does:

```
state Swing { begin:
  owner_lock(anim_length(Item.swing));
  owner_play_anim(Item.swing);
  arm(#blade, Item.attack + stat(#Attack), Item.element);
  finish_anim();
  disarm(#blade);
  gotostate Ready;
}
```

- `arm(name, damage, element)` starts **one hit**: from now until `disarm`, the end of the
  animation or the owner being hurt, every box of that name on the frame shown is live.
  The same figure, frames and animation can so be a weak or a strong attack.
- Arming again starts a new hit (a combo's second swing hits the same target again).
- `arm` has optional flags: `#Pierce` (not stopped by guard boxes), `#Multi(n)` (may hit
  the same target again every n frames: a drill, a flame), `#Knock(x, y)` (push).
- A box without a drawing: `hit_box(x, y, w, h, frames, damage, element)` makes a
  rectangle from numbers for that many frames, as one hit. That is for reach that comes
  from data (the weapons' `reach`) and for effects without a sprite (the Blood Moon spell).

Each hit has a **source**: the figure it belongs to and, if an attachment armed it, that
attachment. The source gets the `struck` event.

## 4. Deciding hits

Once per game frame, after scripts and animations have run and before drawing:

1. Collect the live hit boxes (at most 16) and, for every figure, its hurt and guard boxes
   of the frame shown.
2. For each hit, in a fixed order (player first, then objects by index), for each figure of
   another **team** (player, enemies, neutral; a hit names the teams it reaches), in index
   order:
   - no overlap between any of the hit's boxes and the figure's hurt or guard boxes: next;
   - the hit has already struck this figure (and is not `#Multi`): next;
   - a guard box overlaps and the hit is not `#Pierce`: the figure gets `blocked(source)`,
     the source gets `guarded(target)`, and the hit is spent for this figure;
   - else the figure is struck: amount through the damage rule and the figure's `damage_in`
     filters ([first-items.md](first-items.md) §1), then `hit(amount, element)` to the
     figure and `struck(target, amount)` to the source.
3. A struck figure cannot be struck again for its invulnerability time (by any hit).

- "Already struck" is kept per hit as a list of up to 6 figures. A hit that has struck 6
  strikes no more.
- Everything is whole numbers and fixed orders, so the result is the same on every machine
  ([../spec/runtime-core.md](../spec/runtime-core.md) §1).
- Cost: 16 hits x 17 figures x a few rectangle tests; with the teams and a first test on
  each figure's bounding box, well under 1 % of a frame.

**Hit stop** (optional, per hit): both figures hold their frame for n frames. It makes
hits readable and costs a counter.

## 5. Projectiles

A projectile is an ordinary object created at run time (`spawn`,
attachable-scripts.md §6) with a sprite, and it hits through the same step:

```
class Bolt extends Actor;
var record<Spell> From;

event spawn() {
  fly(From.speed, 0);               // engine moves it: velocity, optional gravity
  lifetime(From.frames);
  arm(#core, From.power + source_stat(#Attack), From.element);
}
event struck(Actor target, int amount) { play_sound(#Burst); destroy(); }
event wall() { destroy(); }
```

- **Team and source** are inherited from whoever spawned it, so the player's bolt does not
  hit the player, and `struck` also reaches the attachment that cast it (for a spell that
  heals on hit, a combo counter).
- **Movement** is the engine's: `fly(vx, vy)`, `gravity(g)`, `lifetime(frames)`,
  `aim_at(target, speed)` or `fly_angle(angle, speed)`
  ([layered-sprites.md](layered-sprites.md) §6). The event `wall()` comes when its centre
  enters a solid cell; a script can `destroy()`, `bounce()` or ignore it (a ghost flame).
- **Fast ones do not skip through.** Each frame the live boxes of a moving projectile are
  stretched back along the way it came this frame before the test. Speed is limited to
  16 pixels per frame.
- **Several targets**: by default a hit is spent on the first figure struck and the usual
  script destroys the projectile there. `#Multi` or simply not destroying it makes a
  piercing shot; the "already struck" list keeps it from hitting one figure every frame.
- **Projectiles can be hit.** One with a `hurt` box can be destroyed by a weapon (a thrown
  axe knocked down). A hit with the flag `#Reflect` that strikes a projectile turns its
  team and direction around instead (a parry sending a fireball back); its source becomes
  the one who reflected it.
- **Limits**: projectiles come out of the room's object pool (16); a class says how many of
  it one source may have alive (`max_alive = 2` for the knives), and a spawn beyond that
  does nothing and tells the script.

A beam or a lingering cloud is not a projectile but a `hit_box` or an armed box on an
effect sprite that stays in place.

## 6. State

Per live hit: source, name or rectangle, damage, element, flags, frames left, the struck
list. Sixteen of them, about 24 bytes each, flags `snapshot`. Per figure: invulnerability
time and hit stop (the player's `hurtTimer` exists). A snapshot taken in the middle of a
swing continues it with the same hits still to come.

## 7. In the editor

**Animation tab**

- A **Boxes** layer over the frame: drag to draw, handles to resize, a kind and name per
  box, numbers in the side panel. Colours: hurt green, hit red, guard blue, body white.
- **Copy** a frame's boxes to the next, or to all frames of the step range selected;
  **onion skin** of the previous frame's boxes.
- A **timeline row per box name** under the animation's steps: filled where that box exists.
  It shows startup, active and recovery frames as counts, per animation.
- **Reach preview**: the union of all `hit` boxes of an animation, drawn over the idle
  frame, and a second figure that can be dragged to see on which frame it would be struck.
- The report warns about: an animation whose `hit` boxes are never armed by any script that
  plays it; a box name a script arms that the sprite does not have; a frame with a `hit`
  box and no `hurt` box (cannot be punished: allowed, but worth seeing).

**Data tab.** A weapon or spell row shows, read from the animation it names, the frames
before the first live box, the live frames and the frames after, next to its damage: the
table then reads like a frame-data sheet.

**Running game.** A **Boxes** switch in the room view and the game view draws every body,
hurt, guard and live hit box of the running game over the picture, and flashes a box that
struck. The editor works them out itself from the game state it already gets over the link
and from the pack, so the runtime sends nothing new and it works on the GBA as on the PC.
Stepping the game frame by frame (the link's debugger halt) with boxes on is how attacks
get tuned.

**On the device.** The Start menu's System page gets a "show boxes" option in development
builds: outlines drawn with the UI layer's sprites, limited to the player and the nearest
four figures.

## 8. Steps

1. Sprite file and `SPRT` version 2 with boxes; reader; the Boxes layer in the Animation
   tab (drawing and saving only).
2. The decision step with teams, `arm`/`disarm`, `hit_box`, the struck list,
   invulnerability; `hit`, `struck`. Test: a swing with a box on three frames strikes the
   dummy once; armed twice, twice.
3. Guard boxes, `#Pierce`, `#Multi`, hit stop, knock.
4. Projectiles: `fly`, `lifetime`, `wall`, team and source, the stretched test, `max_alive`,
   reflection.
5. The editor's live Boxes view, timeline rows, reach preview, report warnings; frame data
   in the Data tab.

Done when: the Great Axe's swing has hurt boxes that change from frame to frame and two hit
boxes, strikes the dummy exactly once and only on the frames drawn for it; a knife thrown
at 12 pixels per frame cannot pass through the bat; a reflected bolt strikes the wisp that
shot it; all of it gives the same state checksums on GBA and PC; and the editor shows the
boxes over the game running on the GBA.

## 9. Open questions

- Rectangles only. A circle for explosions would be cheap to add; rotated boxes are not
  planned.
- Whether enemies' attacks should use the same arming (yes by design) or keep
  `hurt_player` on touch as a shortcut. Both work side by side; touch damage is a hit with
  the body box.
