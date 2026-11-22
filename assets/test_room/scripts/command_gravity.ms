class CommandGravity extends Attachment;

// Gravity Flip (the Moon Charm's command): the player's gravity turns over, or back. Through a
// door or a teleport it is the usual way again.

var record<Command> Item;

event attached() {
  flip_gravity();
  play_sound(#teleport);
}
