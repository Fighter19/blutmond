class ArmourHeavy extends Attachment;

// Heavy armour (first-items.md §7, Chain Mail): its resistance as ArmourStats has it, and the
// player runs 6 % slower while it is worn.

var record<Armour> Item;

event attached() {
  change(1);
  add_speed(-6);
}

event detached() {
  change(-1);
  add_speed(6);
}

function change(int sign) {
  add_resist(Item.resists, sign);
}
