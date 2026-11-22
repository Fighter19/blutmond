class MoonBat extends Actor;

// Vesper, the moon bat: the boss of the hall behind the second room. When the player comes in
// it wakes, comes down and speaks (the player waits); then it flies from side to side above
// the hall, dives at the player with its bite, and every third time spits three fireballs.
// Below half its health it is faster. Defeated, it leaves the Iron Helm behind and stays
// defeated (a `save` variable).

var save bool Defeated;
var int Turn;           // turns flown so far
var bool Right;         // the side it flies to next
var fix Tx;
var fix Ty;
var fix Speed = 1.0;
var int Shots;

const int Health = 24;

event spawn() {
  set_knockback(0);   // a boss does not budge; it still flashes when struck
  if Defeated {
    hide();
    gotostate Gone;
  }
}

// Its bar at the top right (the player's health is in the HUD at the top left).
function hud() {
  // Below the player's health: the top right is the small map's.
  ui_frame(0, 4, 16, 3);
  ui_label(1, 5, "Vesper");
  ui_gauge(8, 5, 7, health * 100 / Health);
  ui_show(true);
}

event hit(int amount) {
  play_sound(#hit_moon);
  if health <= 0 { gotostate Dying; }
  else if health <= Health / 2 {
    // Enraged: faster, and the frantic arpeggio joins the music.
    Speed = 1.75;
    music_intensity(3);
  }
}

state Waiting {
  event tick() {
    if player_x() > 64.0 { gotostate Intro; }
  }
begin:
  play_anim(#idle);
}

state Intro {
begin:
  player_lock(2000);
  play_anim(#idle);
  face_player();
  while !move_toward(x, 96.0, 0.75) { sleep(1); }
  // The castle's tune has faded away by now (the room's music is "none"); a moment of silence,
  // then its song begins quietly: only the drone under the words (dynamic music, layer 0).
  sleep(30);
  music_intensity(0);
  play_music(#vesper);
  // The lines are records (data/lines.bmdata), with the speakers' portraits.
  say_line(#VesperWakes);
  wait_until(dialogue_done());
  say_line(#HeroAnswers);
  wait_until(dialogue_done());
  say_line(#VesperStays);
  wait_until(dialogue_done());
  player_lock(0);
  health = Health;
  // The fight: bass, heartbeat, the lead and the hats come in.
  music_intensity(2);
  gotostate Hover;
}

state Hover {
  event tick() { hud(); }
begin:
  play_anim(#idle);
  Right = !Right;
  Tx = 120.0;
  if Right { Tx = 360.0; }
  while !move_toward(Tx, 72.0, Speed) { sleep(1); }
  Turn += 1;
  if Turn % 3 == 0 { gotostate Fire; }
  gotostate Dive;
}

state Dive {
  event tick() { hud(); }
begin:
  face_player();
  sleep(20);
  play_anim(#dive);
  arm(#bite, 4, Element.None);
  Tx = player_x();
  Ty = player_y() - 8.0;
  while !move_toward(Tx, Ty, Speed * 2.5) { sleep(1); }
  sleep(12);
  disarm(#bite);
  play_anim(#idle);
  while !move_toward(x, 72.0, Speed) { sleep(1); }
  gotostate Hover;
}

state Fire {
  event tick() { hud(); }
begin:
  face_player();
  Shots = 0;
  while Shots < 3 {
    spawn(#Fireball, x, y);
    play_sound(#hurt);
    Shots += 1;
    sleep(24);
  }
  gotostate Hover;
}

state Dying {
begin:
  disarm(#bite);
  Defeated = true;
  player_lock(400);
  ui_clear(0, 4, 16, 3);
  fade_music(0, 150);
  Shots = 0;
  while Shots < 6 {
    hide();
    sleep(4);
    show();
    sleep(4);
    Shots += 1;
  }
  hide();
  play_music(#fanfare);   // once (the song does not loop), over the silence the fight left
  spawn_with(#Drop, #IronHelm);
  say_line(#VesperFades);
  wait_until(dialogue_done());
  player_lock(0);
  gotostate Gone;
}

state Gone {
}

defaultproperties {
  sprite = "moon_bat";
  start_state = Waiting;
}
