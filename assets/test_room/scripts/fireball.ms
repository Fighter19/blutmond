class Fireball extends Actor;

// What the moon bat spits: made with spawn(#Fireball, x, y), it flies straight at where the
// player was, burns what it strikes (its hit box `core`) and is gone after 100 frames or when
// it has struck.

var fix Vx;
var fix Vy;
var int Life;

event spawn() {
  // Toward the player's middle, reaching it in 48 frames, and on.
  Vx = (player_x() - x) / 48.0;
  Vy = (player_y() - 12.0 - y) / 48.0;
  play_anim(#idle);
  arm(#core, 3, Element.Fire);
}

event tick() {
  x += Vx;
  y += Vy;
  Life += 1;
  if Life >= 100 { destroy(); }
}

event struck(int amount) {
  destroy();
}

defaultproperties {
  sprite = "fireball";
}
