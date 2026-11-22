class HighJump extends Attachment;

// An ability as an attachment (docs/plans/attachable-scripts.md): while it is attached to the
// player, every jump gets a push upward right after it starts, so it goes higher. Nothing in
// the test level attaches it; a script does with attach(slot, #HighJump).

var fix Boost = 1.0;
var int Jumps;     // jumps it boosted
var int Presses;   // times the jump button was pressed

event press(name button) {
  if button == #Jump { Presses += 1; }
}

event jumped() {
  impulse(0, -Boost);
  Jumps += 1;
}
