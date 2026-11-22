class CommandsMenu extends Screen;

// Commands (src/world/commands.h): the special moves the player has now, those of the weapon
// in hand and those of the charms worn, with their input and mana; below, what the chosen one
// does. Up and Down choose, B goes back.

var int Pick;
var int Count;

event opened() {
  ui_frame(1, 1, 28, 13);
  ui_frame(1, 14, 28, 5);
  draw();
}

function bool has(record<Command> c) {
  var record<Weapon> weapon = equipped(0);
  if c.weapon == weapon && weapon.attack != 0 { return true; }
  for s in 1..8 {
    var record<Armour> worn = equipped(s);
    if c.charm == worn && worn.price != 0 { return true; }
  }
  return false;
}

function record<Command> at(int n) {
  var int k = 0;
  var record<Command> none;
  for c in records(Command) {
    if has(c) {
      if k == n { return c; }
      k += 1;
    }
  }
  return none;
}

function draw() {
  for y in 2..13 { ui_label(2, y, "                          "); }
  for y in 15..18 { ui_label(2, y, "                          "); }
  ui_label(2, 2, record_text(equipped(0), #title));
  Count = 0;
  for c in records(Command) {
    if has(c) && Count < 5 {
      ui_label(4, 4 + Count * 2, c.title);
      ui_label(19, 4 + Count * 2, "MP");
      ui_number(22, 4 + Count * 2, c.cost, 2);
      ui_label(5, 5 + Count * 2, c.keys);
      Count += 1;
    }
  }
  if Count == 0 {
    ui_label(4, 4, "None");
    return;
  }
  if Pick >= Count { Pick = Count - 1; }
  ui_label(3, 4 + Pick * 2, ">");
  var record<Command> c = at(Pick);
  ui_text(2, 15, 26, c.description);
}

event press(name b) {
  if b == #Up && Count > 0 {
    Pick = (Pick + Count - 1) % Count;
    draw();
  }
  if b == #Down && Count > 0 {
    Pick = (Pick + 1) % Count;
    draw();
  }
  if b == #B { close_screen(); }
}
