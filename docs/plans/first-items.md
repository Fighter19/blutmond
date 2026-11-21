# Plan: the first weapons, spells, abilities, consumables and armour

Status: plan, nothing built. Milestone M15 in [../PLAN.md](../PLAN.md).

## Dependencies

This plan is content; everything it needs comes from the others. Steps refer to section 11.

| Step | Needs |
|---|---|
| 1 (structures and tables) | [structures.md](structures.md) steps 1 and 2 |
| 2 (swords, dummy, bat with health) | [attachable-scripts.md](attachable-scripts.md) steps 3 and 5, [hitboxes.md](hitboxes.md) steps 1 and 2 |
| 3 (armour, Equipment menu, damage rule) | structures.md step 4, attachable-scripts.md steps 4 and 6, [ui.md](ui.md) steps 3 and 4 |
| 4 (consumables, Items page, quick slots) | ui.md step 4 |
| 5 (abilities) | attachable-scripts.md step 4 and the capabilities in the body step (its step 7, attributes) |
| 6 (spells, projectiles, remaining weapons) | hitboxes.md step 4. Casting from the hand at an angle needs [layered-sprites.md](layered-sprites.md) steps 4 and 6; without them spells fly straight ahead |
| 7 (status effects, new enemies, drops) | attachments on actors and messages (attachable-scripts.md steps 3 and 5); enemy variants as records need layered-sprites.md step 5 |

Nothing depends on this plan; it is the proof that the others work together.

The set is chosen so that every mechanism of those plans is used by at least one item, and
most items need no script of their own. All of it is content in the test project
(`assets/test_room` grows into `assets/demo`), not engine code; names, numbers and art are
our own.

## 1. Rules the numbers assume

**Stats** (16 possible, 9 used): `MaxHealth` 30, `MaxMana` 20, `Attack` 0, `Defence` 0,
`Luck` 0 as the player's base; capabilities `AirJumps`, `Dash`, `WallSlide`, `GroundPound`
start at 0.

**Damage.** `amount = max(1, attack - defence)`, where `attack` is the weapon's or spell's
value plus the attacker's `Attack`. Then the element: x 1.5 if the target is weak to it,
x 0.5 if it resists, in `fix`, rounded down, at least 1. Then the target's `damage_in`
filters. After a hit the target cannot be hit again for 20 frames (the player: 60, as the
hurt timer is today).

**Mana** refills by 1 every 90 frames. **Luck**: each point adds 2 % to the chance that a
defeated enemy drops its item.

**Elements**: `None`, `Fire`, `Ice`, `Holy`.

**Slots**: `weapon`, `spell`, `head`, `body`, `accessory1`, `accessory2`. Quick slots: L
uses quick slot 1, R is the Dash button. Buttons: A jump, B attack, Up + B spell, Select
quick slot 2, Start menu.

## 2. Structures

```
struct Item        { text title; text description; sprite icon; int price; int max_stack = 1; }
struct StatBlock   { short max_health; short max_mana; short attack; short defence; short luck;
                     byte air_jumps; byte dash; byte wall_slide; byte ground_pound; }

struct Weapon     is Item role item, equip(weapon), stat_source
                   { int attack; int reach; int startup; int recovery; Element element;
                     name swing; sound sfx; attach behaviour = WeaponSwing; StatBlock stats; }
struct Spell      is Item role item, equip(spell)
                   { int cost; int power; Element element; int frames; name cast = #Cast;
                     sound sfx; attach behaviour; }
struct Ability    is Item role item, stat_source
                   { StatBlock stats; attach behaviour; }        // behaviour may be empty
struct Consumable is Item role item, usable
                   { int health; int mana; name cures; attach behaviour = UseRestore; }
struct Armour     is Item role item, equip(head, body, accessory1, accessory2), stat_source
                   { byte slot; StatBlock stats; Element resists; attach behaviour; }
struct KeyItem    is Item role item { }
```

An ability is an item that cannot be dropped and is always "equipped": owning it attaches
its script and adds its stats.

## 3. Weapons

| Record | Attack | Reach | Startup | Recovery | Element | Script | Shows |
|---|---|---|---|---|---|---|---|
| Short Sword | 3 | 18 | 4 | 10 | | `WeaponSwing` | the plain case; the starting weapon |
| Dagger | 2 | 12 | 2 | 5 | | `WeaponSwing` | same script, other numbers |
| Great Axe | 7 | 22 | 12 | 18 | | `WeaponOverhead` | a second hit box above the player; cannot be used on a ladder |
| Chain Whip | 3 | 44 | 7 | 12 | | `WeaponWhip` | a hit box that grows over three frames |
| Throwing Knives | 2 | | 5 | 8 | | `WeaponThrow` | `spawn` of a projectile actor (`Knife`), at most 2 at a time |
| Flame Brand | 5 | 18 | 4 | 10 | Fire | `WeaponSwing` | a record copied from Short Sword; element; sends `#Ignite` to what it strikes; +2 `Attack` from its `stats` |

Four scripts and one projectile class for six weapons. `WeaponSwing` is the one in
attachable-scripts.md §3.

## 4. Spells

| Record | Cost | Power | Element | Script | What it does |
|---|---|---|---|---|---|
| Ember | 4 | 5 | Fire | `SpellBolt` | a projectile (`Bolt`) flying straight, 2.5 px/frame, gone after 60 frames or a wall |
| Frost Shard | 5 | 3 | Ice | `SpellBolt` | the same; sends `#Freeze` (90) to what it strikes |
| Ward | 8 | | | `SpellEffect` | attaches `WardEffect` for 300 frames: `damage_in` halves the amount |
| Mend | 10 | 12 | Holy | `SpellEffect` | attaches `MendEffect`: 1 health every 5 frames, 12 times |
| Blood Moon | 15 | 8 | Holy | `SpellNova` | a hit box of 96 x 64 around the player for 6 frames; the screen flashes through the UI's `blend` effect |

A spell that cannot be paid for plays a "no" sound and does nothing. `SpellBolt` and
`SpellEffect` take what they spawn or attach from the record, so new bolts and buffs are
data.

## 5. Abilities

| Record | Stats | Script | What it does |
|---|---|---|---|
| Double Jump | `air_jumps` 1 | none | pure data: the body step allows one jump in the air |
| Dash | `dash` 1 | `AbilityDash` | R: 12 frames at 3.5 px/frame, no gravity, 30 frames until the next; ends at a wall |
| Wall Jump | `wall_slide` 1 | `AbilityWallJump` | sliding at 1 px/frame against a wall while falling; jump pushes away at 2 px/frame |
| Ground Pound | `ground_pound` 1 | `AbilityPound` | Down + B in the air: falls at 6 px/frame, hit box on landing with 6 damage |
| Second Wind | | `AbilitySecondWind` | once per room, when health would reach 0 it becomes 1 (`damage_in` filter with a variable) |

Each is found as a scripted pickup in the test level and stays (`save`).

## 6. Consumables

| Record | Stack | Health | Mana | Cures | Script | |
|---|---|---|---|---|---|---|
| Potion | 9 | 15 | | | `UseRestore` | |
| Large Potion | 5 | 40 | | | `UseRestore` | |
| Mana Vial | 9 | | 15 | | `UseRestore` | |
| Antidote | 9 | | | `#Poison` | `UseRestore` | sends `#Cure` with the name; the effect detaches itself |
| Fire Flask | 5 | | | | `UseThrow` | spawns `Flask`: an arc, then a fire hit box for 30 frames |
| Return Feather | 1 | | | | `UseReturn` | brings the player to the last save point (the engine's respawn at the saved room) |

`UseRestore` is one script for four of them. Using an item from the menu closes the menu
for those that act in the world (flask, feather) and stays in it for the rest.

## 7. Armour

| Record | Slot | Stats | Script | |
|---|---|---|---|---|
| Leather Cap | head | defence 1 | | |
| Iron Helm | head | defence 3 | | |
| Traveller's Coat | body | defence 2 | | the starting armour |
| Chain Mail | body | defence 5 | `ArmourHeavy` | lowers the speed factor by 6 % while worn |
| Ember Cloak | body | defence 2, resists Fire | | resistance is data, read by the damage rule |
| Lucky Coin | accessory | luck 3 | | |
| Blood Ring | accessory | attack 2, max_health -5 | | a trade-off; the stat preview in the menu shows both |
| Thorn Band | accessory | | `ArmourThorns` | `hurt`: 2 damage back to the attacker, through `send_to` |

Five of eight need no script.

## 8. Status effects

Attachments in the free slots, with a lifetime:

| Class | Started by | Does |
|---|---|---|
| `PoisonEffect` | the message `#Poison` (the spider's bite) | 1 damage every 60 frames, 10 times; ends on `#Cure` |
| `BurnEffect` | `#Ignite` | on enemies: 1 damage every 20 frames for 100 frames |
| `FreezeEffect` | `#Freeze` | on enemies: no `tick` of the owner's own script while it lasts (the runtime skips a frozen owner), a blue palette |

## 9. What the test level gets

- A **training dummy** with endless health that shows the last damage as a number
  (uses the UI's `number` widget as a sprite).
- The **bat** gets 4 health, is weak to Holy and drops a Potion sometimes; a **spider**
  (6 health, poison bite) and a **fire wisp** (resists Fire, weak to Ice) are new.
- A **chest** class (`var(Loot) record<Item> Gives`), a third room with one chest per
  weapon and piece of armour, and four pedestals for the abilities, each behind the
  obstacle the previous one overcomes (a ledge for the double jump, a gap for the dash, a
  shaft for the wall jump, a cracked floor for the ground pound).
- Player animations: `Slash`, `Overhead`, `Whip`, `Throw`, `Cast`, `Dash`, `WallSlide`,
  `Pound`; an icon sheet (16 x 16, about 30 icons); sprites for knife, bolt, flask;
  about ten sound effects. All drawn or generated by the tools in `tools/` as the present
  test graphics are.

## 10. Budget check

| What | Count | Size |
|---|---|---|
| Records | 30 in 5 tables, plus key items | about 1.6 KB of `DTAB` chunks |
| Script classes | 4 weapon, 3 spell, 4 ability, 3 consumable, 2 armour, 3 effects, 3 projectiles, 3 enemies, chest, dummy: 27 | at about 300 bytes each, 8 KB of modules |
| Attachments at once | 6 equipment + 5 abilities + 4 effects = 15 | pool of 24 |
| Objects at once | room objects + 2 knives + 1 bolt + 1 flask | pool of 16 |
| Icons | 30 | 120 tiles, loaded as shown |

The demo pack stays under the 48 KB a multiboot GBA can take if the third room is small;
if not, the content buffers are the first thing to revisit (HARDWARE.md §1).

## 11. Steps

1. Structures and the six tables with all records; shown in the Data tab. No behaviour.
2. Short Sword and Dagger with `WeaponSwing`; dummy and bat with health. First combat test.
3. Armour and the Equipment menu: stats, preview, the damage rule with defence and
   elements.
4. Consumables and the Items page; quick slots.
5. Abilities with their capabilities in the body step; the third room.
6. Spells, projectiles, mana; the remaining weapons.
7. Status effects, spider, wisp, drops.

Each step ends with a recorded input sequence (the autopilot's or a mirror recording) whose
state checksums must be equal on the host and in mGBA.

Done when: every item of sections 3 to 7 can be found, equipped or used in the demo on GBA
and PC; changing a number in a table reaches the running game; the recorded play-through
that collects everything gives the same checksums on both machines.
