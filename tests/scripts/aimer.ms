class Aimer extends Attachment;

// Aiming the player's torso (docs/plans/layered-sprites.md §6): when attached it plays the
// torso's direction set and turns it toward a point ahead and above, 8 steps a frame; the
// Attack button sends a Spark from the hand, flying along the hand's angle. Cooked only into
// the test pack with wind.

var int Shots;

event attached() {
  layer_play(#torso, #aim);
  layer_aim(#torso, 0);
  layer_turn(#torso, angle_to_point(x + 64.0, y - 80.0), 8);
}

event press(name button) {
  if button == #Attack {
    if spawn_at_point(#Spark, #torso, #hand) { Shots = Shots + 1; }
  }
}
