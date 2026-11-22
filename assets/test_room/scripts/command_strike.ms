class CommandStrike extends Attachment;

// A weapon's special move (data/commands.bmdata, the Command structure in data/items.ms): a push
// at the start, then after `startup` frames `hits` strikes `every` frames apart, `reach` pixels
// ahead (negative: on both sides), each `power` percent of the weapon's attack and the Attack
// stat. The player is held for the whole of it (in the air the jump goes on).

var record<Command> Item;
var int Frame;

event attached() {
  var int every = Item.every;
  if every < 1 { every = 1; }
  owner_lock(Item.startup + (Item.hits - 1) * every + 10);
  var record<Weapon> weapon = equipped(0);
  lock_move(weapon.ground_move, weapon.air_move);   // as the weapon's own swing
  if Item.dash != 0.0 || Item.hop != 0.0 { impulse(fix(facing) * Item.dash, -Item.hop); }
  play_sound(#jump);
}

event tick() {
  var int every = Item.every;
  if every < 1 { every = 1; }
  var int t = Frame - Item.startup;
  if t >= 0 && t % every == 0 && t / every < Item.hits {
    owner_play_anim(Item.anim);
    hit_box(Item.reach, 3, (record_int(equipped(0), #attack) + stat(Stat.Attack)) * Item.power / 100, Item.element);
  }
  Frame += 1;
}
