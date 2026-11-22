# Plan: the first weapons, spells, abilities, consumables and armour

Status: step 1 built: the structures of section 2 and every record of sections 3 to 7 are in
the test project (`assets/test_room/data`: `items.ms` and one `.bmdata` file per table), cooked
into the pack. Step 2 built: `WeaponSwing` (`assets/test_room/scripts/weapon_swing.ms`) for the
Short Sword and the Dagger, each on a `WeaponStand` (the sword near the start, the dagger in
the second room) that gives and equips it; a `slash` animation for the player (32 wide); the
training dummy (no drawing and no number yet); the bat with 4 health from its record, struck
back home and defeated after two strikes of the sword. The autopilot fights the bat, so the
recorded play-through of the tests (host and mGBA, the same picture and state) now contains the
fight. Equipping is `equip(slot, record)` from a script, not yet through roles and a menu; the
weapon slot is 0. Steps 3 and 4 built in part: equipment through the Equipment menu (a script,
ui.md), armour's stats while worn (`ArmourStats`, the default behaviour of armour; the
weapon's own stats through `WeaponSwing`), the damage rule with defence (`max(1, damage -
Defence)`; elements not yet), the most health from MaxHealth, potions used in the Items page.
And a first boss: Vesper, the moon bat, in a third room (the boss hall), with an intro where it
speaks (`say`), a fight with dives (an armed bite) and fireballs (objects made at run time
that arm their own hit box), a health bar, and the Iron Helm it drops; defeated it stays so.
The bat drops a potion half of the time. The autopilot plays all of it, and the picture at
frame 4100 is the same on the PC and the GBA. Steps 3 and 4 since then: the element rule
(weak x 1.5, resists x 0.5; the bat's kind is weak to Holy, enemies name `weak` and `resists`,
armour's `resists` counts while worn: the Ember Cloak halves fire); `ArmourHeavy` (Chain Mail:
6 % slower) and `ArmourThorns` (the Thorn Band strikes back for 2 through `strike_back` from
its `hit()`); mana (20 plus MaxMana, 1 back every 90 frames); consumables used through their
behaviour (`use_item` attaches it for a frame in slot 7): `UseRestore` for potions and the
mana vial (nothing taken when nothing would be restored), `UseReturn` for the Return Feather
(to the last save point, or the start); the quick slots (L and Select), set in the Items page
with L and R; luck adds 2 % per point to the bat's drop. Not yet as the plan has it: the roles
(`role item, equip(...)`, they come with structures.md step 4), the Data tab (structures.md
step 5), icons and sounds, the stat preview of the Blood Ring's trade-off, a "no" sound;
the Antidote's cure and the Fire Flask wait for steps 6 and 7 (effects, projectiles); the new
items are not placed in the level yet (step 5, the room with the chests). Weapons strike with
their element (`hit_box` takes one: the Flame Brand burns, its `#Ignite` waits for step 7). Armour names its slot with an enum
`EquipSlot` instead of a byte. Steps 5 to 7 are open. Milestone M15 in [../PLAN.md](../PLAN.md).

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
| Lance | 4 | 30 | 6 | 12 | | `WeaponSwing` | `style = Thrust`: up and down straight thrusts (`thrust_up`, `thrust_down`), ahead `thrust` |

Four scripts and one projectile class for six weapons. `WeaponSwing` is the one in
attachable-scripts.md §3. It aims by the d-pad held when Attack is pressed: Up strikes above
the head, Down sweeps low on the ground (`slash_low`) and strikes below the feet in the air
(what it strikes bounces the player up at 4.5 px/frame), else ahead. How up and down strike is
the weapon's `style`: `Slash` (swords) cuts in an arc, `slash_up` from ahead over the head to
behind and `slash_down` from ahead under the feet to behind, with the hit boxes `blade` drawn
on those frames (`arm(#blade)`); `Thrust` (the Lance) thrusts straight, a rectangle above or
below (`aim`, `hit_box`) with `thrust_up` and `thrust_down`.
While it swings the player cannot walk but keeps their speed, which fades on the ground, and
steers in the air; a weapon's `ground_move` (Slide, Stop, Keep, Walk) and `air_move` (Steer,
Keep, Drift) choose otherwise: the Dagger walks, the Great Axe stops and drifts.

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

## 8b. Commands

Special moves (runtime-core.md §13): records of the structure `Command` (data/items.ms), each
an input (`#Down_DownFwd_Fwd_Attack`), a mana cost and a behaviour, belonging to a weapon or to
a charm (an accessory) that gives it whatever the weapon. The Start menu's Commands page lists
the ones the player has now with their keys.

Every weapon has two at least. A weapon's moves are one script, `CommandStrike`, with the
numbers in the record: `startup`, `hits` strikes `every` frames apart, `reach` (negative: both
sides), `power` (percent of the weapon's attack), `dash` and `hop` at the start, `element`,
`anim`.

| Command | Of | Input | MP | What |
|---|---|---|---|---|
| Lunge | Short Sword (Flame Brand: of fire) | Down, Down-Fwd, Fwd + B | 4 | a dash forward, 200 % |
| Rising Slash | Short Sword (Flame Brand: of fire) | Fwd, Down, Down-Fwd + B | 5 | a hop up, two cuts of 150 % |
| Flame Burst | Flame Brand | Down, Down + B | 6 | fire 36 px around, three times, 120 % |
| Flurry | Dagger | Fwd, Fwd + B | 2 | three stabs, 8 frames apart |
| Slip Stab | Dagger | Back, Fwd + B | 3 | a hop back, a 40 px stab of 150 % |
| Ground Breaker | Great Axe | Down, Down + B | 6 | after 14 frames one blow on both sides, 250 % |
| Whirlwind | Great Axe | Back, Down-Back, Down, Down-Fwd, Fwd + B | 8 | four strikes around |
| Long Lash | Chain Whip | Back, Fwd + B | 4 | 72 px, 150 % |
| Twirl | Chain Whip | Down, Up + B | 5 | three strikes 40 px around, 80 % |
| Volley | Throwing Knives | Down, Down-Fwd, Fwd + B | 4 | three strikes 96 px ahead |
| Leaping Throw | Throwing Knives | Down, Up + B | 5 | a leap, two strikes 96 px ahead |
| Blood Absorb | Crimson Charm | Up, Down + R | 6 | four pulses, 20 frames apart, strike 2 within 40 px on both sides; what is struck heals as much |
| Gravity Flip | Moon Charm | Down, Up + A | 3 | the player walks on ceilings, or back |

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

A multiboot GBA takes packs up to about 140 KB (one content pool, runtime-abi.md §0).

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
