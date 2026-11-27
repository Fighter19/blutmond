class Hands extends Actor;

// The close-up of Mitsuki's and Hina's hands in Hina's lap (hands.bmsprite, hands.py): shown
// over the bench while the story flag ParkHands is on (park.ms).

defaultproperties {
  sprite = "hands";
}

event spawn() {
  hide();
}

event tick() {
  if flag(#ParkHands) { show(); }
  else { hide(); }
}
