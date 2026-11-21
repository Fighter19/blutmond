class Bat extends Actor;

// Hangs where it was placed until the player comes near, then chases them. When the player
// gets away, or after it has hit them, it flies home and sleeps again.

var(Combat) int Damage = 1;
var(Flight) fix Speed = 0.75;
var(Flight) int WakeDistance = 56;
var(Flight) int GiveUpDistance = 120;
var int Tired;

state Sleeping {
  event tick() {
    if player_distance() < WakeDistance { gotostate Chasing; }
  }
begin:
  play_anim(#idle);
}

state Chasing {
  event tick() {
    face_player();
    move_toward_player(Speed);
    if player_distance() > GiveUpDistance { gotostate Returning; }
  }
  event touch() {
    hurt_player(Damage);
    gotostate Returning;
  }
begin:
  play_anim(#idle);
  // Give up after a while even if the player stays close.
  Tired = 0;
  loop {
    sleep(60);
    Tired += 1;
    if Tired >= 5 { gotostate Returning; }
  }
}

state Returning {
begin:
  loop {
    if move_toward(home_x, home_y, Speed) { gotostate Resting; }
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
