class Bolt extends Actor;

// A bolt (docs/plans/hitboxes.md §5): flies at 3 pixels a frame toward the player and strikes
// for 2. Reflected (a hit with #Reflect), it flies back for the other side. Cooked only into the
// test pack with wind.

event spawn() {
  if x < player_x() { fly(3.0, 0.0); }
  else { fly(-3.0, 0.0); }
  lifetime(200);
  hit_box(-4, 200, 2, Element.None);
}

event struck(int amount) { destroy(); }
event wall() { destroy(); }
