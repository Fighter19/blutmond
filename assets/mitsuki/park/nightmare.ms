class Nightmare extends Actor;

// The nightmare in the park (park.ms: the story flag ParkTaken): it rises out of the ground
// behind the bench, hushing, comes for Hina, takes her (NightmareGrab: she is held at its
// hand, hina.ms), and leaps away with her up into the night (NightmareLeap), gone
// (NightmareGone).

const int TAKE_X = 159;     // where it stands when it takes her, beside her on the bench

var bool Came;

defaultproperties {
  sprite = "nightmare";
}

event spawn() {
  Came = false;
  hide();
}

event tick() {
  if !Came && flag(#ParkTaken) && !flag(#ParkDone) {
    Came = true;
    gotostate Taking;
  }
}

state Taking {
begin:
  facing = -1;
  show();
  play_anim(#rise);
  play_sound(#hush);
  sleep(24);
  play_anim(#hush);
  sleep(60);
  play_anim(#reach);
  while !move_toward(fix(TAKE_X), y, 1.2) { sleep(1); }
  set_flag(#NightmareGrab, true);
  play_anim(#hold);
  sleep(40);
  play_anim(#leap);
  set_flag(#NightmareLeap, true);
  fly(2.0, -2.5);
  sleep(70);
  hide();
  set_flag(#NightmareGone, true);
}
