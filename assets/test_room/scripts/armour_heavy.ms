class ArmourHeavy extends Attachment;

// Heavy armour (first-items.md §7, Chain Mail): its stats as ArmourStats has them, and the
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
  add_stat(Stat.MaxHealth, Item.stats.max_health * sign);
  add_stat(Stat.MaxMana, Item.stats.max_mana * sign);
  add_stat(Stat.Attack, Item.stats.attack * sign);
  add_stat(Stat.Defence, Item.stats.defence * sign);
  add_stat(Stat.Luck, Item.stats.luck * sign);
  add_resist(Item.resists, sign);
}
