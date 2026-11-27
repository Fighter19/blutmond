class Frost extends Actor;

// The things of the snowman's fight (snowman.ms), by their kind (Thing, things.bmdata; one class
// for all: the runtime loads 48):
//
// - Roll: a snowball rolling at Mitsuki along the ground (jump over it), until it leaves the
//   room or has struck;
// - Ripple: his stomp sent through the ground, snow thrown up as it goes, to where Mitsuki stood
//   (pillar_at: the snowman reckons the same place); there a pillar of ice breaks out. The
//   ripple and the pillar breaking out strike; standing, the pillar is cover from his breath
//   (the snowman stops the beam at it) for STANDS frames, then it crumbles;
// - Beam: his frost beam along the ground, growing from where his breath meets it, 1 to 4
//   pieces of 32 pixels (Kind.length; the far part of a longer one waits Kind.delay frames);
// - Icicle: flying fast at her knees (jump over it);
// - Shade: the shadow leaving him, beaten: it hangs a moment, then gets away, up and west.
//
// Made by the snowman with spawn_with (at his place): it moves itself to its start (dx toward
// Mitsuki; dy up). What strikes has a hit box: `dust` (the snowball), `ice` (the ripple, the
// pillar breaking out, the icicle), `frost` (the beam), in his sprite.

const int NEAR_MIN = 40;   // the snowman's own (snowman.ms): where a pillar may stand
const int ARENA_L = 40;
const int ARENA_R = 600;
const int STANDS = 300;    // frames a pillar stands (snowman.ms PILLAR_STANDS: as long, less a margin)

var(Combat) record<Thing> Kind = #FrostBall;
var fix Vx;
var int Life;
var int Target;            // the ripple: where it goes (the pillar's place); then the frame it broke out
var int Grown;             // the beam: pieces shown; the ripple: 1 once the pillar is out

defaultproperties {
  sprite = "snowman";
}

// Where the pillar of a stomp stands: where Mitsuki is, not too near him, in the park.
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
    if Kind.delay > 0 { hide(); }
    grow();
  } else if Kind.does == ThingDoes.Shade {
    play_anim(#shade);
    play_sound(#hush);
  }
}

// The beam: a piece more every 3 frames, up to its length.
function grow() {
  if Life < Kind.delay { return; }
  if Life == Kind.delay && Kind.delay > 0 { show(); }
  var int n = (Life - Kind.delay) / 3 + 1;
  if n > Kind.length { n = Kind.length; }
  if n == Grown { return; }
  Grown = n;
  if n == 1 { play_anim(#beam1); }
  else if n == 2 { play_anim(#beam2); }
  else if n == 3 { play_anim(#beam3); }
  else { play_anim(#beam4); }
  arm(#frost, Kind.damage, Element.Ice);   // (a hit holds while the animation it was armed in plays)
}

// The ripple: on to its place, then the pillar breaks out (it strikes while it rises: the
// sprite's frame of it rising has the hit box, the standing one none).
function ripple() {
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
    destroy();
    return;
  }
  if Kind.does == ThingDoes.Shade {
    if Life > 40 {
      x -= 1.8;
      y -= 1.1;
    }
  } else if Kind.does == ThingDoes.Beam {
    grow();
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
