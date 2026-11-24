class Spark extends Actor;

// What Aimer and LockOn shoot: flies along its angle at 2 pixels a frame, strikes for 1 and is
// gone after 40 frames. Cooked only into the test pack with wind.

event spawn() {
  fly_angle(angle, 2.0);
  lifetime(40);
  hit_box(-4, 40, 1, Element.None);
}

event struck(int amount) { destroy(); }

defaultproperties {
  sprite = "spark";
}
