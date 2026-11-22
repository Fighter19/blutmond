class MapMenu extends Screen;

// The map (docs/spec/world-format.md): the rooms the player has been in, the doors between
// them, the room they are in lighter, their place blinking; how much of the castle was
// explored. B, Start or A go back.

var int Ticks;

event opened() {
  ui_frame(1, 1, 28, 18);
  ui_label(3, 2, "Map");
  ui_number(21, 2, map_percent(), 3);
  ui_label(24, 2, "%");
  ui_map(2, 4, 26, 14);
}

event tick() {
  // The player's place blinks with the game frame: drawn again now and then.
  Ticks = Ticks + 1;
  if Ticks % 8 == 0 { ui_map(2, 4, 26, 14); }
}

event press(name b) {
  if b == #B || b == #Start || b == #A { close_screen(); }
}
