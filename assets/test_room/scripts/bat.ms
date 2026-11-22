class Bat extends Actor;

// Hangs where it was placed until the player comes near, then chases them. When the player
// gets away, or after it has bitten them, it flies home and sleeps again.

// Its numbers come from its kind, a record of data/enemies.bmdata; a bat placed in Tiled can
// be given another kind with the property Kind.
var(Combat) record<Enemy> Kind = #Bat;
var int Tired;

state Sleeping {
  event tick() {
    if player_distance() < Kind.wake_distance { gotostate Chasing; }
  }
begin:
  play_anim(#idle);
}

state Chasing {
  event tick() {
    face_player();
    move_toward_player(Kind.speed);
    if player_distance() > Kind.give_up_distance { gotostate Returning; }
  }
  // Its bite (the hit box `bite` of its sprite) struck the player.
  event struck(int amount) {
    gotostate Returning;
  }
begin:
  play_anim(#idle);
  arm(#bite, Kind.damage, Element.None);
  // Give up after a while even if the player stays close.
  Tired = 0;
  loop {
    sleep(60);
    Tired += 1;
    if Tired >= Kind.patience { gotostate Returning; }
  }
}

state Returning {
begin:
  disarm(#bite);
  loop {
    if move_toward(home_x, home_y, Kind.speed) { gotostate Resting; }
    sleep(1);
  }
}

state Resting {
begin:
  sleep(90);
  gotostate Sleeping;
}

defaultproperties {
  sprite = "bat";
  start_state = Sleeping;
}
