class EquipmentMenu extends Screen;

// Equipment (docs/plans/first-items.md §2, ui.md §1): the slots on the left, what the player
// owns that fits the chosen slot on the right, and below what a choice would change. A chooses
// a slot, then equips; B goes back. Weapons go in equipment slot 0, helmets in 2, body armour
// in 3, an accessory in 4 (slot 1 is the spell's).

var int Slot;          // 0 weapon, 1 head, 2 body, 3 accessory
var bool Choosing;     // in the list on the right
var int Pick;          // there: 0 is "nothing", then what fits in the order of the records
var int Count;

event opened() {
  ui_frame(1, 1, 13, 13);
  ui_frame(14, 1, 15, 13);
  ui_frame(1, 14, 28, 5);
  ui_label(3, 2, "Weapon");
  ui_label(3, 5, "Head");
  ui_label(3, 8, "Body");
  ui_label(3, 11, "Accessory");
  slots();
  list();
  preview();
}

function int equipSlot(int s) {
  if s == 0 { return 0; }
  return s + 1;
}

function bool fits(record<Armour> a, int s) {
  if s == 1 { return a.slot == EquipSlot.Head; }
  if s == 2 { return a.slot == EquipSlot.Body; }
  return a.slot == EquipSlot.Accessory;
}

// The weapon or armour at place n of the list (n from 1).
function record<Weapon> weaponAt(int n) {
  var int k = 0;
  var record<Weapon> none;
  for w in records(Weapon) {
    if item_count(w) > 0 {
      k += 1;
      if k == n { return w; }
    }
  }
  return none;
}

function record<Armour> armourAt(int n) {
  var int k = 0;
  var record<Armour> none;
  for a in records(Armour) {
    if item_count(a) > 0 && fits(a, Slot) {
      k += 1;
      if k == n { return a; }
    }
  }
  return none;
}

function slots() {
  for s in 0..4 {
    ui_label(3, 3 + s * 3, "          ");
    ui_label(3, 3 + s * 3, record_text(equipped(equipSlot(s)), #title));
  }
  slotCursor();
}

function slotCursor() {
  for s in 0..4 { ui_label(2, 2 + s * 3, " "); }
  if !Choosing { ui_label(2, 2 + Slot * 3, ">"); }
}

function listCursor() {
  for y in 2..13 { ui_label(15, y, " "); }
  if Choosing { ui_label(15, 2 + Pick, ">"); }
}

function list() {
  for y in 2..13 { ui_label(15, y, "             "); }
  ui_label(16, 2, "-");
  Count = 1;
  if Slot == 0 {
    for w in records(Weapon) {
      if item_count(w) > 0 && Count < 11 {
        ui_label(16, 2 + Count, w.title);
        Count += 1;
      }
    }
  } else {
    for a in records(Armour) {
      if item_count(a) > 0 && fits(a, Slot) && Count < 11 {
        ui_label(16, 2 + Count, a.title);
        Count += 1;
      }
    }
  }
  listCursor();
}

// What the choice would change: attack and defence now, and with it.
function preview() {
  for y in 15..18 { ui_label(2, y, "                          "); }
  var int attack = record_int(equipped(0), #attack) + stat(Stat.Attack);
  var int defence = stat(Stat.Defence);
  var int newAttack = attack;
  var int newDefence = defence;
  if Choosing && Slot == 0 {
    var record<Weapon> now = equipped(0);
    var record<Weapon> w = weaponAt(Pick);
    newAttack = w.attack + w.stats.attack + stat(Stat.Attack) - now.stats.attack;
    newDefence = defence + w.stats.defence - now.stats.defence;
  }
  if Choosing && Slot > 0 {
    var record<Armour> now = equipped(equipSlot(Slot));
    var record<Armour> a = armourAt(Pick);
    newAttack = attack + a.stats.attack - now.stats.attack;
    newDefence = defence + a.stats.defence - now.stats.defence;
  }
  ui_label(2, 15, "Attack");
  ui_number(12, 15, attack, 3);
  ui_label(2, 16, "Defence");
  ui_number(12, 16, defence, 3);
  if Choosing {
    ui_label(16, 15, ">");
    ui_number(18, 15, newAttack, 3);
    ui_label(16, 16, ">");
    ui_number(18, 16, newDefence, 3);
  }
}

// Each press draws again only what it changed (the menu runs as interpreted script on the GBA).
event press(name b) {
  if !Choosing {
    if b == #Up || b == #Down {
      if b == #Up { Slot = (Slot + 3) % 4; }
      else { Slot = (Slot + 1) % 4; }
      slotCursor();
      list();
    }
    if b == #A {
      Choosing = true;
      Pick = 0;
      slotCursor();
      listCursor();
      preview();
    }
    if b == #B { close_screen(); }
    return;
  }
  if b == #Up || b == #Down {
    if b == #Up { Pick = (Pick + Count - 1) % Count; }
    else { Pick = (Pick + 1) % Count; }
    listCursor();
    preview();
  }
  if b == #A {
    if Slot == 0 { equip(0, weaponAt(Pick)); }
    else { equip(equipSlot(Slot), armourAt(Pick)); }
    play_sound(#jump);
    Choosing = false;
    slots();
    listCursor();
    preview();
  }
  if b == #B {
    Choosing = false;
    slotCursor();
    listCursor();
    preview();
  }
}
