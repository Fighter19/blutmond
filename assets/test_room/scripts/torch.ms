class Torch extends Actor;

// A torch in a bracket on the wall (the torch halls, docs/plans/lighting.md §7): only its flame
// burning. The light is the room's Light object next to it; the sprite is emissive, so the
// room's darkness leaves it bright.

event spawn() {
  play_anim(#burn);
}

defaultproperties {
  sprite = "torch";
}
