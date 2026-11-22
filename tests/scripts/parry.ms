class Parry extends Attachment;

// A parry (docs/plans/hitboxes.md §5): B makes a short rectangle ahead that sends projectiles
// back instead of striking them. Cooked only into the test pack with wind.

event press(name button) {
  if button == #Attack {
    hit_option(#Reflect, 0);
    hit_box(24, 4, 1, Element.None);
  }
}
