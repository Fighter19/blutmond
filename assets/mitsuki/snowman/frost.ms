class Frost extends Actor;

// The things of the snowman's fight (snowman.ms), by their kind (Thing, things.bmdata; one class
// for all: the runtime loads 48):
//
// - Roll: a snowball rolling at Mitsuki along the ground (jump over it), until it leaves the
//   room or has struck;
// - Shard: ice shards knocked up from the ground by his stomp, beside him, a moment;
// - Breath: his frost breath, going out along the ground (two, one over the other) until it
//   reaches a block of ice (hide behind one) or the end of the room;
// - Block: a block of ice west or east of him, there while he fights;
// - Shade: the shadow leaving him, beaten: it hangs a moment, then gets away, up and west.
//
// Made by the snowman with spawn_with (at his place): it moves itself to its start (dx along the
// way he faces, toward Mitsuki; dy up). What strikes has a hit box: `ice` (Roll, Shard), `frost`
// (Breath), in his sprite.

const int BLOCK_A = 230;   // the blocks of ice (park.tmx)
const int BLOCK_B = 420;

var(Combat) record<Thing> Kind = #FrostBall;
var fix Vx;
var int Life;

defaultproperties {
  sprite = "snowman";
}

event spawn() {
  Life = 0;
  if Kind.does == ThingDoes.Block {
    x += fix(Kind.dx);   // (west and east of him: where the breath stops, BLOCK_A, BLOCK_B)
    gotostate Standing;
  }
  // Toward Mitsuki from the snowman's place.
  var int side = 1;
  if player_x() < x { side = -1; }
  // (Shards to his left and right, whichever way he faces.)
  if Kind.does == ThingDoes.Shard { x += fix(Kind.dx); }
  else { x += fix(Kind.dx * side); }
  y += fix(Kind.dy);
  Vx = Kind.speed * fix(side);
  if Kind.does == ThingDoes.Roll {
    play_anim(#roll);
    play_sound(#drag);
    arm(#ice, Kind.damage, Element.Ice);
  } else if Kind.does == ThingDoes.Shard {
    play_anim(#shard);
    play_sound(#crumble);
    arm(#ice, Kind.damage, Element.Ice);
  } else if Kind.does == ThingDoes.Breath {
    play_anim(#frost);
    arm(#frost, Kind.damage, Element.Ice);
  } else if Kind.does == ThingDoes.Shade {
    play_anim(#shade);
    play_sound(#hush);
  }
}

// Whether a breath got to the block at `bx` this frame.
function bool at_block(int bx) {
  if Vx > 0.0 { return x >= fix(bx - 8) && x - Vx < fix(bx - 8); }
  return x <= fix(bx + 8) && x - Vx > fix(bx + 8);
}

event tick() {
  if Kind.does == ThingDoes.Block { return; }
  Life += 1;
  if Kind.does == ThingDoes.Shade {
    if Life > 40 {
      x -= 1.8;
      y -= 1.1;
    }
  } else {
    x += Vx;
  }
  if Kind.does == ThingDoes.Breath && (at_block(BLOCK_A) || at_block(BLOCK_B)) { destroy(); }
  if Life >= Kind.life || x < 0.0 || x > 640.0 { destroy(); }
}

event struck(int amount) {
  if Kind.does == ThingDoes.Roll { destroy(); }
}

// A block of ice: made as the snowman's fight begins, gone when it ends (SnowmanFight).
state Standing {
begin:
  play_anim(#block);
  wait_until(!flag(#SnowmanFight));
  destroy();
}
