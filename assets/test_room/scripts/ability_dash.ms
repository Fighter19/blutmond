class AbilityDash extends Attachment;

// Dash (first-items.md §5): the Dash button (L) sends the player ahead at 3.5 pixels a frame
// for 12 frames without falling; then 30 frames until the next. A wall ends it (the body step
// stops the player there). Attached while the player owns the Dash record.

var record<Ability> Item;
var int Left;    // frames of the dash still to go
var int Rest;    // frames until the next one
var int Dashes;

event press(name button) {
  if button == #Dash && Left == 0 && Rest == 0 && !owner_busy() {
    Left = 12;
    Rest = 42;
    Dashes += 1;
    owner_lock(12);
    lock_move(GroundMove.Keep, AirMove.Keep);   // no friction, no air drag: the speed it sets
  }
}

event tick() {
  if Rest > 0 { Rest -= 1; }
  if Left > 0 {
    Left -= 1;
    owner_velocity(fix(facing) * 3.5, 0.0);
  }
}
