// What kinds of enemies there are: one record per kind in enemies.bmdata
// (docs/plans/structures.md). The bat script reads its numbers from its kind.

struct Enemy {
  int    damage           = 1;      // taken from the player's health on a touch
  fix    speed            = 0.75;   // pixels per frame
  ushort wake_distance    = 56;     // pixels: comes after the player when they are this near
  ushort give_up_distance = 120;    // pixels: flies home when they are this far away
  ubyte  patience         = 5;      // seconds of chasing before it gives up anyway
}
