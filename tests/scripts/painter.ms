class Painter extends Actor;

// set_palette and flash (docs/plans/layered-sprites.md §2): drawn as the bat, in its red
// palette, flashing for 5 frames when made; a palette the bat does not have leaves it as it is.
// Cooked only into the test pack with wind.

event spawn() {
  set_palette(#red);
  flash(5);
}

event hit(int amount) { set_palette(#nothing_like_this); }

defaultproperties {
  sprite = "bat";
}
