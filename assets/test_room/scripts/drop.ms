class Drop extends Actor;

// Something left behind (first-items.md §9): made with spawn_with(#Drop, record) where an enemy
// was defeated; it falls to the floor and is the player's when they touch it. Gone with the
// room, like everything made at run time.

var record<Item> Gives;
var fix Floor;

event spawn() {
  Floor = floor_y();
  play_anim(#idle);
}

event tick() {
  if y < Floor { move_toward(x, Floor, 2.0); }
}

event touch() {
  give(Gives, 1);   // with the pickup sound
  destroy();
}

defaultproperties {
  sprite = "drop";
}
