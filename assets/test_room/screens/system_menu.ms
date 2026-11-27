class SystemMenu extends Screen;

// System: the language of the texts, when the pack holds a correction the colours (corrected
// for the GBA's own screen or as drawn on a PC), and with more than one playable character a
// row that opens the Character menu (character_menu.ms): options kept apart from the save
// game. A chooses (on the colours: switches them), B goes back. Every open menu shows itself
// again in the new language; the colours change at once.

var int Pick;
var int Rows;    // the languages, then below a gap the colours' row and the Character row
var int Chars;   // the Character row (0: none)

event opened() {
  Pick = language();
  Rows = language_count();
  if has_color_correction() { Rows = Rows + 1; }
  if character_count() > 1 {
    Chars = Rows;
    Rows = Rows + 1;
  }
  ui_frame(8, 3, 15, 6 + language_count() + (Rows - language_count()) / 2);
  ui_label(10, 4, "Language");
  for i in 0..language_count() { ui_label(11, 6 + i, language_name(i)); }
  if Chars > 0 { ui_label(11, 7 + Chars, "Character"); }
  cursor();
}

// Row i's place: the languages from row 6, the others below a gap.
function cursor() {
  for i in 0..Rows { ui_label(10, 6 + i + (i + 1) / (language_count() + 1), " "); }
  ui_label(10, 6 + Pick + (Pick + 1) / (language_count() + 1), ">");
  if has_color_correction() {
    ui_clear(11, 7 + language_count(), 11, 1);
    if color_correction() { ui_label(11, 7 + language_count(), "GBA colours"); }
    else { ui_label(11, 7 + language_count(), "PC colours"); }
  }
}

event press(name b) {
  if b == #Up { Pick = (Pick + Rows - 1) % Rows; }
  if b == #Down { Pick = (Pick + 1) % Rows; }
  if b == #A {
    if Pick < language_count() { set_language(Pick); }
    else if Pick == Chars { open_screen(#CharacterMenu); }
    else { set_color_correction(!color_correction()); }
  }
  if b == #B {
    close_screen();
    return;
  }
  cursor();
}
