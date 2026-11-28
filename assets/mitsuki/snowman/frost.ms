class Frost extends Actor;

// The things of the snowman's fight (snowman.ms), by their kind (Thing, things.bmdata; one class
// for all: the runtime loads 48):
//
// - Roll: a snowball rolling at Mitsuki along the ground (jump over it), until it leaves the
//   room or has struck;
// - Ripple: his stomp sent through the ground, snow thrown up as it goes, to where Mitsuki stood
//   (pillar_at: the snowman reckons the same place); there a block of ice breaks out. The
//   ripple and the block breaking out strike; standing, the block is solid (set_solid: she
//   stands on it, and it stops his beam) for STANDS frames, then it crumbles;
// - Beam: his frost beam (set_beam: the engine draws it, a straight bright line, stops it at
//   the ground or a block, sprays snow where it ends, strikes her): from his mouth, sweeping
//   from the ground at his feet out and up (Kind.aim_from to Kind.aim_to, 256 a turn: 64 straight
//   down) over its life;
// - Icicle: flying fast at her knees (jump over it);
// - Shade: the shadow leaving him, beaten: it hangs a moment, then gets away, up and west.
//
// Made by the snowman with spawn_with (at his place): it moves itself to its start (dx toward
// Mitsuki; dy up). What strikes has a hit box: `dust` (the snowball), `ice` (the ripple, the
// block breaking out, the icicle), in his sprite; the beam strikes by itself.

const int NEAR_MIN = 40;   // the snowman's own (snowman.ms): where a block may stand
const int ARENA_L = 40;
const int ARENA_R = 600;
const int STANDS = 300;    // frames a block stands (snowman.ms PILLAR_STANDS: as long, less a margin)
const int BLOCK_W = 24;    // the block of ice, as its sprite shows it (snowman.py)
const int BLOCK_H = 40;
const int RISE = 6;        // frames it takes to break out (its rising frame, then solid)

var(Combat) record<Thing> Kind = #FrostBall;
var fix Vx;
var int Life;
var int Target;            // the ripple: where it goes (the block's place); then the frame it broke out
var int Grown;             // the ripple: 1 once the block breaks out, 2 once it is solid

defaultproperties {
  sprite = "snowman";
}

// Where the block of a stomp stands: where Mitsuki is, not too near him, in the park.
// (snowman.ms has the same.)
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

event spawn() {
  Life = 0;
  Grown = 0;
  health = 999;   // (not to be broken by her blade)
  face_player();
  var int side = 1;
  if player_x() < x { side = -1; }
  if Kind.does == ThingDoes.Ripple {
    Target = pillar_at();
    side = 1;
    if Target < int(x) { side = -1; }
    play_anim(#ripple);
    play_sound(#drag);
    arm(#ice, Kind.damage, Element.Ice);
  }
  x += fix(Kind.dx * side);
  y += fix(Kind.dy);
  Vx = Kind.speed * fix(side);
  if Kind.does == ThingDoes.Roll {
    health = 1;
    play_anim(#roll);
    play_sound(#drag);
    arm(#dust, Kind.damage, Element.Ice);
  } else if Kind.does == ThingDoes.Icicle {
    play_anim(#icicle);
    play_sound(#hush);
    arm(#ice, Kind.damage, Element.Ice);
  } else if Kind.does == ThingDoes.Beam {
    hide();   // (nothing of its sprite: the engine draws the beam)
    play_sound(#hush);
    sweep();
  } else if Kind.does == ThingDoes.Shade {
    play_anim(#shade);
    play_sound(#hush);
  }
}

// The beam: its angle this frame, from Kind.aim_from to Kind.aim_to over its life.
function sweep() {
  var int aim = Kind.aim_from + (Kind.aim_to - Kind.aim_from) * Life / Kind.life;
  var int reach = Kind.reach;
  var int damage = Kind.damage;
  set_beam(aim, reach, damage);
}

// The ripple: on to its place, then the block breaks out (it strikes while it rises: the
// sprite's frame of it rising has the hit box, the standing one none), then is solid.
function ripple() {
  if Grown == 1 && Life - Target >= RISE {
    Grown = 2;
    set_solid(BLOCK_W, BLOCK_H);
  }
  if Grown != 0 { return; }
  x += Vx;
  if (Vx > 0.0 && x >= fix(Target)) || (Vx < 0.0 && x <= fix(Target)) {
    x = fix(Target);
    Target = Life;
    Grown = 1;
    play_anim(#rise);
    play_sound(#crumble);
    arm(#ice, Kind.damage, Element.Ice);
  }
}

event tick() {
  Life += 1;
  if Kind.does != ThingDoes.Shade && !flag(#SnowmanFight) {
    destroy();
    return;
  }
  if Life >= Kind.life || (Kind.does == ThingDoes.Ripple && Grown != 0 && Life - Target >= STANDS) {
    if Kind.does == ThingDoes.Ripple { play_sound(#crumble); }
    if Kind.does == ThingDoes.Beam { set_beam(0, 0, 0); }
    destroy();
    return;
  }
  if Kind.does == ThingDoes.Shade {
    if Life > 40 {
      x -= 1.8;
      y -= 1.1;
    }
  } else if Kind.does == ThingDoes.Beam {
    sweep();
  } else if Kind.does == ThingDoes.Ripple {
    ripple();
  } else {
    x += Vx;
    if x < 0.0 || x > 640.0 { destroy(); }
  }
}

event struck(int amount) {
  if Kind.does == ThingDoes.Roll { destroy(); }
}
