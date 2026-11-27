class Uneasy extends Attachment;

// Mitsuki's uneasy walk before the monsters are out (her room, the street, the park until the
// nightmare took Hina: the story flag ParkDone): slower, so the park's music has time to build
// as she nears the bench (60 % of her running speed). Attached by the rooms' property `attach`;
// it gives the speed back when the room is left, or at once when ParkDone comes on.

const int SLOWER = -40;   // add_speed percent

var bool Slow;

event attached() {
  Slow = !flag(#ParkDone);
  if Slow { add_speed(SLOWER); }
}

event tick() {
  if Slow && flag(#ParkDone) {
    add_speed(-SLOWER);
    Slow = false;
  }
}

event detached() {
  if Slow { add_speed(-SLOWER); }
  Slow = false;
}
