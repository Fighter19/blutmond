class Cottage extends Attachment;

// The rural street's cottage is Mitsuki's home (the street's `attach`): its window, drawn lit
// in the street's tiles, is lit while the light in her room is on (the story flag
// MitsukiLight: the switch by her bed, prologue.ms) and dark while it is off. The window's
// colours are the street's `glow_switch`: set_glow turns them, no sprite covers them.

event tick() {
  if flag(#MitsukiLight) { set_glow(100); }
  else { set_glow(18); }
}
