class WeaponSwing extends Attachment;

// The swing of a sword-like weapon (docs/plans/first-items.md §3): equipped weapons whose
// `behaviour` is WeaponSwing get it in their slot, with Item set to the weapon. The Attack
// button swings: the player stands still for the weapon's startup and recovery, plays its swing
// animation, and after the startup a rectangle `reach` pixels ahead strikes for 3 frames.

var record<Weapon> Item;
var int Swings;
var int Strikes;

// The weapon's stats (Flame Brand: +2 attack) count while it is equipped.
event attached() {
  add_stat(Stat.Attack, Item.stats.attack);
  add_stat(Stat.Defence, Item.stats.defence);
}

event detached() {
  add_stat(Stat.Attack, -Item.stats.attack);
  add_stat(Stat.Defence, -Item.stats.defence);
}

event press(name button) {
  if button == #Attack && !owner_busy() { gotostate Swing; }
}

event struck(int amount) {
  Strikes += 1;
}

state Ready {
}

state Swing {
begin:
  Swings += 1;
  owner_lock(Item.startup + Item.recovery);
  owner_play_anim(Item.swing);
  play_sound(Item.sfx);
  sleep(Item.startup);
  hit_box(Item.reach, 3, Item.attack + stat(Stat.Attack), Item.element);
  sleep(Item.recovery);
  gotostate Ready;
}

defaultproperties {
  start_state = Ready;
}
