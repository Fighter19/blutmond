class Search extends Attachment;

// Mitsuki searching for Hina after the nightmare took her (Mitsuki's story; the outskirts west
// of the park: tools/make_outskirts.py). The rooms' attachment, which one by the room property
// Search.Room:
//
// 1 the riverside: the first time she comes, she calls for Hina; nothing answers. The first
//   monsters she meets out here (the bats) get a word from her.
// 2 the shrine path: a little way in, her phone rings: Hina, somewhere dark and round, "like the
//   inside of a Christmas ball, but wrong"; the call drops. Then it starts to snow (the story
//   flag Snowing: from now on it snows outside, set_snow), and she puts it together: a Christmas
//   ball, snow... the park. At the gate at the west end the way is closed: Hina is not here.

const int SNOW = 70;          // how hard it snows (set_snow percent)
const int MESSAGE_X = 330;    // as far along the shrine path as the call comes
const int GATE_X = 80;        // the closed gate's
const int NOTICE = 90;        // as near as she notices a monster

var(Story) int Room = 1;
var int Snow;                 // snowing now (fades in when it starts)
var bool AtGate;              // she said the way is closed (again once she walked away)

defaultproperties {
  start_state = Arrive;
}

event attached() {
  Snow = 0;
  if flag(#Snowing) { Snow = SNOW; }
  AtGate = false;
  music_intensity(1 + Room);
}

event tick() {
  set_snow(Snow);
  // The first monsters out here: a word for them, once.
  if Room == 1 && !flag(#MitsukiBats) && dialogue_done() {
    if a := nearest_enemy(NOTICE, #Any) {
      set_flag(#MitsukiBats, true);
      say_line(#MitsukiBats);
    }
  }
  if Room == 2 {
    if !flag(#HinaMessage) && player_x() < fix(MESSAGE_X) && on_ground() && dialogue_done() {
      gotostate Message;
    }
    if flag(#HinaMessage) && !AtGate && player_x() < fix(GATE_X) && dialogue_done() {
      AtGate = true;
      say_line(#MitsukiDeadEnd);
    }
    if AtGate && player_x() > fix(GATE_X + 80) { AtGate = false; }
  }
}

// Entered: at the riverside the first time, she calls for Hina.
state Arrive {
begin:
  if Room == 1 && !flag(#SearchCalled) {
    set_flag(#SearchCalled, true);
    sleep(40);
    say_line(#MitsukiCallsHina);
    wait_until(dialogue_done());
    sleep(50);
    say_line(#MitsukiNothing);
  }
  gotostate Searching;
}

state Searching {
}

// Hina's call, and the snow.
state Message {
begin:
  set_flag(#HinaMessage, true);
  owner_lock(65535);
  lock_move(GroundMove.Keep, AirMove.Keep);
  owner_velocity(0.0, 0.0);
  owner_play_anim(#idle);
  play_sound(#phone_ring);
  sleep(40);
  play_sound(#phone_ring);
  sleep(40);
  say_line(#HinaCanYouHear);
  wait_until(dialogue_done());
  say_line(#MitsukiWhereAreYou);
  wait_until(dialogue_done());
  say_line(#HinaDarkRound);
  wait_until(dialogue_done());
  say_line(#HinaChristmasBall);
  wait_until(dialogue_done());
  say_line(#MitsukiHina);
  wait_until(dialogue_done());
  sleep(30);
  say_line(#MitsukiCallGone);
  wait_until(dialogue_done());
  // It starts to snow.
  set_flag(#Snowing, true);
  sleep(40);
  while Snow < SNOW {
    Snow += 1;
    sleep(3);
  }
  say_line(#MitsukiSnow);
  wait_until(dialogue_done());
  say_line(#MitsukiThePark);
  wait_until(dialogue_done());
  owner_lock(0);
  gotostate Searching;
}
