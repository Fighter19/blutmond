class Thrower extends Attachment;

// Throwing knives (docs/plans/hitboxes.md §5): R throws one ahead, two in the air at most. A
// knife's hits are the player's: this attachment hears struck(). Cooked only into the test pack
// with wind.

var int Thrown;
var int Strikes;

event press(name button) {
  if button == #Spell && alive(#Knife) < 2 {
    if spawn(#Knife, x + fix(facing) * 10.0, y - 6.0) { Thrown += 1; }
  }
}

event struck(int amount) { Strikes += 1; }
