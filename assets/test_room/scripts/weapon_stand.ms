class WeaponStand extends Actor;

// A weapon on a stand: touching it takes it, and equips it in the weapon slot (0) if no weapon
// is equipped yet (else it waits in the inventory for the Equipment menu). Taken stays taken
// (a `save` variable). Which weapon is the property Gives in Tiled.

var(Loot) record<Weapon> Gives = #ShortSword;
var save bool Taken;

event spawn() {
  if Taken { hide(); }
}

event touch() {
  if !Taken {
    Taken = true;
    give(Gives, 1);
    if record_int(equipped(0), #attack) == 0 { equip(0, Gives); }
    hide();
  }
}
