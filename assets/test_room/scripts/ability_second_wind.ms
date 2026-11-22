class AbilitySecondWind extends Attachment;

// Second Wind (first-items.md §5): a blow that would end the player leaves them standing with 1
// health; then it needs 30 seconds to come back. Attached while the player owns the record.

var record<Ability> Item;
var int Wait;     // frames until it can save the player again
var int Saved;

event damage_in(int amount) {
  if Wait == 0 && amount >= health {
    set_damage(health - 1);
    Wait = 1800;
    Saved += 1;
  }
}

event tick() {
  if Wait > 0 { Wait -= 1; }
}
