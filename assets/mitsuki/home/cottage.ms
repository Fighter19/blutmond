class Cottage extends Attachment;

// Mitsuki's story in the rural street (the street's `attach`; a room has four attachment
// slots, so one does both):
//
// - The street's cottage is Mitsuki's home: its window, drawn lit in the street's tiles, is lit
//   while the light in her room is on (the story flag MitsukiLight: the switch by her bed,
//   prologue.ms) and dark while it is off. The window's colours are the street's
//   `glow_switch`: set_glow turns them, no sprite covers them.
// - Once the nightmare took Hina (ParkDone) a knight stands in the street (spawned as the room
//   is entered: before, there were no monsters out), and the first time Mitsuki comes near it
//   she has a word for it (once: the flag MitsukiKnight).

const int KNIGHT_X = 350;   // where it stands
const int GROUND = 128;
const int NOTICE = 96;      // as near as she notices it

var bool Spawned;

event attached() {
  Spawned = false;
}

event tick() {
  if flag(#MitsukiLight) { set_glow(100); }
  else { set_glow(18); }
  if !flag(#ParkDone) { return; }
  if !Spawned {
    Spawned = spawn(#Knight, fix(KNIGHT_X), fix(GROUND));
  }
  if !flag(#MitsukiKnight) && dialogue_done() {
    var int d = int(player_x()) - KNIGHT_X;
    if d < 0 { d = -d; }
    if d < NOTICE {
      set_flag(#MitsukiKnight, true);
      say_line(#MitsukiKnight);
    }
  }
}
