class Door extends Actor;

// A door leads to `target_room` (the properties of the object in the room say where). A door
// with `needs` set only opens once the player has that item.

var(Door) name needs = #nothing;

event touch() {
  if needs == #nothing || has_item(needs) {
    enter_door();
  }
}
