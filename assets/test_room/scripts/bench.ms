class Bench extends Attachment;

// A bench to rest on (the rural street's, attached by its property `attach = Threat, Bench`):
// Up while standing at it sits the player down (the player's animation `sit`, held by a lock),
// and resting restores health and mana. Not while an enemy is within SAFE pixels: the knight
// keeps watch by the bench. Left, Right, Down or a jump gets the player up, and so does being
// struck. The bench is drawn in the room's tiles, from LEFT to RIGHT (where the player's feet
// may be to sit on it).

const int LEFT = 322;
const int RIGHT = 368;
const int SAFE = 96;

var int Sitting;   // 1 while the player rests on it
var int UpBefore;  // Up was held a frame ago (sitting down takes a fresh press)
var int Health;    // the player's health a frame ago

event attached() {
  Sitting = 0;
  UpBefore = 1;
  Health = player_health();
}

function stand() {
  Sitting = 0;
  owner_lock(0);
  owner_play_anim(#idle);
}

event tick() {
  var bool up = held(#Up);
  if Sitting == 0 {
    if up && UpBefore == 0 && on_ground() && player_x() >= fix(LEFT) && player_x() <= fix(RIGHT)
        && !handle_valid(nearest_enemy(SAFE, #Any)) {
      Sitting = 1;
      owner_lock(65535);
      owner_play_anim(#sit);
      heal(max_health());
      restore_mana(max_mana());
      play_sound(#pickup);
    }
  } else if held(#Left) || held(#Right) || held(#Down) || held(#Jump) || !on_ground() || player_health() < Health {
    stand();
  }
  UpBefore = 0;
  if up { UpBefore = 1; }
  Health = player_health();
}
