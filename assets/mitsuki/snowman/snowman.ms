class Snowman extends Actor;

// The snowman in the snowy park (Mitsuki's story, after Hina's call: the story flag Snowing). In
// the middle of his head, a Christmas ball: dark glass glowing (where Hina is). Up in front of
// him once: Mitsuki doubts anyone built him tonight; twice: she grabs the ball (SnowmanGrab,
// park.ms), he holds on, grows and fights:
//
// - snowballs rolling at her along the ground: jump over them;
// - a stomp: up, down, and ice shards knocked up from the ground beside him;
// - every fourth time, his frost breath: it goes out along the ground and over it, and only the
//   blocks of ice stop it: hide behind one.
// (These things are one class, Frost, by kind: frost.ms, things.bmdata.)
//
// Beaten, a shadow leaves him and gets away, and he is a happy snowman again (Defeated,
// kept in the save game; the story flag SnowmanDone).

const int Health = 30;
const int ROOM_W = 640;

var save bool Defeated;
var int Turn;
var int Shots;
var int Side;            // which way he faces: 1 right, -1 left

defaultproperties {
  sprite = "snowman";
  start_state = Waiting;
}

event spawn() {
  set_knockback(0);   // a boss does not budge
  weight = 10;
}

// His bar at the top right, as the moon bat's.
function hud() {
  ui_frame(0, 4, 16, 3);
  ui_label(1, 5, "Snowman");
  ui_gauge(8, 5, 7, health * 100 / Health);
  ui_show(true);
}

event hit(int amount) {
  play_sound(#hit_bat);
  if Defeated { return; }
  if health <= 0 { gotostate Melting; }
  else if health <= Health / 2 { music_intensity(3); }
}

// Not there before the snow; a happy snowman once beaten.
state Waiting {
begin:
  if Defeated {
    play_anim(#happy);
    gotostate Calm;
  }
  if !flag(#Snowing) {
    hide();
    wait_until(flag(#Snowing));
    show();
  }
  play_anim(#still);
  wait_until(flag(#SnowmanGrab));
  gotostate Waking;
}

// She grabbed the ball: he holds on, then grows.
state Waking {
begin:
  play_anim(#hold);
  wait_until(flag(#SnowmanHolds));
  sleep(30);
  play_sound(#crumble);
  play_anim(#grow);
  sleep(48);
  play_anim(#idle);
  health = Health;
  play_music(#frost_waltz);
  music_intensity(2);
  set_flag(#SnowmanFight, true);
  spawn_with(#Frost, #FrostBlockWest);
  spawn_with(#Frost, #FrostBlockEast);
  gotostate Idle;
}

function face() {
  face_player();
  Side = 1;
  if player_x() < x { Side = -1; }
}

state Idle {
  event tick() { hud(); }
begin:
  play_anim(#idle);
  face();
  sleep(40 - Turn % 3 * 6);
  Turn += 1;
  if Turn % 4 == 0 { gotostate Breath; }
  if Turn % 2 == 0 { gotostate Stomp; }
  gotostate Throw;
}

// Two snowballs, rolling at her.
state Throw {
  event tick() { hud(); }
begin:
  Shots = 0;
  while Shots < 2 {
    face();
    play_anim(#throw);
    sleep(16);
    spawn_with(#Frost, #FrostBall);
    play_anim(#idle);
    Shots += 1;
    sleep(44);
  }
  gotostate Idle;
}

// Up, down, and the ground breaks into ice shards beside him.
state Stomp {
  event tick() { hud(); }
begin:
  play_anim(#jump);
  sleep(22);
  play_anim(#land);
  play_sound(#thud);
  spawn_with(#Frost, #FrostShardLeft);
  spawn_with(#Frost, #FrostShardRight);
  sleep(30);
  gotostate Idle;
}

// His frost breath: a moment to take cover, then it goes out along the ground.
state Breath {
  event tick() { hud(); }
begin:
  face();
  play_anim(#breath);
  play_sound(#hush);
  sleep(40);
  spawn_with(#Frost, #FrostBreathLow);
  spawn_with(#Frost, #FrostBreathHigh);
  sleep(70);
  gotostate Idle;
}

// Beaten: a shadow leaves him; he is a snowman again.
state Melting {
begin:
  Defeated = true;
  player_lock(600);
  ui_clear(0, 4, 16, 3);
  fade_music(0, 120);
  Shots = 0;
  while Shots < 6 {
    hide();
    sleep(4);
    show();
    sleep(4);
    Shots += 1;
  }
  spawn_with(#Frost, #FrostShade);
  play_anim(#happy);
  sleep(90);
  set_flag(#SnowmanDone, true);
  set_flag(#SnowmanFight, false);
  say_line(#MitsukiShadowGone);
  wait_until(dialogue_done());
  say_line(#MitsukiHappySnowman);
  wait_until(dialogue_done());
  play_music(#park_dread);
  fade_music(100, 60);
  music_intensity(1);
  player_lock(0);
  gotostate Calm;
}

state Calm {
}
