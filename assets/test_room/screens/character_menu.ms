class CharacterMenu extends Screen;

// Character (from the System menu): the playable characters (data/characters.bmdata) whose
// figure the pack has, the player's own first, the one playing marked. A makes the chosen one
// the player at once (set_character: an option, kept apart from the save game), B goes back.

var int Pick;
var int Count;

event opened() {
  Count = 0;
  for c in records(Character) {
    if has_character(c.figure) { Count += 1; }
  }
  Pick = 0;
  ui_frame(8, 5, 15, 4 + Count);
  ui_label(10, 6, "Character");
  draw();
}

// The n-th character the pack has (0: none): the player's own (#player) first, then the others.
function record<Character> at(int n) {
  var int k = 0;
  var record<Character> none;
  for pass in 0..2 {
    for c in records(Character) {
      var bool own = c.figure == #player;
      if has_character(c.figure) && ((pass == 0 && own) || (pass == 1 && !own)) {
        if k == n { return c; }
        k += 1;
      }
    }
  }
  return none;
}

function draw() {
  for i in 0..Count {
    var record<Character> c = at(i);
    ui_clear(10, 8 + i, 12, 1);
    if i == Pick { ui_label(10, 8 + i, ">"); }
    ui_label(11, 8 + i, c.name);
    if c.figure == character() { ui_label(21, 8 + i, "*"); }
  }
}

event press(name b) {
  if b == #Up { Pick = (Pick + Count - 1) % Count; }
  if b == #Down { Pick = (Pick + 1) % Count; }
  if b == #A { set_character(at(Pick).figure); }
  if b == #B {
    close_screen();
    return;
  }
  draw();
}
