class Prologue extends Attachment;

// Mitsuki's prologue, in her room at home (mitsuki_home.tmx: the room's `attach`). Come in at
// her bed (a new game started there, or Prologue in the Character menu: the room's
// PlayerStart), she is asleep; the phone rings, she sits up and answers: Hina, in a panic,
// sees monsters in the street. Mitsuki does not believe her, then sees one at her own window
// (it blinks while they talk: home_prop.ms), stays calm about it, and agrees to meet her at
// the park. She gets up (the thing at the window ducks away: the story flag MonsterGone),
// goes to the switch by the foot of the bed and flips it: the ceiling lamp comes on, the room
// goes from the moon's cool light to a warm, bright one (set_light), and the cottage's window
// in the street is lit from now on (the story flag MitsukiLight). Then the player has her:
// the door out (to the street, the bench by the lamp) stays shut until she has taken her
// katana from its rack (the door's `needs`); she says so if she tries.
//
// Up in front of things: the switch flips the light; the rabbit on the dresser gets a word
// (two, by turns); the bed: she goes back to sleep, and that is one way for the story to end.
//
// Her animations here (sleep, ring, call, wake, flip) are drawn on the bed, from where she
// stands in front of it (assets/mitsuki/mitsuki.py), facing right: the prologue makes Mitsuki
// the player if another character is.

const int BED = 46;      // the room's PlayerStart: in front of the bed
const int SWITCH = 58;   // the light switch (a HomeProp)
const int RABBIT = 115;  // the plush rabbit on the dresser
const int DOOR = 224;    // the door's left edge

var int Rings;
var bool Warned;         // she said she needs her katana (again once she walked away)
var bool Armed;          // she has it (and said so)
var int Glow;            // the lamp: 0 off (the moon) to 16 on
var bool UpBefore;       // Up was held a frame ago (things take a fresh press)
var int Looks;           // how often she looked at the rabbit

// The room's light for Glow g: from the moon's (the room's Light: #8ca0e6, 0.9, ambient 0.3)
// to the lamp's (#ffc882, 2.0, ambient 0.7). A lit colour is at most as drawn: the room is
// drawn as the lamp shows it.
function lamp(int g) {
  var int r = 140 + (255 - 140) * g / 16;
  var int gr = 160 + (200 - 160) * g / 16;
  var int b = 230 + (130 - 230) * g / 16;
  set_light(r * 65536 + gr * 256 + b, 90 + 110 * g / 16, 30 + 40 * g / 16);
}

defaultproperties {
  start_state = Arrive;
}

event attached() {
  Rings = 0;
  Warned = false;
  Armed = has_item(#Katana);
  UpBefore = true;
  Looks = 0;
  Glow = 0;
  if flag(#MitsukiLight) { Glow = 16; }
}

// Every frame: the lamp as it is (a room just entered or a state loaded brings the room's own
// light back: set again; nothing when unchanged).
event tick() {
  lamp(Glow);
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
  // She switched the light off before going to bed; the thing is at the window.
  set_flag(#MitsukiLight, false);
  set_flag(#MonsterGone, false);
  Glow = 0;
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
  // She puts the phone away and sits on the edge of the bed: the thing outside ducks away.
  owner_play_anim(#wake);
  set_flag(#MonsterGone, true);
  sleep(50);
  owner_play_anim(#idle);
  sleep(20);
  // Over to the switch, and the light on.
  owner_turn(1);
  owner_play_anim(#run);
  lock_move(GroundMove.Keep, AirMove.Keep);
  while player_x() < fix(SWITCH - 6) {
    owner_velocity(1.0, 0.0);
    sleep(1);
  }
  owner_velocity(0.0, 0.0);
  owner_play_anim(#idle);
  sleep(10);
  owner_play_anim(#flip);
  sleep(12);
  set_flag(#MitsukiLight, true);
  play_sound(#light_switch);
  while Glow < 16 {
    Glow += 1;
    sleep(2);
  }
  sleep(16);
  owner_play_anim(#idle);
  owner_lock(0);
  sleep(20);
  if !Armed {
    say_line(#MitsukiKatana);
    wait_until(dialogue_done());
  }
  gotostate Awake;
}

state Awake {
  event tick() {
    lamp(Glow);
    var bool up = held(#Up) && !UpBefore && on_ground() && dialogue_done();
    UpBefore = held(#Up);
    if up && player_x() >= fix(SWITCH - 10) && player_x() <= fix(SWITCH + 8) {
      // The switch: the light on or off.
      set_flag(#MitsukiLight, !flag(#MitsukiLight));
      play_sound(#light_switch);
      Glow = 0;
      if flag(#MitsukiLight) { Glow = 16; }
    } else if up && player_x() >= fix(14) && player_x() < fix(SWITCH - 10) {
      gotostate BackToBed;
    } else if up && player_x() >= fix(RABBIT - 9) && player_x() <= fix(RABBIT + 9) {
      // The rabbit: a word for it, by turns.
      if Looks % 2 == 0 { say_line(#MitsukiRabbit); }
      else { say_line(#MitsukiRabbitMoved); }
      Looks += 1;
    }
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

// Up at the bed: monsters can wait. She lies down; the phone rings once more; she sleeps on.
// The end? Then she gets up again (the story is not over).
state BackToBed {
begin:
  owner_lock(65535);
  lock_move(GroundMove.Keep, AirMove.Keep);
  if player_x() < fix(BED) {
    owner_turn(1);
    owner_play_anim(#run);
    while player_x() < fix(BED) {
      owner_velocity(1.0, 0.0);
      sleep(1);
    }
  } else {
    owner_turn(-1);
    owner_play_anim(#run);
    while player_x() > fix(BED) {
      owner_velocity(-1.0, 0.0);
      sleep(1);
    }
  }
  owner_velocity(0.0, 0.0);
  owner_turn(1);
  owner_play_anim(#wake);
  sleep(30);
  say_line(#MitsukiTomorrow);
  wait_until(dialogue_done());
  owner_play_anim(#sleep);
  sleep(120);
  owner_play_anim(#ring);
  play_sound(#phone_ring);
  sleep(40);
  say_line(#HinaBench);
  wait_until(dialogue_done());
  owner_play_anim(#sleep);
  sleep(30);
  say_line(#MitsukiZzz);
  wait_until(dialogue_done());
  sleep(30);
  ui_frame(10, 8, 10, 3);
  ui_label(12, 9, "The end?");
  ui_show(true);
  sleep(180);
  ui_clear(10, 8, 10, 3);
  // ... not quite.
  owner_play_anim(#wake);
  sleep(40);
  owner_play_anim(#idle);
  owner_lock(0);
  UpBefore = true;
  gotostate Awake;
}
