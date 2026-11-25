class Bench extends Attachment;

// A bench to rest on (the rural street's, attached by its property `attach = Threat, Bench`):
// Up while standing at it sits the player down (the player's animation `sit`, held by a lock),
// and resting restores health and mana and saves the game (the bench the place to start again
// from). Not while an enemy is within SAFE pixels: the knight
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

// The lock ends: the sitting ends with it, and the player's own animations go on (walking,
// running). (An animation played without a lock would be held until it ends: idle never does.)
function stand() {
  Sitting = 0;
  owner_lock(0);
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
      save_game();
      play_sound(#pickup);
      Health = player_health();
    }
  } else {
    // (Only while sitting: what gets the player up.)
    var int health = player_health();
    if held(#Left) || held(#Right) || held(#Down) || held(#Jump) || !on_ground() || health < Health { stand(); }
    Health = health;
  }
  UpBefore = 0;
  if up { UpBefore = 1; }
}
