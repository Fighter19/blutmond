class WeaponSwing extends Attachment;

// The swing of a sword-like weapon (docs/plans/first-items.md §3): equipped weapons whose
// `behaviour` is WeaponSwing get it in their slot, with Item set to the weapon. The Attack
// button swings: the player keeps their speed but not the controls for the weapon's startup and
// recovery, plays its swing animation, and after the startup a rectangle `reach` pixels ahead
// strikes for 3 frames. Holding Up the swing goes above the head; holding Down it sweeps low on
// the ground, and in the air stabs below the feet (striking something bounces the player up).

var record<Weapon> Item;
var int Swings;
var int Strikes;
var name Aim;     // #Ahead, #Up, #Low or #Down: where this swing goes

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
  if button == #Attack && !owner_busy() {
    Aim = #Ahead;
    if held(#Up) { Aim = #Up; }
    if held(#Down) {
      if on_ground() { Aim = #Low; }
      if !on_ground() { Aim = #Down; }
    }
    gotostate Swing;
  }
}

event struck(int amount) {
  Strikes += 1;
  // A stab downwards that strikes: off the one struck, up again.
  if Aim == #Down { bounce(4.5); }
}

function swingAnim() {
  if Aim == #Up { owner_play_anim(#slash_up); return; }
  if Aim == #Down { owner_play_anim(#slash_down); return; }
  if Aim == #Low { owner_play_anim(#slash_low); return; }
  owner_play_anim(Item.swing);
}

state Ready {
}

state Swing {
begin:
  Swings += 1;
  owner_lock(Item.startup + Item.recovery);
  swingAnim();
  play_sound(Item.sfx);
  sleep(Item.startup);
  aim(Aim);
  hit_box(Item.reach, 3, Item.attack + stat(Stat.Attack), Item.element);
  sleep(Item.recovery);
  gotostate Ready;
}

defaultproperties {
  start_state = Ready;
}
