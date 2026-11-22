class ItemsMenu extends Screen;

// Items (first-items.md §6): the consumables the player carries, how many, what the chosen one
// does. A uses it (through its behaviour: a potion heals, the feather returns to the save
// point), L and R put it on the quick slots (L and Select use them in the game), B goes back.

var int Pick;
var int Count;

event opened() {
  ui_frame(1, 1, 28, 13);
  ui_frame(1, 14, 28, 5);
  draw();
}

function record<Consumable> at(int n) {
  var int k = 0;
  var record<Consumable> none;
  for c in records(Consumable) {
    if item_count(c) > 0 {
      if k == n { return c; }
      k += 1;
    }
  }
  return none;
}

function draw() {
  for y in 2..13 { ui_label(2, y, "                          "); }
  Count = 0;
  for c in records(Consumable) {
    if item_count(c) > 0 && Count < 10 {
      ui_label(4, 2 + Count, c.title);
      ui_label(22, 2 + Count, "x");
      ui_number(23, 2 + Count, item_count(c), 3);
      Count += 1;
    }
  }
  for y in 15..18 { ui_label(2, y, "                          "); }
  quick();
  status();
  if Count == 0 {
    ui_label(4, 2, "Nothing");
    return;
  }
  if Pick >= Count { Pick = Count - 1; }
  ui_label(3, 2 + Pick, ">");
  var record<Consumable> c = at(Pick);
  ui_text(2, 15, 26, c.description);
}

// The quick slots, at the bottom of the list.
function quick() {
  ui_label(2, 12, "                          ");
  var record<Consumable> first = quick_slot(0);
  var record<Consumable> second = quick_slot(1);
  ui_label(2, 12, "L");
  ui_label(4, 12, first.title);
  ui_label(15, 12, "Sel");
  ui_label(19, 12, second.title);
}

function status() {
  ui_label(2, 18, "Health");
  ui_number(9, 18, player_health(), 3);
  ui_label(14, 18, "Mana");
  ui_number(19, 18, mana(), 3);
}

event press(name b) {
  if b == #Up && Count > 0 { Pick = (Pick + Count - 1) % Count; }
  if b == #Down && Count > 0 { Pick = (Pick + 1) % Count; }
  if b == #A && Count > 0 { use_item(at(Pick)); }
  if b == #L && Count > 0 { set_quick_slot(0, at(Pick)); }
  if b == #R && Count > 0 { set_quick_slot(1, at(Pick)); }
  if b == #B {
    close_screen();
    return;
  }
  draw();
}
