class Pickup extends Actor;

// Something to collect. Once taken it stays taken: Taken is a `save` variable, kept for this
// object across rooms and in the save game.

var(Pickup) name item = #coin;
var save bool Taken;

event spawn() {
  if Taken { hide(); }
}

event touch() {
  if !Taken {
    Taken = true;
    give_item(item);
    hide();
  }
}
