class Threat extends Attachment;

// Dynamic music that follows the danger the player is in (sound-format.md, "Dynamic music"),
// attached to a room by its property `attach = Threat` (the rural street, with night_escape:
// calm, escape, pursuit). Every frame it rates the danger: 2 while an enemy is within `Close` or
// for `Shaken` frames after the player was struck, 1 while one is within `Notice`, else 0; and
// sets the music intensity to it. The music rises at once and calms down a step at a time, each
// after the danger has stayed lower for `Hold` frames, so it does not flutter at the edge of a
// range. Distances are from the player (an attachment measures from the player).

var(Threat) int Notice = 96;   // pixels: as far as a knight wakes
var(Threat) int Close = 36;    // a lunge away
var(Threat) int Hold = 180;    // frames before the music calms down a step
var(Threat) int Shaken = 240;  // frames a hit keeps the danger at its height
var int Level;     // the intensity set
var int Calm;      // frames the danger has been below it
var int Health;    // the player's health a frame ago
var int Since;     // frames left of being shaken

event attached() {
  Level = 0;
  Calm = 0;
  Since = 0;
  Health = player_health();
  music_intensity(0);
}

event tick() {
  var int danger = 0;
  if player_health() < Health { Since = Shaken; }
  Health = player_health();
  if Since > 0 {
    Since -= 1;
    danger = 2;
  }
  if handle_valid(nearest_enemy(Close, #Any)) { danger = 2; }
  if danger == 0 && handle_valid(nearest_enemy(Notice, #Any)) { danger = 1; }
  if danger >= Level {
    Level = danger;
    Calm = 0;
  } else {
    Calm += 1;
    if Calm >= Hold {
      Level -= 1;
      Calm = 0;
    }
  }
  music_intensity(Level);
}
