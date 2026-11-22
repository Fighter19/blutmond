class ArmourThorns extends Attachment;

// The Thorn Band (first-items.md §7): its resistance as ArmourStats has it, and whatever
// strikes the player is struck back for 2 (a hazard is not).

var record<Armour> Item;
var int Returned;

event attached() { change(1); }
event detached() { change(-1); }

event hit(int amount) {
  if strike_back(2) { Returned += 1; }
}

function change(int sign) {
  add_resist(Item.resists, sign);
}
