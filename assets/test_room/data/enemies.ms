// What kinds of enemies there are: one record per kind in enemies.bmdata
// (docs/plans/structures.md). The bat script reads its numbers from its kind.

struct Enemy {
  int    damage           = 1;      // taken from the player's health by its bite
  int    health           = 4;      // what it takes to defeat it
  fix    speed            = 0.75;   // pixels per frame
  ushort wake_distance    = 56;     // pixels: comes after the player when they are this near
  ushort give_up_distance = 120;    // pixels: flies home when they are this far away
  ubyte  patience         = 5;      // seconds of chasing before it gives up anyway
  record<Consumable> drop;          // what it may leave behind when defeated
  ubyte  drop_chance      = 0;      // in percent
  Element weak;                     // takes x 1.5 from this element (first-items.md §1)
  Element resists;                  // takes x 0.5 from this one
  sound  hit_sound;                 // played when the player strikes it
  // How a room object of this kind looks (layered-sprites.md §5): a room object with the
  // property `kind` and no class gets the class `behaviour`, the sprite `look` (none: the
  // class's) in its palette `palette`, and `extra` drawn over it at its point `extra_at`.
  attach behaviour;
  sprite look;
  name   palette          = #default;
  sprite extra;
  name   extra_at         = #body;
}
