class CommandsMenu extends Screen;

// Commands (src/world/commands.h): the special moves the player has now, those of the weapon
// in hand and those of the charms worn, with their input and mana; below, what the chosen one
// does. Up and Down choose, B goes back.
//
// The list is found once, when the page opens (every record against what is equipped is too
// much work for the script of one button press), and kept in Move0 to Move4.

var int Pick;
var int Count;
var record<Command> Move0;
var record<Command> Move1;
var record<Command> Move2;
var record<Command> Move3;
var record<Command> Move4;

event opened() {
  ui_frame(1, 1, 28, 13);
  ui_frame(1, 14, 28, 5);
  find();
  list();
  chosen();
}

// Whether the player has the command now: its weapon in hand, or its charm worn.
function bool has(record<Command> c) {
  var record<Weapon> weapon = equipped(0);
  var record<Weapon> noWeapon;
  if c.weapon != noWeapon { return c.weapon == weapon; }
  var record<Armour> noCharm;
  if c.charm == noCharm { return false; }
  for s in 1..8 {
    var record<Armour> worn = equipped(s);
    if c.charm == worn { return true; }
  }
  return false;
}

function find() {
  Count = 0;
  for c in records(Command) {
    if Count < 5 && has(c) {
      if Count == 0 { Move0 = c; }
      if Count == 1 { Move1 = c; }
      if Count == 2 { Move2 = c; }
      if Count == 3 { Move3 = c; }
      if Count == 4 { Move4 = c; }
      Count += 1;
    }
  }
}

function record<Command> at(int n) {
  if n == 1 { return Move1; }
  if n == 2 { return Move2; }
  if n == 3 { return Move3; }
  if n == 4 { return Move4; }
  return Move0;
}

function list() {
  for y in 2..13 { ui_label(2, y, "                          "); }
  ui_label(2, 2, record_text(equipped(0), #title));
  if Count == 0 {
    ui_label(4, 4, "None");
    return;
  }
  for i in 0..Count {
    var record<Command> c = at(i);
    ui_label(4, 4 + i * 2, c.title);
    ui_label(19, 4 + i * 2, "MP");
    ui_number(22, 4 + i * 2, c.cost, 2);
    ui_label(5, 5 + i * 2, c.keys);
  }
}

// The cursor and what the chosen move does.
function chosen() {
  for i in 0..5 { ui_label(3, 4 + i * 2, " "); }
  for y in 15..18 { ui_label(2, y, "                          "); }
  if Count == 0 { return; }
  if Pick >= Count { Pick = Count - 1; }
  ui_label(3, 4 + Pick * 2, ">");
  var record<Command> c = at(Pick);
  ui_text(2, 15, 26, c.description);
}

event press(name b) {
  if b == #Up && Count > 0 {
    Pick = (Pick + Count - 1) % Count;
    chosen();
  }
  if b == #Down && Count > 0 {
    Pick = (Pick + 1) % Count;
    chosen();
  }
  if b == #B { close_screen(); }
}
