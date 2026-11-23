class Knight extends Actor;

// A knight with a shield (docs/plans/hitboxes.md §4). Its shield, the guard box `shield` of
// its sprite, stops what comes at it from the front; it turns slowly, so the way in is from
// behind (jump over it) or from above (a downward stab at the helm), or into its lunge, when the
// shield is swung back. It keeps near the place it stands, steps up to the player and lunges
// when they are close ahead. Struck, it flinches and turns to face whoever struck it; struck
// often enough it is defeated (until the room is entered again).

var(Combat) record<Enemy> Kind = #Knight;
var int Behind;     // frames the player has been behind it
var int Rest;       // frames until it may lunge again
var int Walking;    // 1 while the walk animation plays
var int Roll;

event spawn() {
  health = Kind.health;
  set_affinity(Kind.weak, Kind.resists);
  set_knockback(25);
}

// Whether the player is on the side it faces.
function bool player_ahead() {
  if facing > 0 { return player_x() >= x; }
  return player_x() <= x;
}

event hit(int amount) {
  play_sound(Kind.hit_sound);
  if health <= 0 { gotostate Defeated; }
  else { gotostate Flinching; }
}

// The shield took it.
event blocked(int amount) { play_sound(#clang); }

state Guarding {
  event tick() {
    if Rest > 0 { Rest -= 1; }
    if !player_ahead() {
      // Slow to turn: the moment to strike its back.
      Behind += 1;
      if Behind >= Kind.patience * 8 {
        face_player();
        Behind = 0;
      }
      stand();
      return;
    }
    Behind = 0;
    if player_distance() < 30 && Rest == 0 { gotostate Lunging; }
    if player_distance() < Kind.wake_distance && player_distance() > 22 && near_home() {
      move_toward(player_x(), home_y, Kind.speed);
      if Walking == 0 {
        play_anim(#walk);
        Walking = 1;
      }
    } else {
      stand();
    }
  }
begin:
  Walking = 0;
  play_anim(#idle);
}

// Not more than give_up_distance from where it was placed, unless it is on its way back.
function bool near_home() {
  if x > home_x + fix(Kind.give_up_distance) { return player_x() < x; }
  if x < home_x - fix(Kind.give_up_distance) { return player_x() > x; }
  return true;
}

function stand() {
  if Walking == 1 {
    play_anim(#idle);
    Walking = 0;
  }
}

// The lunge: the sword wound up, thrust ahead (the hit box `sword`, while the shield is behind
// it), drawn back.
state Lunging {
begin:
  Walking = 0;
  play_anim(#attack);
  arm(#sword, Kind.damage, Element.None);
  finish_anim();
  disarm(#sword);
  Rest = 50;
  gotostate Guarding;
}

state Flinching {
begin:
  disarm(#sword);
  Walking = 0;
  play_anim(#flinch);
  finish_anim();
  face_player();
  Behind = 0;
  Rest = 20;
  gotostate Guarding;
}

state Defeated {
begin:
  disarm(#sword);
  hide();
  Roll = random(100);
  if Kind.drop_chance > 0 && Roll < Kind.drop_chance + 2 * stat(Stat.Luck) { spawn_with(#Drop, Kind.drop); }
}

defaultproperties {
  sprite = "knight";
  start_state = Guarding;
}
