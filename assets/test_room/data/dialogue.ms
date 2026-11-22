// Who speaks in a dialogue and what they say (docs/plans/dialogue.md §2): speakers and lines
// are records, so both are edited in the editor's Data tab like the items. A script shows a
// line with say_line(#Name); say_as(#Speaker, "...") takes a line written in the script.

struct Speaker {
  text   name;
  sprite portrait;   // 32 x 32, its anchor at the top left: shown at the dialogue box's left
  sound  voice;      // a short sound effect, heard with every second letter as a line appears
}

struct Line {
  record<Speaker> speaker;
  text            says;   // up to two rows of the box: about 44 letters next to a portrait
}
