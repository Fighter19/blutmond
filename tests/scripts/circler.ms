class Circler extends Actor;

// A bat that flies round a circle (48 x 32 pixels) about where it was made, a whole turn every
// 128 frames: what the lock is tested with. Cooked only into the test pack with wind.

var int Phase;

event spawn() {
  health = 30;
  play_anim(#idle);
}

event tick() {
  Phase = Phase + 2;
  x = home_x + 48.0 * cos(Phase);
  y = home_y + 32.0 * sin(Phase);
}

defaultproperties {
  sprite = "bat";
}
