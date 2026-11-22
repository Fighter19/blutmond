class ArmourStats extends Attachment;

// What a piece of armour does while it is worn: its stats are added to the player's and the
// element it resists counts (first-items.md §7), and both are taken away again when it is taken
// off. The default behaviour of every Armour record.

var record<Armour> Item;

event attached() { change(1); }
event detached() { change(-1); }

function change(int sign) {
  add_stat(Stat.MaxHealth, Item.stats.max_health * sign);
  add_stat(Stat.MaxMana, Item.stats.max_mana * sign);
  add_stat(Stat.Attack, Item.stats.attack * sign);
  add_stat(Stat.Defence, Item.stats.defence * sign);
  add_stat(Stat.Luck, Item.stats.luck * sign);
  add_resist(Item.resists, sign);
}
