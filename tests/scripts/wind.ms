class Wind extends Attachment;

// The room as an owner (attachable-scripts.md §8): attached to a room by its property
// `attach = Wind`, it blows the player along while the player is in that room.

var(Wind) fix Strength = 0.25;
var save int Visits;   // kept for the room: how often the wind started
var int Ticks;

event attached() { Visits += 1; }

event tick() {
  impulse(Strength, 0);
  Ticks += 1;
}
