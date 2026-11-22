class Jab extends Attachment;

// A strike without a weapon or a drawing, for trying hits out (docs/plans/hitboxes.md): the
// Attack button makes a rectangle `Reach` pixels ahead of the player for 3 frames. Cooked only
// into the test pack with wind (tests/test_game.c attaches it), not into the test level.

var int Reach = 20;
var int Damage = 2;
var int Strikes;      // figures it struck
var int LastAmount;
var int Taken;        // health the player lost while it was attached

event press(name button) {
  if button == #Attack { hit_box(Reach, 3, Damage); }
}

event struck(int amount) {
  Strikes += 1;
  LastAmount = amount;
}

event hit(int amount) {
  Taken += amount;
}
