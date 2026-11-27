class Hina extends Actor;

// Hina in the park (park.ms directs the scene through story flags): hiding under the bench,
// only her eyes showing and blinking; out in a leap (ParkFound) into Mitsuki's arms (ParkHug,
// rocked; ParkPat: she sobs while Mitsuki pats her head); led by the hand behind Mitsuki
// (ParkLead); sitting close beside her on the bench (ParkSit; ParkLow: her head low); and
// snatched by the nightmare (ParkTaken: held up at its hand, NightmareGrab, reaching out for
// Mitsuki; carried off with it, NightmareLeap). After the scene (ParkDone) she is gone.

const int SIT_X = 139;      // beside Mitsuki on the bench (park.ms: SIT_X + 13)
const int TAKE_X = 159;     // where the nightmare stands when it takes her (nightmare.ms)
const int GRAB_DX = -18;    // its hand, from it (facing left)
const int GRAB_Y = 114;     // her feet, held up

var bool Taken;

defaultproperties {
  sprite = "hina";
}

event spawn() {
  Taken = false;
  facing = 1;
  play_anim(#hide);
  if flag(#ParkDone) { hide(); }
}

event tick() {
  if flag(#ParkDone) || Taken { return; }
  if flag(#ParkTaken) {
    Taken = true;
    gotostate Snatched;
  } else if flag(#ParkSit) {
    x = fix(SIT_X);
    facing = 1;
    if flag(#ParkLow) { play_anim(#sit_low); }
    else { play_anim(#sit); }
  } else if flag(#ParkLead) {
    draw_in_front(false);
    x = player_x() + 12.0;
    facing = -1;
    play_anim(#walk);
  } else if flag(#ParkHug) {
    // In her arms: drawn in front of her.
    x = player_x() - 4.0;
    facing = 1;
    draw_in_front(true);
    if flag(#ParkPat) { play_anim(#sob); }
    else { play_anim(#hug); }
  } else if flag(#ParkFound) {
    facing = 1;
    play_anim(#leap);
    var bool there = move_toward(player_x() - 4.0, y, 3.0);
  }
}

// Snatched: held up at the nightmare's hand, reaching out for Mitsuki; carried off with it.
state Snatched {
begin:
  wait_until(flag(#NightmareGrab));
  play_anim(#grabbed);
  facing = -1;
  x = fix(TAKE_X + GRAB_DX);
  y = fix(GRAB_Y);
  wait_until(flag(#NightmareLeap));
  fly(2.0, -2.5);
  sleep(70);
  hide();
}
