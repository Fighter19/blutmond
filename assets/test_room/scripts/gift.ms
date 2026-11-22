class Gift extends Actor;

// A charm lying about (the debug lobby): touching it takes it, and wears it in the accessory
// slot (4) if that is free. Which is the property Gives in Tiled; taken stays taken.

var(Gift) record<Armour> Gives;
var save bool Taken;

event spawn() {
  if Taken { hide(); }
}

event touch() {
  if !Taken {
    Taken = true;
    give(Gives, 1);
    if record_int(equipped(4), #price) == 0 { equip(4, Gives); }
    hide();
  }
}
