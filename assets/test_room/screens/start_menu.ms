class StartMenu extends Screen;

// The Start menu (docs/plans/ui.md §1): the player's status, and the pages Equipment, Items,
// Commands and System. Up and Down choose, A opens a page, B or Start close the menu. The engine opens the
// class of this name when Start is pressed.

var int Choice;

event opened() {
  ui_frame(1, 1, 12, 12);
  ui_label(3, 2, "Equipment");
  ui_label(3, 4, "Items");
  ui_label(3, 6, "Commands");
  ui_label(3, 8, "System");
  ui_label(3, 10, "Close");
  cursor();
  status();
}

function cursor() {
  for i in 0..5 { ui_label(2, 2 + i * 2, " "); }
  ui_label(2, 2 + Choice * 2, ">");
}

function status() {
  ui_frame(14, 1, 15, 10);
  ui_label(15, 2, "Health");
  ui_number(21, 2, health, 3);
  ui_label(24, 2, "/");
  ui_number(25, 2, max_health(), 3);
  ui_gauge(15, 3, 13, health * 100 / max_health());
  ui_label(15, 5, "Attack");
  ui_number(25, 5, record_int(equipped(0), #attack) + stat(Stat.Attack), 3);
  ui_label(15, 6, "Defence");
  ui_number(25, 6, stat(Stat.Defence), 3);
  ui_label(15, 8, record_text(equipped(0), #title));
}

event press(name b) {
  if b == #Up {
    Choice = (Choice + 4) % 5;
    cursor();
  }
  if b == #Down {
    Choice = (Choice + 1) % 5;
    cursor();
  }
  if b == #A {
    if Choice == 0 { open_screen(#EquipmentMenu); }
    if Choice == 1 { open_screen(#ItemsMenu); }
    if Choice == 2 { open_screen(#CommandsMenu); }
    if Choice == 3 { open_screen(#SystemMenu); }
    if Choice == 4 { close_screen(); }
  }
  if b == #B || b == #Start { close_screen(); }
}
