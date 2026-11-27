class ParkScene extends Attachment;

// Mitsuki's park scene (park.tmx: the room's `attach`). The park is dark and the music tense
// (park_dread), more so the nearer she comes to the bench at its west end (music intensity 0
// to 3 by the distance). Close to it, the scene: Hina's eyes under the bench; she storms out
// and into Mitsuki's arms, who rocks her and pats her head while Hina tells how scared she
// was and how relieved; the music turns warm with an ache in it (park_reunion). Mitsuki lets
// go, takes her hand and leads her to the bench; they sit close, Mitsuki holding Hina's hand
// in her lap (a close-up of their hands: Hands), and Hina slowly opens up about what
// has weighed on her since "that day" (never said what). Then the nightmare comes, hushing,
// and takes her (nightmare.ms); she reaches out for Mitsuki and is carried off into the
// night, screaming from off-screen. Mitsuki is left reaching after her.
//
// Hina, the nightmare and the close-up follow the scene through story flags (Park*); after it
// (ParkDone) the park stays as it was left, tense, and Hina gone.

const int BENCH = 137;      // the bench's middle (Hina under it)
const int NEAR = 70;        // how close the scene starts
const int SNOWMAN_X = 300;  // the snowman in the snow (park.tmx)
const int NIGHTMARE_X = 194;  // where the nightmare rises (made then: spawn)
const int GROUND = 128;
const int HANDS_X = 126;    // the close-up of their hands (made when it is shown)
const int HANDS_Y = 56;
const int SIT_X = 126;      // Mitsuki's place on the bench (Hina at SIT_X + 7, shoulder to shoulder: pair.py)

var int Level;              // the music's intensity
var bool UpBefore;          // Up was held a frame ago (the snowman takes a fresh press)
var fix Away;               // thrown off the snowman: her speed
var int Steps;

event attached() {
  Level = -1;
}

defaultproperties {
  start_state = Approach;
}

// The park before the scene: the music by the distance to the bench.
state Approach {
  event tick() {
    if flag(#ParkDone) {
      gotostate After;
      return;   // (not on into the scene: from the riverside she comes in near the bench)
    }
    var int d = int(player_x()) - BENCH;
    var int level = 0;
    if d < 360 { level = 1; }
    if d < 240 { level = 2; }
    if d < 140 { level = 3; }
    if level != Level {
      Level = level;
      music_intensity(level);
    }
    if d <= NEAR && on_ground() { gotostate Scene; }
  }
}

// After the scene: the park tense, and once it snows (Snowing: the outskirts, search.ms) snow,
// and the snowman (snowman.ms): Up in front of him once, she doubts anyone built him tonight;
// twice, she grabs the ball in his hands (Grab). Beaten, he is a happy snowman; Up: a word.
state After {
  event tick() {
    if flag(#Snowing) { set_snow(70); }
    var bool up = held(#Up) && !UpBefore && on_ground() && dialogue_done();
    UpBefore = held(#Up);
    var int d = int(player_x()) - SNOWMAN_X;
    if d < 0 { d = -d; }
    if !up || d > 20 || !flag(#Snowing) || flag(#SnowmanFight) { return; }
    if flag(#SnowmanDone) {
      say_line(#MitsukiSnowmanAfter);
    } else if !flag(#SnowmanLooked) {
      set_flag(#SnowmanLooked, true);
      say_line(#MitsukiSnowmanNight);
    } else if !flag(#SnowmanGrab) {
      gotostate Grab;
    }
  }
begin:
  UpBefore = true;
  music_intensity(3);
}

// She grabs the ball; he holds on (SnowmanHolds); he grows and pushes her off: the fight.
state Grab {
begin:
  owner_lock(65535);
  lock_move(GroundMove.Keep, AirMove.Keep);
  owner_velocity(0.0, 0.0);
  if player_x() < fix(SNOWMAN_X) { owner_turn(1); }
  else { owner_turn(-1); }
  say_line(#MitsukiGrab);
  wait_until(dialogue_done());
  owner_play_anim(#flip);
  set_flag(#SnowmanGrab, true);
  sleep(30);
  say_line(#MitsukiHolds);
  wait_until(dialogue_done());
  set_flag(#SnowmanHolds, true);
  wait_until(flag(#SnowmanFight));
  // Thrown off as he grows.
  owner_play_anim(#idle);
  Away = -2.5;
  if player_x() > fix(SNOWMAN_X) { Away = 2.5; }
  Steps = 0;
  while Steps < 14 {
    owner_velocity(Away, 0.0);
    sleep(1);
    Steps += 1;
  }
  owner_velocity(0.0, 0.0);
  owner_lock(0);
  gotostate After;
}

state Scene {
begin:
  owner_lock(65535);
  owner_turn(-1);
  owner_play_anim(#idle);
  sleep(40);
  // She storms out from under the bench, into Mitsuki's arms; the music lets go.
  set_flag(#ParkFound, true);
  play_music(#park_reunion);
  say_line(#ParkMitsuki);
  sleep(26);
  set_flag(#ParkHug, true);
  owner_play_anim(#hug);
  wait_until(dialogue_done());
  say_line(#ParkCame);
  wait_until(dialogue_done());
  say_line(#ParkTwelve);
  wait_until(dialogue_done());
  say_line(#ParkScared);
  wait_until(dialogue_done());
  // A pat on her head.
  set_flag(#ParkPat, true);
  owner_play_anim(#pat);
  say_line(#ParkDidWell);
  wait_until(dialogue_done());
  set_flag(#ParkPat, false);
  owner_play_anim(#hug);
  say_line(#ParkDontLetGo);
  wait_until(dialogue_done());
  gotostate Lead;
}

// She lets go and takes Hina's hand, and leads her to the bench; they sit.
state Lead {
begin:
  set_flag(#ParkLead, true);
  owner_play_anim(#lead);
  say_line(#ParkSitDown);
  wait_until(dialogue_done());
  lock_move(GroundMove.Keep, AirMove.Keep);
  while player_x() > fix(SIT_X) {
    owner_velocity(-0.6, 0.0);
    sleep(1);
  }
  owner_velocity(0.0, 0.0);
  // They sit, close; Mitsuki holds her hand in Hina's lap.
  owner_turn(1);
  owner_play_anim(#sit_hand);
  set_flag(#ParkSit, true);
  sleep(40);
  say_line(#ParkCold);
  wait_until(dialogue_done());
  say_line(#ParkWarm);
  wait_until(dialogue_done());
  spawn(#Hands, fix(HANDS_X), fix(HANDS_Y));   // (made now: the room keeps its object slots free)
  set_flag(#ParkHands, true);
  sleep(90);
  say_line(#ParkTellYou);
  wait_until(dialogue_done());
  say_line(#ParkMhm);
  wait_until(dialogue_done());
  set_flag(#ParkHands, false);
  gotostate Low;
}

// Hina opens up; then she leans her head into Mitsuki's lap, and Mitsuki strokes her hair.
state Low {
begin:
  say_line(#ParkSince);
  wait_until(dialogue_done());
  say_line(#ParkChest);
  wait_until(dialogue_done());
  say_line(#ParkOutLoud);
  wait_until(dialogue_done());
  set_flag(#ParkLean, true);
  owner_play_anim(#sit_leaned);
  sleep(60);
  say_line(#ParkLookAtMe);
  wait_until(dialogue_done());
  set_flag(#ParkCaress, true);
  owner_play_anim(#caress);
  sleep(50);
  say_line(#ParkHere);
  wait_until(dialogue_done());
  gotostate Taken;
}

// The nightmare: it takes her; she reaches out; it is gone with her.
state Taken {
begin:
  fade_music(0, 30);
  spawn(#Nightmare, fix(NIGHTMARE_X), fix(GROUND));
  set_flag(#ParkTaken, true);
  wait_until(flag(#NightmareGrab));
  owner_play_anim(#idle);
  wait_until(flag(#NightmareGone));
  sleep(20);
  say_line(#ParkScream);
  wait_until(dialogue_done());
  owner_turn(1);
  owner_play_anim(#reach_out);
  sleep(50);
  say_line(#ParkHina);
  wait_until(dialogue_done());
  say_line(#ParkGiveBack);
  wait_until(dialogue_done());
  // After: the park as it was left; the flags of the scene cleared but ParkDone.
  set_flag(#ParkDone, true);
  set_flag(#ParkFound, false);
  set_flag(#ParkHug, false);
  set_flag(#ParkLead, false);
  set_flag(#ParkSit, false);
  set_flag(#ParkLean, false);
  set_flag(#ParkCaress, false);
  set_flag(#ParkTaken, false);
  set_flag(#NightmareGrab, false);
  set_flag(#NightmareLeap, false);
  set_flag(#NightmareGone, false);
  play_music(#park_dread);
  fade_music(100, 60);
  music_intensity(3);
  owner_play_anim(#idle);
  owner_lock(0);
  gotostate After;
}
