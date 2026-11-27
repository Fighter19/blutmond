class HomeProp extends Actor;

// Things of Mitsuki's home (mitsuki_props.bmsprite, props.py), by the property Kind:
//
// - #Katana: her katana on its rack (her room): walking under it takes it and equips it in the
//   weapon slot (0), whatever was there (back to the inventory). Taken stays taken (`save`).
//   Which weapon is the property Gives.
// - #Switch: the light switch by her bed: it shows whether her light is on, the story flag
//   MitsukiLight (her prologue flips it, and Up in front of it: prologue.ms).
// - #Window: the cottage's window in the rural street, which is hers: drawn lit in the street's
//   tiles, this dark glass over it while her light is off.
// - #Monster: the thing peeking in at her window: it stares, blinks while anyone talks (the
//   dialogue box is open), and ducks away once she gets up (the story flag MonsterGone,
//   prologue.ms); it is back when the flag is off again.

var(Prop) name Kind = #Katana;
var(Loot) record<Weapon> Gives = #Katana;
var save bool Taken;
var bool Here;   // the thing at the window is there (not ducked away)

defaultproperties {
  sprite = "mitsuki_props";
}

event spawn() {
  if Kind == #Katana {
    play_anim(#katana);
    if Taken { hide(); }
  }
  if Kind == #Window {
    play_anim(#window);
    if flag(#MitsukiLight) { hide(); }
  }
  if Kind == #Monster {
    Here = !flag(#MonsterGone);
    play_anim(#monster);
    if !Here { hide(); }
  }
}

event tick() {
  if Kind == #Switch {
    if flag(#MitsukiLight) { play_anim(#switch_on); }
    else { play_anim(#switch_off); }
  }
  if Kind == #Monster {
    if flag(#MonsterGone) {
      if Here {
        Here = false;
        gotostate Ducking;
      }
    } else {
      if !Here {
        Here = true;
        show();
      }
      if dialogue_done() { play_anim(#monster); }
      else { play_anim(#monster_blink); }
    }
  }
}

// The thing at the window: eyes shut, down below the sill, gone.
state Ducking {
begin:
  play_anim(#monster_hush);
  finish_anim();
  hide();
}

event touch() {
  if Kind == #Katana && !Taken {
    Taken = true;
    give(Gives, 1);
    equip(0, Gives);
    play_sound(#pickup);
    hide();
  }
}
