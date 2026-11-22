class Capable extends Attachment;

// Capabilities through stats (docs/plans/attachable-scripts.md §3): while attached, the player
// has a jump in the air and slides down walls. It adds them and never takes them away: the
// runtime does when it is detached. Cooked only into the test pack with wind.

event attached() {
  add_stat(Stat.AirJumps, 1);
  add_stat(Stat.WallSlide, 1);
}
