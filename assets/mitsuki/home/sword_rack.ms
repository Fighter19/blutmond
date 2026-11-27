class SwordRack extends Actor;

// A sword resting on a rack (Mitsuki's katana in her room, katana.bmsprite): walking under it
// takes it and equips it in the weapon slot (0), whatever was there (it goes back to the
// inventory). Taken stays taken (a `save` variable). As WeaponStand, with a picture: which
// weapon is the property Gives in Tiled.

var(Loot) record<Weapon> Gives = #Katana;
var save bool Taken;

defaultproperties {
  sprite = "katana";
}

event spawn() {
  if Taken { hide(); }
}

event touch() {
  if !Taken {
    Taken = true;
    give(Gives, 1);
    equip(0, Gives);
    play_sound(#pickup);
    hide();
  }
}
