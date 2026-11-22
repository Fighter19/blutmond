class UseRestore extends Attachment;

// Using a potion or a mana vial (first-items.md §6): attached for a frame by use_item, with
// Item set to the record. It restores what the record says and takes one, unless there is
// nothing to restore. (Cures come with the status effects, step 7.)

var record<Consumable> Item;

event attached() {
  var bool helps = false;
  if Item.health > 0 && player_health() < max_health() { helps = true; }
  if Item.mana > 0 && mana() < max_mana() { helps = true; }
  if !helps { return; }
  heal(Item.health);
  restore_mana(Item.mana);
  take(Item, 1);
  play_sound(#jump);
}
