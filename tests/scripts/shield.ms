class Shield extends Attachment;

// Filters (docs/plans/attachable-scripts.md §3): what strikes the player is halved, fire not at
// all; what the player's hits do is tripled. Dash sets the player's speed. Cooked only into the
// test pack with wind.

var int Filtered;     // damage_in calls
var int LastIn;       // the amount it got last

event damage_in(int amount) {
  Filtered += 1;
  LastIn = amount;
  if damage_is(Element.Fire) { set_damage(0); }
  else { set_damage(amount / 2); }
}

event damage_out(int amount) {
  set_damage(amount * 3);
}

event press(name button) {
  if button == #Dash { owner_velocity(3.5, -1.0); }
}
