class Heavy extends Attachment;

// A heavy blow (docs/plans/hitboxes.md §3): B strikes ahead with hit stop and a push of its own.
// Cooked only into the test pack with wind.

var int Strikes;
var int Guarded;

event press(name button) {
  if button == #Attack {
    hit_option(#Stop, 6);
    hit_knock(5.0, 0.0);
    hit_box(20, 3, 2, Element.None);
  }
}

event struck(int amount) { Strikes += 1; }
event guarded(int amount) { Guarded += 1; }
