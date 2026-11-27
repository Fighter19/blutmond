// The playable characters (set_character, game.h): who can be chosen in the System menu. Each
// names a figure of the pack, "player" or "player_<name>" (sprite-format.md "Figures").

struct Character {
  text            name;      // as the menu shows it
  name            figure;    // the figure the player is drawn with: #player or #player_<name>
  record<Speaker> speaker;   // who says the player's lines while it is the player: lines of
                             // the player's own character's speaker are shown with this one
}
