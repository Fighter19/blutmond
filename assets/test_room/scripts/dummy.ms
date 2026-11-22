class Dummy extends Actor;

// A training dummy (docs/plans/first-items.md §9): it can be struck without end and remembers
// how often and for how much. It has no drawing yet: placed as a point it shows the
// placeholder frame.

var int Hits;
var int LastDamage;

event spawn() {
  health = 999;
  set_knockback(0);   // it stands where it was put
}

event hit(int amount) {
  play_sound(#hit_dummy);
  Hits += 1;
  LastDamage = amount;
  health = 999;
}
