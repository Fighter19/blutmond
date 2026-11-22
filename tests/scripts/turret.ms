class Turret extends Actor;

// Something that shoots bolts (docs/plans/hitboxes.md §5); remembers being struck. Cooked only
// into the test pack with wind; the tests make it and its bolts.

var int Hits;

event spawn() {
  health = 99;
  set_knockback(0);
}

event hit(int amount) {
  Hits += 1;
  health = 99;
}
