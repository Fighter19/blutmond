class Prologue extends Attachment;

// Mitsuki's prologue, in her room at home (mitsuki_home.tmx: the room's `attach`). Come in at
// her bed (a new game started there, or Prologue in the Character menu: the room's
// PlayerStart), she is asleep; the phone rings, she sits up and answers: Hina, in a panic,
// sees monsters in the street. Mitsuki does not believe her, then sees one at her own window,
// stays calm about it, and agrees to meet her at the park. Then she gets up, and the player
// has her: the door out (to the street, the bench by the lamp) stays shut until she has taken
// her katana from its rack (the door's `needs`); she says so if she tries.
//
// Her animations here (sleep, ring, call, wake) are drawn on the bed, from where she stands
// in front of it (assets/mitsuki/mitsuki.py), facing right: the prologue makes Mitsuki the
// player if another character is.

const int BED = 56;      // the room's PlayerStart: in front of the bed
const int DOOR = 304;    // the door's left edge

var int Rings;
var bool Warned;         // she said she needs her katana (again once she walked away)
var bool Armed;          // she has it (and said so)

defaultproperties {
  start_state = Arrive;
}

event attached() {
  Rings = 0;
  Warned = false;
  Armed = has_item(#Katana);
}

// The room is attached before the player is put in it: where she is shows a frame later.
state Arrive {
begin:
  if player_x() >= fix(BED - 4) && player_x() <= fix(BED + 4) { gotostate Night; }
  gotostate Awake;
}

state Night {
begin:
  if character() != #player_mitsuki && has_character(#player_mitsuki) { set_character(#player_mitsuki); }
  owner_turn(1);
  owner_lock(65535);
  owner_play_anim(#sleep);
  sleep(150);
  // Three rings; she does not stir. A pause; a fourth, and she answers.
  while Rings < 3 {
    owner_play_anim(#ring);
    play_sound(#phone_ring);
    sleep(40);
    owner_play_anim(#sleep);
    sleep(30);
    Rings += 1;
  }
  sleep(40);
  owner_play_anim(#ring);
  play_sound(#phone_ring);
  sleep(30);
  owner_play_anim(#call);
  sleep(40);
  say_line(#HinaWakes);
  wait_until(dialogue_done());
  say_line(#MitsukiNow);
  wait_until(dialogue_done());
  say_line(#HinaOutside);
  wait_until(dialogue_done());
  say_line(#MitsukiWhat);
  wait_until(dialogue_done());
  say_line(#HinaThings);
  wait_until(dialogue_done());
  say_line(#MitsukiCheese);
  wait_until(dialogue_done());
  say_line(#HinaNotCheese);
  wait_until(dialogue_done());
  sleep(50);
  say_line(#MitsukiWindow);
  wait_until(dialogue_done());
  say_line(#MitsukiStaring);
  wait_until(dialogue_done());
  say_line(#HinaCalm);
  wait_until(dialogue_done());
  say_line(#MitsukiWhere);
  wait_until(dialogue_done());
  say_line(#HinaPark);
  wait_until(dialogue_done());
  say_line(#MitsukiStay);
  wait_until(dialogue_done());
  say_line(#MitsukiTiring);
  wait_until(dialogue_done());
  say_line(#HinaComfort);
  wait_until(dialogue_done());
  say_line(#MitsukiOnMyWay);
  wait_until(dialogue_done());
  sleep(30);
  // She puts the phone away, sits on the edge of the bed a moment, gets up.
  owner_play_anim(#wake);
  sleep(50);
  owner_lock(0);
  owner_play_anim(#idle);
  sleep(20);
  if !Armed {
    say_line(#MitsukiKatana);
    wait_until(dialogue_done());
  }
  gotostate Awake;
}

state Awake {
  event tick() {
    if !Armed && has_item(#Katana) {
      Armed = true;
      say_line(#MitsukiReady);
    }
    if !Armed && !Warned && player_x() >= fix(DOOR - 14) {
      Warned = true;
      say_line(#MitsukiNotWithout);
    }
    if Warned && player_x() < fix(DOOR - 60) { Warned = false; }
  }
}
