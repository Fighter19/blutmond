class AbilityPound extends Attachment;

// Ground Pound (first-items.md §5): Down and Jump in the air send the player straight down as
// fast as the room lets anything fall (6 pixels a frame asked; the test level's rooms allow 4);
// landing strikes both sides for 6. (Down and B in the air is the weapon's stab.)
// Attached while the player owns the Ground Pound record.

var record<Ability> Item;
var int Pounds;

event press(name button) {
  if button == #Jump && held(#Down) && !on_ground() && !owner_busy() { gotostate Pound; }
}

state Ready {
}

state Pound {
begin:
  Pounds += 1;
  owner_lock(90);
  while !on_ground() {
    owner_velocity(0.0, 6.0);
    sleep(1);
  }
  hit_box(-24, 3, 6, Element.None);
  owner_lock(8);
  gotostate Ready;
}

defaultproperties {
  start_state = Ready;
}
