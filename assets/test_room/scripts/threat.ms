class Threat extends Attachment;

// Dynamic music that follows what the player is doing (sound-format.md, "Dynamic music"),
// attached to a room by its property `attach = Threat` (the rural street, with night_escape:
// calm, escape, pursuit). Every frame it adds up the danger, one step each for:
//   - moving: over the last 3 seconds the player went at least MOVING pixels (|dx| + |dy| per
//     frame, at most 8, summed per second; the three whole seconds before this one): on
//     average 0.75 pixels a frame, running being 2;
//   - an enemy in sight: one within NOTICE pixels;
//   - combat: one within CLOSE, or for SHAKEN frames after the player was struck;
// and sets the music intensity to it (0 to 3). The music rises at once and calms down a step at
// a time, each after the danger has stayed lower for HOLD frames, so it does not flutter at the
// edge of a range. Distances are from the player (an attachment measures from the player).

const int MOVING = 135;   // pixels in 3 seconds (180 frames at 0.75)
const int NOTICE = 96;    // as far as a knight wakes
const int CLOSE = 36;     // a lunge away
const int HOLD = 180;     // frames before the music calms down a step
const int SHAKEN = 240;   // frames a hit keeps the combat step

var int Level;     // the intensity set
var int Calm;      // frames the danger has been below it
var int Health;    // the player's health a frame ago
var int Since;     // frames left of being shaken
var fix LastX;     // where the player was a frame ago
var fix LastY;
var int Part;      // this second so far: the distance in sixteenths of a pixel * 64 + its frames
var int Seconds;   // the last three seconds' distances in pixels, 1024 to a second, newest lowest

event attached() {
  Level = 0;
  Calm = 0;
  Since = 0;
  Health = player_health();
  LastX = player_x();
  LastY = player_y();
  Part = 0;
  Seconds = 0;
  music_intensity(0);
}

event tick() {
  // How far the player went, kept for three seconds.
  var fix dx = player_x() - LastX;
  var fix dy = player_y() - LastY;
  if dx < 0.0 { dx = -dx; }
  if dy < 0.0 { dy = -dy; }
  if dx + dy > 8.0 { dx = 8.0; dy = 0.0; }   // a door or a teleport is no run
  Part += int((dx + dy) * 16.0) * 64 + 1;
  LastX = player_x();
  LastY = player_y();
  if Part % 64 >= 60 {
    var int pixels = Part / 64 / 16;
    if pixels > 1023 { pixels = 1023; }
    Seconds = Seconds % (1024 * 1024) * 1024 + pixels;
    Part = 0;
  }
  var int gone = Seconds % 1024 + Seconds / 1024 % 1024 + Seconds / (1024 * 1024);
  var int danger = 0;
  if gone >= MOVING { danger += 1; }
  if handle_valid(nearest_enemy(NOTICE, #Any)) { danger += 1; }
  if player_health() < Health { Since = SHAKEN; }
  Health = player_health();
  if Since > 0 { Since -= 1; }
  if Since > 0 || handle_valid(nearest_enemy(CLOSE, #Any)) { danger += 1; }
  if danger >= Level {
    Level = danger;
    Calm = 0;
  } else {
    Calm += 1;
    if Calm >= HOLD {
      Level -= 1;
      Calm = 0;
    }
  }
  music_intensity(Level);
}
