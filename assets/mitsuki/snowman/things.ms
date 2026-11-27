// The things of the snowman's fight (frost.ms): one class, Frost, does them all (the runtime
// loads 48 classes), each kind a record of Thing (things.bmdata): what it does, where it starts
// from its maker (the snowman: spawn_with makes it at his place), how it moves, what it shows.

enum ThingDoes { Roll, Ripple, Beam, Icicle, Shade }

struct Thing {
  ThingDoes does = Roll;
  int dx = 0;        // where it starts from its maker, toward Mitsuki (pixels)
  int dy = 0;        // and up (pixels; negative up)
  fix speed = 0.0;   // along the way it goes, a frame
  int life = 300;    // frames until it is gone
  int damage = 2;
  int length = 0;    // a beam: how many pieces of 32 pixels it grows to
  int delay = 0;     // a beam: frames before it shows (the far one waits for the near one)
}
