class ArmourStats extends Attachment;

// What a piece of armour does while it is worn: the element it resists counts (first-items.md
// §7) until it is taken off. Its stats count by themselves (Armour is a stat_source). The
// default behaviour of every Armour record.

var record<Armour> Item;

event attached() { change(1); }
event detached() { change(-1); }

function change(int sign) {
  add_resist(Item.resists, sign);
}
