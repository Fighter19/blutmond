class Knife extends Actor;

// A thrown knife (docs/plans/hitboxes.md §5): flies at 12 pixels a frame away from the player,
// for at most 60 frames; strikes the first figure it meets and is gone, as it is at a wall or a shield.
// Cooked only into the test pack with wind.

var int Walls;

event spawn() {
  if x < player_x() { fly(-12.0, 0.0); }
  else { fly(12.0, 0.0); }
  lifetime(60);
  hit_box(-4, 60, 2, Element.None);
}

event struck(int amount) { destroy(); }
event guarded(int amount) { destroy(); }

event wall() {
  Walls += 1;
  destroy();
}
