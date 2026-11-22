class SystemMenu extends Screen;

// System: the language of the texts (an option kept apart from the save game). A chooses,
// B goes back. Every open menu shows itself again in the new language.

var int Pick;

event opened() {
  Pick = language();
  ui_frame(8, 3, 14, 4 + language_count());
  ui_label(10, 4, "Language");
  for i in 0..language_count() { ui_label(11, 6 + i, language_name(i)); }
  cursor();
}

function cursor() {
  for i in 0..language_count() { ui_label(10, 6 + i, " "); }
  ui_label(10, 6 + Pick, ">");
}

event press(name b) {
  if b == #Up { Pick = (Pick + language_count() - 1) % language_count(); }
  if b == #Down { Pick = (Pick + 1) % language_count(); }
  if b == #A { set_language(Pick); }
  if b == #B {
    close_screen();
    return;
  }
  cursor();
}
