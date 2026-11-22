class UseReturn extends Attachment;

// The Return Feather (first-items.md §6): back to the last save point (the start until one is
// reached); the menu closes. One is used up.

var record<Consumable> Item;

event attached() {
  take(Item, 1);
  return_to_save();
}
