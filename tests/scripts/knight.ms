class Knight extends Actor;

// A figure with a shield (docs/plans/hitboxes.md §4): its guard box, ahead of it, stops hits
// that touch it unless they pierce. Cooked only into the test pack with wind; the tests make it.

var int Hits;
var int LastDamage;
var int Blocked;

event spawn() {
  health = 99;
  set_knockback(0);
  play_anim(#idle);
}

event hit(int amount) {
  Hits += 1;
  LastDamage = amount;
  health = 99;
}

event blocked(int amount) { Blocked += 1; }

defaultproperties {
  sprite = "knight";
}
