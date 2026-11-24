class LockOn extends Attachment;

// The lock (docs/plans/layered-sprites.md §6): Select locks on to the nearest enemy in front
// within 96 pixels (again: lets go). While locked the player faces it, the torso plays its
// aiming directions and the engine turns it toward the target 6 steps a frame, and a marker
// sits on the target; B sends a Spark from the hand. The lock ends when the target is gone or
// more than 128 pixels away. Cooked only into the test pack with wind.

var Actor? Target;
var bool Locked;
var int Shots;

event press(name button) {
  if button == #Use {
    if Locked { Target = null; }
    else { Target = nearest_enemy(96, #InFront); }
    refresh();
  }
  if button == #Attack && Locked {
    if spawn_at_point(#Spark, #torso, #hand) { Shots = Shots + 1; }
  }
}

event tick() {
  if t := Target {
    if distance_to(t) > 128 { Target = null; refresh(); }
    else { owner_face(t); }
  } else {
    if Locked { refresh(); }
  }
}

function refresh() {
  if t := Target {
    owner_face(t);
    layer_play(#torso, #aim);
    layer_aim(#torso, angle_to(t));
    layer_track(#torso, t, 6);
    marker_show(t, #spark);
    Locked = true;
  } else {
    layer_play(#torso, #follow);   // a name the torso has no animation of: back to the legs
    marker_hide();
    Locked = false;
  }
}
