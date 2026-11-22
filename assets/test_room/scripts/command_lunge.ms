class CommandLunge extends Attachment;

// Lunge (data/commands.bmdata): a dash forward with the blade ahead, striking for twice the
// weapon's attack while it moves.

var record<Command> Item;

event attached() {
  owner_lock(20);
  owner_play_anim(#slash);
  play_sound(#jump);
  impulse(fix(facing) * 5.0, 0.0);
  hit_box(24, 12, 2 * record_int(equipped(0), #attack) + stat(Stat.Attack), Element.None);
}
