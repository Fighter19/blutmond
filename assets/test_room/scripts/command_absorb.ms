class CommandAbsorb extends Attachment;

// Blood Absorb (the Crimson Charm's command): four pulses, one every 20 frames (as long as a
// struck figure cannot be struck again), each strikes everything within 40 pixels on both
// sides for 2; what is struck heals the player as much.

var record<Command> Item;
var int Frame;
var int Drawn;

event attached() {
  owner_lock(70);
}

event tick() {
  if Frame % 20 == 0 && Frame < 80 {
    hit_box(-40, 2, 2, Element.None);
    play_sound(#hurt);
  }
  Frame += 1;
}

event struck(int amount) {
  Drawn += amount;
  heal(amount);
}
