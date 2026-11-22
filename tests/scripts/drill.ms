class Drill extends Attachment;

// A drill (docs/plans/hitboxes.md §3): Select makes a rectangle for 20 frames that pierces
// guard boxes and strikes the same figure every 4 frames. Cooked only into the test pack with
// wind.

var int Strikes;

event press(name button) {
  if button == #Use {
    hit_option(#Multi, 4);
    hit_option(#Pierce, 0);
    hit_box(20, 20, 1, Element.None);
  }
}

event struck(int amount) { Strikes += 1; }
