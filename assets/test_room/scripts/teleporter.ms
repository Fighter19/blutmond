class Teleporter extends Actor;

// Touched: the player is taken to (X, Y) of the room Room, with the wave of a teleport
// (effects.h). Set Room, X and Y in Tiled.

var(Teleporter) name Room = #test_room;
var(Teleporter) int X = 40;
var(Teleporter) int Y = 160;

event touch() {
  teleport(Room, X, Y);
}
