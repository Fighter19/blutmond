class CommandFlurry extends Attachment;

// Flurry (data/commands.bmdata): three quick stabs, one every 8 frames.

var record<Command> Item;
var int Frame;

event attached() {
  owner_lock(28);
}

event tick() {
  if Frame % 8 == 0 && Frame < 24 {
    owner_play_anim(#slash);
    play_sound(#jump);
    hit_box(16, 3, record_int(equipped(0), #attack) + stat(Stat.Attack), Element.None);
  }
  Frame += 1;
}
