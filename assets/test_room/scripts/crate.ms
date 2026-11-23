class Crate extends Actor;

// A wooden crate: one hit breaks it (debris, the sound "crumble") and it leaves what it holds
// behind. Broken it stays broken: Broken is a `save` variable, kept for this object across
// rooms and in the save game. Set Holds on a crate placed in Tiled to give it something else.

var(Crate) record<Item> Holds = #Potion;
var save bool Broken;

event spawn() {
  set_knockback(0);   // it does not slide away
  health = 1;
  if Broken { hide(); }
}

event hit(int amount) {
  Broken = true;
  effect(#debris);
  play_sound(#crumble);
  hide();
  spawn_with(#Drop, Holds);
}

defaultproperties {
  sprite = "crate";
}
