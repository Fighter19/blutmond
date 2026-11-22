class ArmourThorns extends Attachment;

// The Thorn Band (first-items.md §7): its stats as ArmourStats has them, and whatever strikes
// the player is struck back for 2 (a hazard is not).

var record<Armour> Item;
var int Returned;

event attached() { change(1); }
event detached() { change(-1); }

event hit(int amount) {
  if strike_back(2) { Returned += 1; }
}

function change(int sign) {
  add_stat(Stat.MaxHealth, Item.stats.max_health * sign);
  add_stat(Stat.MaxMana, Item.stats.max_mana * sign);
  add_stat(Stat.Attack, Item.stats.attack * sign);
  add_stat(Stat.Defence, Item.stats.defence * sign);
  add_stat(Stat.Luck, Item.stats.luck * sign);
  add_resist(Item.resists, sign);
}
