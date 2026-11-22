class Pedestal extends Actor;

// An ability on a pedestal (first-items.md §9): touching it takes it, and the ability acts from
// then on (its stats count, its script is attached). Which is the property Gives in Tiled;
// taken stays taken.

var(Loot) record<Ability> Gives = #DoubleJump;
var save bool Taken;

event spawn() {
  if Taken { hide(); }
  else { play_anim(#idle); }
}

event touch() {
  if !Taken {
    Taken = true;
    give(Gives, 1);
    hide();
  }
}

defaultproperties {
  sprite = "drop";
}
