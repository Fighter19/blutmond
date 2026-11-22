# Plan: attachable scripts

Status: steps 1 and 2 built: one bytecode module per class, native images with a module per
class ([../spec/mondskript.md](../spec/mondskript.md) §10 and §12), the reload mode `module`
([../spec/runtime-abi.md](../spec/runtime-abi.md) §5). The layout hash of section 4 is
worked out by the runtime from the class table instead of being stored in the module header;
an object whose module changed restarts its state's code whether or not that code changed.
Step 3 built: `Attachment` classes on the player
([../spec/mondskript.md](../spec/mondskript.md) §11 "Attachments"), the test level has a
`HighJump` attachment that makes jumps go higher. Where step 3 differs from sections 3 and 9:
8 slots, all the player's, no attachments on actors yet; the game buttons' mapping is fixed;
attachments keep no `save` variables; `impulse` (section 7) came early, for the test;
`jumped()` and `landed()` arrive after the body step. Steps 4 to 8 are open. Milestone M14 in [../PLAN.md](../PLAN.md).

## Dependencies

Steps refer to section 11.

| Step | Needs |
|---|---|
| 1, 2 (one module per class, reload of one module) | nothing from other plans |
| 3 (attachments on the player, game buttons) | nothing from other plans |
| 4 (stats, capabilities, filters, actions) | the stats of [structures.md](structures.md) step 3 |
| 5 (spawn, hit boxes, health, messages) | the hit-box part **is** [hitboxes.md](hitboxes.md) steps 1 and 2; build them as one piece of work |
| 6 (attachments from the inventory) | structures.md step 4 |
| 7 (the player as a script, its attributes) | steps 3 and 4. The example's `layer_play` needs [layered-sprites.md](layered-sprites.md) step 3; without it the script uses `play_anim` |
| 8 (attachments and values from the level) | step 3. The `kind` property itself is built in layered-sprites.md step 5; this step makes `attach` lists and attachment values work together with it |

Needed by: [ui.md](ui.md) step 3, structures.md (resolving `attach`),
[first-items.md](first-items.md), hitboxes.md steps 2 and 4, layered-sprites.md step 6,
[dialogue.md](dialogue.md) step 5. [versioning.md](versioning.md) gains from step 1 (an
update for one script is one small chunk) without needing it.

## 1. What this is for

Today a script class belongs to one object placed in a room (section 8 says how it gets
there), all classes are compiled into one module, and the player has no script at all
([../spec/mondskript.md](../spec/mondskript.md) §11). A sword's swing, a double jump or a
poison effect would have to be written into one growing "player" script.

Instead: **small scripts that are attached to an owner at run time.** A weapon is a record
plus one script; changing the weapon detaches one script and attaches another; an ability is
a script that is attached when the ability is found. No script needs to know which others
exist.

This is run-time composition. The compile-time kind (`has Health, Flight`, mondskript.md §3)
stays planned for sharing fields and functions between classes; the two do not replace each
other.

## 2. Kinds of class

| Base | An instance exists | Created by |
|---|---|---|
| `Actor` | per object in the room | the room, or `spawn` (section 6) |
| `Player` | one | the engine, from the project setting (section 7) |
| `Attachment` | per (owner, slot) | the inventory, the level (section 8), or `attach` |
| `Screen`, `Effect` | while shown | the UI ([ui.md](ui.md)) |

All are the same thing to the interpreter: class, state, wait, place, timer, variables
(mondskript.md §11, "An object and its state code"). So states, state code, `sleep`, save
states in the middle of a script and native modules work for all of them without new
machinery. The base class only decides which events arrive and which runtime functions may
be called (checked by the compiler from the API table, which gets a "who may call" column).

## 3. Attachments

```
class WeaponSwing extends Attachment;

var record<Weapon> Item;          // set by the runtime from the slot's record

event press(name button) {
  if button == #Attack && !owner_busy() { gotostate Swing; }
}

state Swing { begin:
  owner_lock(Item.startup + Item.recovery);
  owner_play_anim(Item.swing);
  play_sound(Item.sfx);
  sleep(Item.startup);
  hit_box(4, -20, Item.reach, 16, Item.attack + stat(#Attack), Item.element, 3);
  sleep(Item.recovery);
  gotostate Ready;
}
```

- **Owner**: the player, an actor, or a screen. An attachment cannot have attachments.
- **Slots.** The player has the equipment slots the game declares (structures.md §5), one
  slot per ability owned, and 4 free slots for temporary effects (poison, a spell's buff).
  An actor has 2. The pool holds 24 attachments in all.
- **Where they come from.** For equipment and abilities the runtime does it: when a record
  with an `attach` field enters a slot, its class is attached and gets the record; when it
  leaves, the class is detached. `attach(slot, Class)` and `detach(slot)` do the same from a
  script for the free slots, with an optional lifetime in frames.
- **Order.** Each frame: the owner first, then its attachments in slot order. The order is
  fixed, so the game stays deterministic.

### Events an attachment gets

| Event | When |
|---|---|
| `attached()`, `detached()` | once each |
| `tick()` | every game frame |
| `press(name)`, `release(name)` | player only: a game button (`#Attack`, `#Jump`, `#Spell`, `#Use`, `#Dash`) changed; the mapping from keys to buttons is the game's, changeable in the options |
| `landed()`, `jumped()`, `hurt(int)`, `died()`, `entered_room()` | what happened to the owner this frame |
| `struck(Actor, int)` | a hit box of this attachment hit something |
| `used()` | the record was used from the menu or a quick slot (consumables) |
| `message(name, int)` | section 5 |

### Hooks: changing what the engine does

The player's movement stays engine code ([../spec/runtime-core.md](../spec/runtime-core.md)
§5): it has to be fast and the same on every machine. Attachments change it through
numbers, not by replacing it:

- **Stats and capabilities.** `stat_add(#AirJumps, 1)` in `attached()` is all a double jump
  needs; the body step reads the total. Capabilities to start with: `AirJumps`, `Dash`,
  `WallSlide`, `Swim`, `HighJump`, plus speed, jump and gravity factors. What an attachment
  added is taken away again when it is detached, by the runtime.
- **Filters.** `event int damage_in(int amount, Element e)` and `damage_out` are asked in
  slot order, each getting the previous result. That is armour, a shield spell, a weakness.
- **Actions.** `owner_lock(frames)` takes the player's input for a while (a swing, a
  dodge); `owner_set_velocity`, `owner_play_anim`. Only one action at a time; `owner_busy()`
  tells.

## 4. One module per script

The cooker writes one `MODB` chunk per class instead of one for all. This fits what is
already true: a `CALL` can only reach a function of the same class, and classes refer to
each other only by name (a room object's class, an `attach` field, `spawn`, a message).

- A module's header gains the class's name id and a **layout hash** over its variables
  (names, types, flags).
- At load the runtime builds a table name → module. A name nobody defines is ignored, as an
  unknown object class is today.
- **Changing one script** changes one chunk of a few hundred bytes. The live link already
  sends only chunks that differ; the reload mode `module`
  ([../spec/runtime-abi.md](../spec/runtime-abi.md) §5) then swaps that class alone:
  instances whose layout hash is unchanged keep their variables; their place goes back to
  the start of their state if that state's code changed; instances of a class whose layout
  changed are created anew. Every other script keeps running where it was.
- **Native code** per class as well: the project marks which classes are worth it, and
  those share the IWRAM slot (8 KB); the rest runs as bytecode. Both can be mixed freely
  because they keep the same state.
- The limits per class stay (8 variables, 512 words per function); the limit of what a game
  can do moves from "one module" to "how many classes".

## 5. Messages

Attachments and their owner talk without knowing each other:

```
send(#Ignite, 60);          // to the owner and all its attachments
send_to(target, #Freeze, 90);   // to another actor and its attachments
```

`event message(name what, int value)` receives it in the same frame, in slot order; a
message sent from inside a message handler is delivered after the current one (a queue of
8, more is dropped and reported). No references to instances are kept, so nothing dangles
when a script is exchanged.

## 6. What attachments need from the engine

These are engine work this plan brings along, because a weapon is useless without them:

- **Objects created at run time**: `spawn(Class, x, y)` for projectiles and effects, taken
  from the room's object pool (raised from 8 to 16), gone on a room change or when they call
  `destroy()`.
- **Hit boxes** (in full: [hitboxes.md](hitboxes.md)): `hit_box(x, y, w, h, damage, element, frames)` relative to the owner and
  mirrored with its facing; tested against the hitboxes sprites already carry
  ([../spec/sprite-format.md](../spec/sprite-format.md)). A hit gives the target
  `hit(amount)` after its `damage_in` filters and the attacker `struck`.
- **Health** for the player and for actors: current value in the state (`save link` for the
  player), maximum from the stat `MaxHealth`. Today the player can be hurt but has no
  health.
- **Game buttons** instead of raw keys.

## 7. The player as a script

The player is not scriptable today: its movement, animation choice, hurt, respawn and door
handling are engine code ([../spec/runtime-core.md](../spec/runtime-core.md) §5, §6, §9),
and no script class stands behind it. Attachments alone would leave the core of the player
fixed. So the player becomes a scripted object like any other, with the engine's movement
as a service it uses:

```
class Hero extends Player;

var(Move) fix RunSpeed = 1.5;
var save   int Level = 1;

event spawn()            { layer_play(#Legs, #idle); }
event hurt(int amount)   { if health() > 0 { gotostate Hurt; } }
event died()             { gotostate Dying; }
event entered_room()     { }

state Normal {
  event tick() { choose_move_anim(); }       // the engine's rule, or the script's own
}
state Hurt { begin:
  control(false);  knock_back(1.5, -2.0);  flash(8);
  sleep(20);
  control(true);
  gotostate Normal;
}
state Dying { begin:
  control(false);  layer_play(#Legs, #die);  finish_anim();
  respawn();
  gotostate Normal;
}
```

- **What stays engine code**: the body step (collision, slopes, ladders, water, jump
  rules). It is the part that must be fast and exact, and it is driven by numbers the
  script and its attachments set: speeds, the capabilities of section 3, and
  `control(bool)` for whether the buttons reach it.
- **What moves into the script**: which animation plays when (`choose_move_anim()` does
  what the engine does today, so a script that only calls it changes nothing), what happens
  when hurt and when dying, respawning, what a door or a save point does to the player,
  cutscene control (`walk_to(x)`, `control(false)`).
- **`Player` is a base class** with the events of the attachments' table (section 3) plus
  `spawn`, `tick`, `touch`. The API functions about "the owner" (`owner_lock`,
  `owner_play_anim`, `stat_add`) are the same functions without the prefix here.
- **Which class is the player** is a project setting (`player = Hero`), and a `PlayerStart`
  object can override it with a property `class` (a room played as another character).
  The engine ships a `DefaultPlayer` script that reproduces today's behaviour exactly; a
  project without the setting uses it. Test: the test level gives the same state checksums
  with `DefaultPlayer` as with the engine code it replaces.
- **Order per frame**: the player's script, then its attachments in slot order, then the
  body step, then the room's objects. So what a weapon decides in a frame moves the player
  in the same frame.
- **State**: the player's script object (class, state, wait, place, timer, variables) is a
  new group of fields in `game` with the flags `snapshot`, its `save` variables as for any
  object. A snapshot taken while the player is in `Hurt` continues there.
- The player's class has the limits of every class (8 variables); what does not fit belongs
  in attachments, which is the point of this plan.

### The player's attributes

Not possible today: every number of the player's movement is a constant in the engine
([../spec/runtime-core.md](../spec/runtime-core.md) §5), scripts can read the player's
position (`player_x`, `player_y`) and hurt it (`hurt_player`), and nothing else. Planned:
everything below can be read, and what is marked can be written, by the player's script,
by its attachments, and (through `player.`) by any other script, such as a wind zone, a
teleporter or an enemy's grab.

**What the player is doing** (state):

| Attribute | Write | Notes |
|---|---|---|
| `x`, `y` | yes | `fix`, the feet. Writing moves the player there at once, without a collision test (a teleport); velocity is kept. `safe_x`, `safe_y` follow only when the new place is safe ground |
| `vx`, `vy` | yes | speed in pixels per frame; used by the next body step |
| `facing` | yes | -1 or 1 |
| `health`, `mana` | yes | limited to 0 and the maximum |
| `on_ground`, `climbing`, `in_water`, `air_jumps_left` | no | what the body step found |
| `invulnerable` | yes | frames left during which nothing strikes |
| `control` | yes | whether the buttons reach the body step (`control(false)` in a cutscene or while knocked back) |

**How the player moves** (tuning; the defaults are today's constants, so nothing changes
until a script changes one):

| Attribute | Default | |
|---|---|---|
| `run_speed` | 2.0 | |
| `accel_ground`, `accel_air` | 0.5, 0.25 | |
| `friction` | 0.5 | |
| `jump_speed` | 1357/256 | |
| `jump_cut` | 0.5 | how much of the rise is kept when the button is let go |
| `climb_speed` | 1.0 | |
| `gravity_scale`, `fall_scale` | 1.0 | multiply the room's gravity and fastest fall |
| `water_speed_scale` | 0.5 | |
| `coyote_frames`, `jump_buffer_frames` | 6, 6 | |
| `box_half_width`, `box_height` | 6, 28 | a change that would put the box into a wall is refused and returns false (standing up under a low ceiling) |
| `hurt_frames` | 60 | invulnerability after being hurt |
| `knock_x`, `knock_y` | 2.0, 3.0 | the push when hurt, away from the facing and upward |

**Two ways to change a tuning value**, for two different needs:

- **Set it**: `run_speed = 2.5` in the player's own script changes the base value for good
  (a level-up, another character).
- **Modify it while something lasts**: `modify(#run_speed, #Mul, 0.8)` or
  `modify(#jump_speed, #Add, 1.0)` from an attachment. The value in use is the base, times
  all factors, plus all additions. A modifier belongs to the attachment that made it and
  disappears with it, so heavy armour, a haste spell and a slowing swamp can overlap and
  each end on its own without any of them knowing the others. The capabilities of section
  3 (`AirJumps`, `Dash`) are the same mechanism.

**Pushing the player around** without taking over its speed:

| Function | Effect |
|---|---|
| `knock_back(vx, vy, frames)` | sets the speed, takes control away for that many frames, lets friction and gravity act; ends early on landing if asked (`#UntilGround`). Being hurt calls it with `knock_x`, `knock_y`; an enemy's hit can pass its own values through `#Knock` ([hitboxes.md](hitboxes.md) §3) |
| `impulse(vx, vy)` | added to the speed once (an explosion, a spring) |
| `push(vx, vy)` | moves the player by this much this frame, through the collision test, without changing its speed (wind, a conveyor, a moving platform); has to be called every frame it should act |
| `move_to(x, y)` | like writing `x`, `y`, but refused if the box does not fit there |
| `walk_to(x, speed)` | cutscenes: the body step walks there as if a direction were held; latent in state code |

All of it goes through the one body step, so collision stays exact: scripts decide where the
player wants to go and how fast, the engine decides where it ends up.

**State.** The base tuning values are new `fix` fields of `game` with the flags `save link`
(a character that was levelled up stays that way). Modifiers are part of each attachment's
record (`snapshot`); the values in use are `derived`, worked out again after loading and
whenever a modifier changes.

## 8. From the level to a running script

How an object placed in a room gets its script. The first three points are how it works
today ([../spec/runtime-core.md](../spec/runtime-core.md) §8,
[../spec/room-format.md](../spec/room-format.md) §5); the rest is planned.

**Today**

1. In Tiled, an object in a layer with the role `objects` has a **class** (the `type` field
   before Tiled 1.9). The cooker stores the class name, the object's id, position, size,
   name and its custom properties as text ([../spec/room-format.md](../spec/room-format.md)
   §7, `OBJS`).
2. When a room is entered, every object whose class name is **exactly the name of a script
   class** in the pack becomes a scripted object (the first 8). A script class named like
   an engine class (`Door`) takes that object over from the engine. An object whose class
   no script defines is left to the engine, or ignored.
3. The object's variables get the class's defaults; then every **custom property named
   like a `var(Category)` variable** sets that variable for this object; then the values
   kept for `save` variables (keyed by room and the object's Tiled id); then `spawn()`
   runs. The sprite is the one the class names, else the one named like the class.

**Planned additions**

| In Tiled | Effect |
|---|---|
| class = a script class | as today |
| property `kind = IronBat` | the class, figure, palette and numbers come from that record ([layered-sprites.md](layered-sprites.md) §5); the object's class in Tiled may then stay empty or be `Enemy` |
| property `attach = Patrol, DropsKey` | these `Attachment` classes are attached to the object when it is created, in this order, in its slots (an actor has 2) |
| property `Patrol.Speed = 2` | sets the variable `Speed` of the attachment `Patrol`; a plain `Speed = 2` still means the object's own class |
| property `palette`, `facing`, `state` | the starting palette, facing and state, without a variable for them |

- **A class can ask for attachments itself**: `defaultproperties { attach = Patrol; }`. The
  level's list is added to the class's. That is how a behaviour written once (patrolling,
  dropping an item, being frozen) is reused by many classes and switched on per object.
- **Created at run time** (`spawn(Class, x, y)`, `spawn_kind(record, x, y)`): the same
  steps without Tiled properties; the script that spawns sets variables through
  `spawned_set(#Speed, 2)` before the new object's `spawn()` runs.
- **Checked when cooking**, not when playing: a `kind` that is not a record, an `attach`
  that is not an `Attachment` class or more of them than the object has slots, a property
  that matches no editable variable of the class or of its attachments (today such a
  property is silently unused), a value of the wrong type. Each is reported with the room
  and the object's id.
- **Tiled knows the classes**: the cooker writes a Tiled custom-types file from the
  scripts (every class and attachment with its `var(Category)` variables, their types and
  defaults; every `kind`), so Tiled offers the classes in its class list and shows their
  properties with defaults to fill in. The editor's room view does the same in its object
  panel. Whether the installed Tiled version reads the file as written is to be checked
  when this is built.
- **Persistent ids** stay the Tiled object id, for the object and for its attachments
  (their `save` variables are keyed by the object and the attachment's slot).

## 9. State

A block `attachments`, pool of 24: owner, slot, class (by name hash, so it survives a
module being exchanged), state, wait, place, timer, 8 variables, lifetime. Flags `snapshot`.

Which attachments exist is not saved: after loading a save game they are created again from
the equipment and abilities in the inventory. Their `save` variables go into the existing
store keyed by owner slot and variable ([../spec/runtime-core.md](../spec/runtime-core.md)
§8). A save state (snapshot) restores them exactly, in the middle of a swing if need be.

Size: 24 x 48 bytes, about 1.2 KB of snapshot; nothing in the save game beyond the
inventory.

## 10. In the editor

- The Script tab shows the kind of each class and where it is used: by which records, in
  which rooms.
- A record's `attach` field is a picker of `Attachment` classes.
- The State tab lists the player's attachments with their state and variables while the
  game runs.
- Saving one script pushes one module; the Messages list says which class was swapped and
  how many instances kept their state.

## 11. Steps

1. One module per class: cooker, loader, name table, verifier unchanged per module. Test:
   the test level plays the same (same state checksums) with split modules.
2. Reload mode `module`. Test: a changed bat script is swapped while the door script is in
   the middle of a `sleep` and continues.
3. `Attachment` classes on the player: pool, slots, `tick`, `press`/`release`, game
   buttons, `attach`/`detach`. Test: an attachment that makes the player jump higher while
   attached.
4. Stats and capabilities read by the body step; filters; `owner_lock` and actions.
5. `spawn`, hit boxes, health, `hit`/`struck`, messages.
6. Attachments from the inventory (needs structures.md step 4); editor views.
7. The player as a script: the `Player` base class, `DefaultPlayer`, the project setting,
   `control`, `choose_move_anim`, hurt and death in the script. Test: equal checksums with
   the engine code it replaces. Then the attributes: the body step reads its numbers from
   the state instead of constants (same checksums again), reading and writing from scripts,
   modifiers, `knock_back`, `impulse`, `push`, `move_to`.
8. From the level: `attach` and dotted properties, attachments asked for by a class, the
   cook-time checks, the Tiled types file.

Done when: with the game running on the GBA, the script named by a weapon record is
replaced by another one and the next attack behaves differently, while no other script was
re-sent or restarted; giving the player the "double jump" ability record attaches one class
and the second jump works; a snapshot taken mid-swing continues the swing on the other
machine; the demo's player is a script whose hurt state was changed while the game ran; a wind
zone in the level pushes the player, a swamp slows it and heavy armour slows it further,
and each effect ends on its own;
and a bat placed in Tiled with `attach = Patrol` and `Patrol.Speed = 2` patrols at that
speed, while a misspelt property is reported by the cooker.

## 12. Open questions

- 8 variables per class is tight for a spell that tracks several projectiles. Raising it
  costs snapshot size for every instance; an alternative is to let spawned projectiles be
  their own small actors, which this plan assumes.
- Whether attachments on actors are needed at first (status effects on enemies) or only on
  the player. The design allows both; step 3 starts with the player.
