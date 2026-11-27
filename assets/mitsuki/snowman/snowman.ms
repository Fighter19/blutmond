class Snowman extends Actor;

// The snowman in the snowy park (Mitsuki's story, after Hina's call: the story flag Snowing). In
// his forehead, a Christmas ball: red glass glowing (where Hina is). Up in front of him once:
// Mitsuki doubts anyone built him tonight; twice: she grabs the ball (SnowmanGrab, park.ms), he
// holds on, grows and fights, in turns:
//
// - a stomp: he jumps at her (where she stands as he jumps off; he strikes in the air) and lands;
//   a ripple goes through the ground to where she stood, and there a pillar of ice breaks out
//   (the ripple and the pillar breaking out strike; standing, it is cover);
// - snowballs rolling at her along the ground: jump over them;
// - his frost breath: a beam along the ground, growing out from his breath; a pillar stops it:
//   stand behind one;
// - from half his health on, icicles instead of the snowballs: fast, at her knees, always in the
//   same rhythm (two close, a pause, two close): jump over them.
// (These things are one class, Frost, by kind: frost.ms, things.bmdata.)
//
// The pillars are the snowman's to reckon with: he notes each one's place and the frame it is
// out (the ripple's way at its speed: PillarA, PillarB, place + 1024 x frame; two at most: one
// stomp is more than a pillar's time after the one before the last), and stops the beam at the
// nearest that stands in front of him (for PILLAR_STANDS frames: then it crumbles, frost.ms).
//
// Beaten, a shadow leaves him and gets away, and he is a happy snowman again (Defeated,
// kept in the save game; the story flag SnowmanDone).

const int Health = 30;
const int ROOM_W = 640;
const int NEAR_MIN = 40;      // a pillar stands at least this far from him (frost.ms the same)
const int ARENA_L = 40;
const int ARENA_R = 600;
const int PILLAR_STANDS = 296;  // frames a pillar is counted on once out (frost.ms STANDS 300; out a
                                // frame or two before he reckons)
const int RIPPLE_SPEED10 = 28;  // the ripple's speed x 10 (FrostRipple: 2.8)
const int PILLAR_HALF = 6;    // half a pillar's width
const int BEAM_DX = 20;       // where the beam starts (FrostBeam*)
const int BEAM = 32;          // a piece of the beam
const int GROWN = 155;        // his size grown, percent (snowman_sheet.py SCALE)
const int JUMP = 42;          // frames in the air
const fix JUMP_UP = 4.1;      // his speed up as he jumps off; down by GRAVITY each frame
const fix GRAVITY = 0.2;

var save bool Defeated;
var int Turn;
var int Shots;
var int Clock;               // frames since the fight began
var int PillarA;             // the newest pillar: its place + 1024 x the frame it is out (0: none)
var int PillarB;             // the one before
var fix Vx;                  // in a jump
var fix Vy;

defaultproperties {
  sprite = "snowman";
  start_state = Waiting;
}

event spawn() {
  set_knockback(0);   // a boss does not budge
  weight = 10;
}

// His bar at the top right, as the moon bat's; the fight's clock.
function fighting() {
  ui_frame(0, 4, 16, 3);
  ui_label(1, 5, "Snowman");
  ui_gauge(8, 5, 7, health * 100 / Health);
  ui_show(true);
  set_scale(GROWN);   // (again each frame: a state loaded does not keep it)
  Clock += 1;
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
  // He grows (drawn larger: set_scale; his grown frames' boxes are as large in the sheet).
  Shots = 0;
  while Shots < 24 {
    set_scale(100 + Shots * (GROWN - 100) / 24 + (Shots % 4) * 3);   // (a wobble in it)
    sleep(2);
    Shots += 1;
  }
  set_scale(GROWN);
  play_anim(#idle);
  health = Health;
  play_music(#frost_waltz);
  music_intensity(2);
  set_flag(#SnowmanFight, true);
  gotostate Idle;
}

function face() {
  face_player();
}

// Where the pillar of a stomp stands: where Mitsuki is, not too near him, in the park.
// (frost.ms has the same: the ripple goes there.)
function int pillar_at() {
  var int px = int(player_x());
  var int me = int(x);
  var int side = 1;
  if px < me { side = -1; }
  if (px - me) * side < NEAR_MIN { px = me + NEAR_MIN * side; }
  if px < ARENA_L { px = ARENA_L; }
  if px > ARENA_R { px = ARENA_R; }
  return px;
}

// A pillar breaking out at `at`, once the ripple has got there from him.
function note_pillar(int at) {
  var int way = at - int(x);
  if way < 0 { way = -way; }
  PillarB = PillarA;
  PillarA = at + 1024 * (Clock + way * 10 / RIPPLE_SPEED10 + 2);
}

// Which way the beam goes: toward her.
function int beam_side() {
  if player_x() < x { return -1; }
  return 1;
}

// How far the beam gets from its start: to the pillar `p` if it stands in the way and nearer
// than `room`.
function int blocked(int room, int p) {
  if p == 0 { return room; }
  var int side = beam_side();
  var int start = int(x) + BEAM_DX * side;
  var int age = Clock - p / 1024;
  if age < 0 || age >= PILLAR_STANDS { return room; }
  var int d = (p % 1024 - start) * side - PILLAR_HALF;
  if d >= 0 && d < room { return d; }
  return room;
}

// His frost beam toward her: its pieces, up to a pillar or the park's end; a longer one in two.
function breathe() {
  var int side = beam_side();
  var int start = int(x) + BEAM_DX * side;
  var int room = ROOM_W - start;
  if side < 0 { room = start; }
  room = blocked(room, PillarA);
  room = blocked(room, PillarB);
  var int n = room / BEAM;
  if n > 8 { n = 8; }
  if n == 1 { spawn_with(#Frost, #FrostBeam1); }
  else if n == 2 { spawn_with(#Frost, #FrostBeam2); }
  else if n == 3 { spawn_with(#Frost, #FrostBeam3); }
  else if n >= 4 { spawn_with(#Frost, #FrostBeam4); }
  if n == 5 { spawn_with(#Frost, #FrostBeamFar1); }
  else if n == 6 { spawn_with(#Frost, #FrostBeamFar2); }
  else if n == 7 { spawn_with(#Frost, #FrostBeamFar3); }
  else if n == 8 { spawn_with(#Frost, #FrostBeamFar4); }
}

state Idle {
  event tick() { fighting(); }
begin:
  play_anim(#idle);
  face();
  if health <= Health / 2 { sleep(24); }
  else { sleep(36 - Turn % 3 * 4); }
  Turn += 1;
  if Turn % 4 == 0 { gotostate Breath; }
  if Turn % 2 == 1 { gotostate Stomp; }
  if health <= Health / 2 { gotostate Icicles; }
  gotostate Throw;
}

// Two snowballs, rolling at her.
state Throw {
  event tick() { fighting(); }
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

// Icicles, fast at her knees: two close, a pause, two close (always so: a jump each, or one
// for two well timed).
state Icicles {
  event tick() { fighting(); }
begin:
  face();
  play_anim(#throw);
  play_sound(#hush);
  sleep(24);
  spawn_with(#Frost, #FrostIcicle);
  sleep(16);
  spawn_with(#Frost, #FrostIcicle);
  sleep(44);
  face();
  spawn_with(#Frost, #FrostIcicle);
  sleep(16);
  spawn_with(#Frost, #FrostIcicle);
  play_anim(#idle);
  sleep(30);
  gotostate Idle;
}

// He jumps at her: off toward where she stands, landing there (in the park); the ground breaks,
// a ripple runs to where she is and a pillar of ice breaks out.
state Stomp {
  event tick() { fighting(); }
begin:
  face();
  play_anim(#idle);
  sleep(10);
  Shots = int(player_x());
  if Shots < ARENA_L { Shots = ARENA_L; }
  if Shots > ARENA_R { Shots = ARENA_R; }
  Vx = fix(Shots - int(x)) / fix(JUMP);
  Vy = -JUMP_UP;
  play_anim(#jump);
  arm(#snow, 3, Element.Ice);   // in the air he strikes (the jump's frame: its box `snow`)
  play_sound(#jump);
  Shots = 0;
  while Shots < JUMP {
    x += Vx;
    y += Vy;
    Vy += GRAVITY;
    Shots += 1;
    sleep(1);
  }
  y = home_y;
  play_anim(#land);
  play_sound(#thud);
  face();
  Shots = pillar_at();
  if spawn_with(#Frost, #FrostRipple) { note_pillar(Shots); }
  sleep(30);
  gotostate Idle;
}

// His frost breath: once his newest pillar is out, he draws breath (a moment to take cover),
// then it pours out and the beam runs along the ground.
state Breath {
  event tick() { fighting(); }
begin:
  face();
  play_anim(#idle);
  while Clock < PillarA / 1024 { sleep(1); }   // (his newest pillar out first)
  play_sound(#hush);
  sleep(50);
  face();
  play_anim(#breath);
  arm(#frost, 3, Element.Ice);  // where his breath pours (its box `frost`)
  breathe();
  sleep(96);
  gotostate Idle;
}

// Beaten: a shadow leaves him; he is a snowman again.
state Melting {
begin:
  Defeated = true;
  player_lock(600);
  disarm(#snow);
  disarm(#frost);
  ui_clear(0, 4, 16, 3);
  fade_music(0, 120);
  y = home_y;
  Shots = 0;
  while Shots < 6 {
    hide();
    sleep(4);
    show();
    sleep(4);
    Shots += 1;
  }
  spawn_with(#Frost, #FrostShade);
  set_scale(100);
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
