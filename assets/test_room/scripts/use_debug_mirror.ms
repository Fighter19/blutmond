class UseDebugMirror extends Attachment;

// The Debug Mirror (data/consumables.bmdata): to the debug lobby with the wave of a teleport
// (effects.h). It is not used up; the lobby's teleporter leads back to the start.

var record<Consumable> Item;

event attached() {
  teleport(#debug_lobby, 40, 160);
}
