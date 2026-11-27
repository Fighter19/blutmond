class CharacterMenu extends Screen;

// Character (from the System menu): the playable characters (data/characters.bmdata) whose
// figure the pack has, the player's own first, the one playing marked. A makes the chosen one
// the player at once (set_character: an option, kept apart from the save game), B goes back.
// Below them, when a character has a prologue (the room its story starts in), the row
// Prologue: it takes the player there (the one playing, if it has one; else the first that
// has), and closes the menus. The room's own script does the rest (Mitsuki's: prologue.ms).

var int Pick;
var int Count;
var record<Character> Story;   // whose prologue the row starts (none: no row)

event opened() {
  Count = 0;
  var record<Character> none;
  Story = none;
  for c in records(Character) {
    if has_character(c.figure) {
      Count += 1;
      if c.prologue != #none && (Story.prologue == #none || c.figure == character()) { Story = c; }
    }
  }
  Pick = 0;
  var int rows = Count;
  if Story.prologue != #none { rows = Count + 2; }
  ui_frame(8, 5, 15, 4 + rows);
  ui_label(10, 6, "Character");
  if Story.prologue != #none { ui_label(11, 9 + Count, "Prologue"); }
  draw();
}

// Rows: the characters, then (below a gap) Prologue.
function int rows() {
  if Story.prologue != #none { return Count + 1; }
  return Count;
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
  if Story.prologue != #none {
    ui_label(10, 9 + Count, " ");
    if Pick == Count { ui_label(10, 9 + Count, ">"); }
  }
}

event press(name b) {
  var int n = rows();
  if b == #Up { Pick = (Pick + n - 1) % n; }
  if b == #Down { Pick = (Pick + 1) % n; }
  if b == #A && Pick == Count {
    // Out of the menus (this one, System, Start) and into the story.
    if teleport(Story.prologue, Story.prologue_x, Story.prologue_y) {
      close_screen();
      close_screen();
      close_screen();
      return;
    }
  }
  if b == #A && Pick < Count { set_character(at(Pick).figure); }
  if b == #B {
    close_screen();
    return;
  }
  draw();
}
