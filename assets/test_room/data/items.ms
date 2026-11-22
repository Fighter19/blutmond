// What the player can find, carry, equip and use (docs/plans/first-items.md §2): the data
// behind weapons, spells, abilities, consumables and armour. Behaviour lives in the scripts
// their `behaviour` fields name; numbers live in the tables (*.bmdata) next to this file.

enum Element { None, Fire, Ice, Holy }
enum EquipSlot { Head, Body, Accessory }
// The player's stats (first-items.md §1), read with stat(Stat.Attack).
enum Stat { MaxHealth, MaxMana, Attack, Defence, Luck, AirJumps, Dash, WallSlide, GroundPound }

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
}

struct Weapon is Item {
  int       attack;
  int       reach;       // pixels in front of the player
  int       startup;     // frames before the hit
  int       recovery;    // frames after it
  Element   element;
  name      swing = #Slash;
  sound     sfx;
  attach    behaviour = WeaponSwing;
  StatBlock stats;
}

struct Spell is Item {
  int     cost;
  int     power;
  Element element;
  int     frames;
  name    cast = #Cast;
  sound   sfx;
  attach  behaviour;
}

struct Ability is Item {
  StatBlock stats;
  attach    behaviour;   // none: pure data
}

struct Consumable is Item {
  int    health;
  int    mana;
  name   cures;
  attach behaviour = UseRestore;
}

struct Armour is Item {
  EquipSlot slot;
  StatBlock stats;
  Element   resists;
  attach    behaviour;
}

struct KeyItem is Item {
}
