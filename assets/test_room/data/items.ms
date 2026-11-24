// What the player can find, carry, equip and use (docs/plans/first-items.md §2): the data
// behind weapons, spells, abilities, consumables and armour. Behaviour lives in the scripts
// their `behaviour` fields name; numbers live in the tables (*.bmdata) next to this file.
// The roles after ':' (docs/spec/data-format.md §6) tell the engine what a structure's records
// are: items to carry, equipment for a slot of enum Slot, whose stats count while it is worn.

enum Element { None, Fire, Ice, Holy }
// The equipment slots (equip(slot, record), the role equip).
enum Slot { Weapon, Spell, Head, Body, Accessory }
// How the player moves while an attack holds them (lock_move, src/world/game.h), on the ground:
// no walking, the speed fades; stopped at once; the speed kept; walking. In the air: steering;
// the speed kept; no steering, slowed by the air.
enum GroundMove { Slide, Stop, Keep, Walk }
// How a WeaponSwing weapon strikes up and (in the air) down: in an arc (a sword: over the head
// from ahead to behind, under the feet) or straight (a lance: a thrust above, a stab below).
enum WeaponStyle { Slash, Thrust }
enum AirMove { Steer, Keep, Drift }
// The player's stats (first-items.md §1), read with stat(Stat.Attack).
enum Stat { MaxHealth, MaxMana, Attack, Defence, Luck, AirJumps, Dash, WallSlide, GroundPound, Grapple }

struct Item {
  text   title;
  text   description;
  sprite icon;
  int    price;
  int    max_stack = 1;
}

// What equipping something or owning an ability adds to the player's stats.
struct StatBlock {
  short max_health;
  short max_mana;
  short attack;
  short defence;
  short luck;
  byte  air_jumps;
  byte  dash;
  byte  wall_slide;
  byte  ground_pound;
  byte  grapple;
}

struct Weapon is Item : item, equip(Weapon), stat_source(stats) {
  int       attack;
  int       reach;       // pixels in front of the player
  int       startup;     // frames before the hit
  int       recovery;    // frames after it
  Element   element;
  name      swing = #Slash;
  sound     sfx;
  attach    behaviour = WeaponSwing;
  StatBlock stats;
  GroundMove ground_move;   // while it swings (and its commands strike)
  AirMove    air_move;
  WeaponStyle style;
}

struct Spell is Item : item, equip(Spell) {
  int     cost;
  int     power;
  Element element;
  int     frames;
  name    cast = #Cast;
  sound   sfx;
  attach  behaviour;
}

// Abilities act while owned: their stats count, their behaviour is attached in an ability slot.
struct Ability is Item : item, ability, stat_source(stats) {
  StatBlock stats;
  attach    behaviour;   // none: pure data
}

struct Consumable is Item : item, usable {
  int    health;
  int    mana;
  name   cures;
  attach behaviour = UseRestore;
}

struct Armour is Item : item, equip(slot), stat_source(stats) {
  Slot      slot;
  StatBlock stats;
  Element   resists;
  attach    behaviour = ArmourStats;   // its stats while worn
}

struct KeyItem is Item : item {
}

// A special move (src/world/commands.h): directions, then a button. It belongs to a weapon, or
// to a charm that gives it whatever the weapon. Its behaviour is attached for `frames`. A
// weapon's moves are mostly CommandStrike, which the numbers below describe: after `startup`
// frames `hits` strikes, `every` frames apart, `reach` pixels ahead (negative: on both sides),
// each `power` percent of the weapon's attack; at the start a push of `dash` forward
// (negative: back) and `hop` up.
struct Command {
  text           title;
  text           keys;          // the input as the Commands page shows it
  text           description;
  name           input;         // Down_DownFwd_Fwd_Attack
  int            cost;          // mana
  record<Weapon> weapon;
  record<Armour> charm;
  attach         behaviour = CommandStrike;
  short          frames = 30;
  short          power = 100;
  short          reach = 20;
  byte           hits = 1;
  byte           every = 8;
  byte           startup;
  Element        element;
  fix            dash;
  fix            hop;
  name           anim = #slash;
}
