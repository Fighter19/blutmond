class Threat extends Attachment;

// Dynamic music that follows what the player is doing (sound-format.md, "Dynamic music"),
// attached to a room by its property `attach = Threat` (the rural street, with night_escape:
// calm, escape, pursuit). Every frame it adds up the danger, one step each for:
//   - moving: over the last 3 seconds the player went at least MOVING pixels along (|dx| per
//     frame, at most 8, summed per second; the three whole seconds before this one): on
//     average 0.75 pixels a frame, running being 2;
//   - an enemy in sight: one within NOTICE pixels;
//   - combat: one within CLOSE, or for SHAKEN frames after the player was struck;
// and sets the music intensity to it (0 to 3). The music rises at once and calms down a step at
// a time, each after the danger has stayed lower for HOLD frames, so it does not flutter at the
// edge of a range. Distances are from the player (an attachment measures from the player).
//
// It runs every frame in an interpreter on a GBA without division: the seconds' sums are
// divided out only once a second, when a second ends, and whether they add up to moving is
// kept as a flag in the same number (MOVED), so a frame only compares.

const int MOVING = 135;         // pixels in 3 seconds (180 frames at 0.75)
const int NOTICE = 96;          // as far as a knight wakes
const int CLOSE = 36;           // a lunge away
const int HOLD = 180;           // frames before the music calms down a step
const int SHAKEN = 240;         // frames a hit keeps the combat step
const int MOVED = 1073741824;   // in Seconds: the last three seconds were moving

var int Level;     // the intensity set
var int Calm;      // frames the danger has been below it
var int Health;    // the player's health a frame ago
var int Since;     // frames left of being shaken
var fix LastX;     // where the player was a frame ago
var int Part;      // the distance gone this second, in sixteenths of a pixel
var int Ticks;     // frames of this second
var int Seconds;   // the last three seconds' distances in pixels, 1024 to a second, newest lowest; + MOVED

event attached() {
  Level = 0;
  Calm = 0;
  Since = 0;
  Health = player_health();
  LastX = player_x();
  Part = 0;
  Ticks = 0;
  Seconds = 0;
  music_intensity(0);
}

event tick() {
  // How far the player went, kept for three seconds.
  var fix x = player_x();
  var fix dx = x - LastX;
  if dx < 0.0 { dx = -dx; }
  if dx > 8.0 { dx = 8.0; }   // a door or a teleport is no run
  Part += int(dx * 16.0);
  LastX = x;
  Ticks += 1;
  if Ticks >= 60 {
    var int last = Seconds;
    if last >= MOVED { last -= MOVED; }
    var int pixels = Part / 16;
    if pixels > 1023 { pixels = 1023; }
    last = last % (1024 * 1024) * 1024 + pixels;
    Seconds = last;
    if last % 1024 + last / 1024 % 1024 + last / (1024 * 1024) >= MOVING { Seconds += MOVED; }
    Part = 0;
    Ticks = 0;
  }
  var int danger = 0;
  if Seconds >= MOVED { danger += 1; }
  var int health = player_health();
  if health < Health { Since = SHAKEN; }
  Health = health;
  if Since > 0 { Since -= 1; }
  var bool close = Since > 0;
  if a := nearest_enemy(NOTICE, #Any) {
    danger += 1;
    if distance_to(a) <= CLOSE { close = true; }
  }
  if close { danger += 1; }
  if danger >= Level {
    Calm = 0;
    if danger > Level {
      Level = danger;
      music_intensity(Level);
    }
  } else {
    Calm += 1;
    if Calm >= HOLD {
      Level -= 1;
      Calm = 0;
      music_intensity(Level);
    }
  }
}
