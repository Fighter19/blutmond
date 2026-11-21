# Plan: attachable scripts

Status: plan, nothing built. Milestone M14 in [../PLAN.md](../PLAN.md). Used by
[structures.md](structures.md) (a record names the script that makes it work) and
[ui.md](ui.md) (screens and effects are script classes without a room object).

## 1. What this is for

Today a script class belongs to one object placed in a room, all classes are compiled into
one module, and the player has no script at all
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
| `Attachment` | per (owner, slot) | the inventory, or `attach` |
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

## 7. State

A block `attachments`, pool of 24: owner, slot, class (by name hash, so it survives a
module being exchanged), state, wait, place, timer, 8 variables, lifetime. Flags `snapshot`.

Which attachments exist is not saved: after loading a save game they are created again from
the equipment and abilities in the inventory. Their `save` variables go into the existing
store keyed by owner slot and variable ([../spec/runtime-core.md](../spec/runtime-core.md)
§8). A save state (snapshot) restores them exactly, in the middle of a swing if need be.

Size: 24 x 48 bytes, about 1.2 KB of snapshot; nothing in the save game beyond the
inventory.

## 8. In the editor

- The Script tab shows the kind of each class and where it is used: by which records, in
  which rooms.
- A record's `attach` field is a picker of `Attachment` classes.
- The State tab lists the player's attachments with their state and variables while the
  game runs.
- Saving one script pushes one module; the Messages list says which class was swapped and
  how many instances kept their state.

## 9. Steps

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

Done when: with the game running on the GBA, the script named by a weapon record is
replaced by another one and the next attack behaves differently, while no other script was
re-sent or restarted; giving the player the "double jump" ability record attaches one class
and the second jump works; a snapshot taken mid-swing continues the swing on the other
machine.

## 10. Open questions

- 8 variables per class is tight for a spell that tracks several projectiles. Raising it
  costs snapshot size for every instance; an alternative is to let spawned projectiles be
  their own small actors, which this plan assumes.
- Whether attachments on actors are needed at first (status effects on enemies) or only on
  the player. The design allows both; step 3 starts with the player.
