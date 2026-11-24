class Spark extends Actor;

// What Aimer shoots: flies along its angle at 2 pixels a frame and is gone after 40 frames.
// Cooked only into the test pack with wind.

event spawn() {
  fly_angle(angle, 2.0);
  lifetime(40);
}

defaultproperties {
  sprite = "spark";
}
