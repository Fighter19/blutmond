# Plan: custom structures and records

Status: plan, nothing built. Milestone M12 in [../PLAN.md](../PLAN.md).

## 1. What this is for

A weapon, a spell, an ability, a consumable and a piece of armour are mostly **data**: a
name, an icon, numbers, and a pointer to what it does. Today the game knows items only as
names (`give_item(#Key)`, four of them, [../spec/runtime-core.md](../spec/runtime-core.md)
§8). This plan adds:

- **structures**: types with named fields, declared by the game, not by the engine;
- **records**: named values of a structure, edited as data (a table in the editor), cooked
  into the pack, read by scripts and by the engine;
- **an inventory** in the game state that refers to records.

The engine does not know what a "weapon" is. It knows structures, records and a few
**roles** a structure can declare (section 5), so a game can add "relics" or "familiars"
without an engine change.

## 2. Structures

Declared in Mondskript, in a file of their own (`.ms`, one or more structures per file):

```
enum Element { None, Fire, Ice, Holy }

struct Weapon is Item {
  int      attack    = 1;
  int      reach     = 16;         // pixels
  int      startup   = 4;          // frames before the hit
  int      recovery  = 10;         // frames after it
  Element  element   = None;
  name     swing     = #Slash;     // animation of the player sprite
  sound    sfx;
  attach   behaviour = WeaponSwing; // a script to attach (attachable-scripts.md)
}
```

- `struct S { ... }` as already reserved in [../spec/mondskript.md](../spec/mondskript.md)
  §2. Field types: `bool`, `byte`/`short`/`int` and their unsigned forms, `fix`, `name`,
  `text`, enums, other structures (inline), fixed arrays, and four **reference types** that
  the cooker checks and resolves: `sprite`, `sound`, `record<S>` (another record) and
  `attach` (a script class that can be attached).
- `struct S is Base` copies the fields of `Base` in front (single level is enough to start:
  `Item` gives every kind `title`, `description`, `icon`, `price`, `max_stack`).
- Field defaults as in the example. A structure has no functions: behaviour lives in the
  attached script.
- Enums come with this plan: `enum E { ... }`, one byte, values in order from 0.

Limits to start with: 32 fields per structure, 16 bytes of padding-free layout rules as for
state (fields laid out in declaration order, aligned to their size), a record at most 64
bytes. The cooker reports the size of every structure.

## 3. Records

Source assets, one file per table, text so that they diff and merge
([../spec/asset-model.md](../spec/asset-model.md)):

```
# weapons.bmdata
table Weapon

record ShortSword {
  title = "Short Sword"
  description = "A plain blade."
  icon = "icons/short_sword"
  attack = 3
  reach = 18
}

record FlameBrand : ShortSword {      # starts as a copy of ShortSword
  title = "Flame Brand"
  attack = 5
  element = Fire
  behaviour = FlameSwing
}
```

- A record has a name that is unique in the whole project (so `#ShortSword` means one
  thing), and values for some fields; the rest are the structure's defaults or those of the
  record it copies from.
- A record is identified in saved state by the 32-bit FNV-1a hash of its name, as items are
  today. Renaming a record needs an entry in the migration table
  ([versioning.md](versioning.md)); the cooker reports two names with the same hash as an
  error.
- Texts are string ids, so they go through the same table as all other text and can be
  translated later.

### In the editor

A **Data** tab: one table per structure, a row per record, a column per field; reference
fields are pickers (sprite, sound, record, script class), enums are drop-downs. New
structure fields appear as new columns with their default. Unused records and records that
refer to something missing are listed in the report. Saving a table cooks and pushes it like
any other asset.

## 4. Cooked form

One chunk per table, global group, category `objects`:

```
'DTAB'  u16 structure;            // string id of the structure's name
        u16 record_count;
        u16 record_size;          // bytes, a multiple of 4
        u16 field_count;
        u32 layout_hash;          // CRC-32 over field names, types and offsets
        { u16 name; u8 type; u8 count; u16 offset; u16 detail; } [field_count]
                                  // detail: enum or structure string id, record<S> target
        { u32 key; u16 name; u16 reserved; } [record_count]   // key: FNV-1a of the name,
                                                              // sorted by key
        u8  records[record_count * record_size];
```

- Records are read in place (from ROM on a cartridge): no copy in RAM, no loading step.
- References are stored resolved: a sprite or sound as its string id (as scripts name them
  now), a record as its key, an `attach` as the class's name id (resolved to a class index
  when the script module is loaded).
- The field list makes the chunk self-describing, so the editor's state view, `bmlink` and
  tests can print a record without the sources, and a script module compiled against another
  layout is refused (`layout_hash` is part of what the module records per structure it
  uses).

## 5. Roles

A structure can declare a role, which is how the engine finds what it needs without knowing
the game's types:

| Role | What the engine does with it |
|---|---|
| `item` | can be in the inventory; needs `title`, `icon`, `max_stack` |
| `equip(slot)` | can be put into the named equipment slot; the slots are declared by the game (`slots { weapon, offhand, head, body, accessory1, accessory2, spell }`) |
| `usable` | can be used from the menu or a quick slot; needs `attach` |
| `stat_source` | its `stats` field (a structure of numbers) is added to the player's totals while equipped (section 7) |

Roles are checked by the compiler: a structure that claims a role without the fields it
needs is an error.

## 6. Scripts and records

```
var record<Weapon> held = #ShortSword;

function int damage() {
  return held.attack + stat(#Strength);
}

for w in records(Weapon) { ... }        // bounded: the table's size is known
```

- `record<S>` is a 4-byte value (the key). Field access compiles to one runtime call with
  the field's offset and type; a key that is not in the table gives the structure's defaults
  and reports through the link in debug builds, so a save from another version cannot crash
  a script.
- New API entries (appended, as the rule in [../spec/mondskript.md](../spec/mondskript.md)
  §11 demands): `record_get(table, key, field)`, `record_count(table)`,
  `record_at(table, index)`, and the inventory calls of section 7.
- Records are constant. What changes at run time (how many potions, which sword is held,
  a weapon's level) is state, not record.

## 7. Inventory and stats in the game state

A new state block `inventory` ([../spec/state.md](../spec/state.md)), all `save link`:

| Field | Size | |
|---|---|---|
| `count` | u8 | used entries |
| `key[64]` | u32 | record |
| `amount[64]` | u8 | stack size |
| `equipped[8]` | u32 | record per slot, 0 none |
| `quick[4]` | u32 | records on quick slots |
| `stat_base[16]` | s16 | the player's own values (level-ups, permanent upgrades) |

About 440 bytes. The save game grows from 183 bytes to about 630, far below the 32 KB of
save memory; a link transfer of the `link` part no longer fits one packet, so `STATE_DATA`
and `STATE_PUT` get an offset like `PPU_SNAPSHOT` has (already listed as open in
mondskript.md §11, "larger limits need state transfer in several packets").

**Stats.** The game declares up to 16 stat names (`stats { MaxHealth, Attack, Defence,
Luck, ... }`). The total of a stat is `stat_base` plus the `stats` of every equipped record
with the role `stat_source` plus what attached scripts add. Totals are `derived`: worked
out again after loading and whenever equipment changes, never saved.

The four names items of today become records of a structure `KeyItem`; `give_item` and
`has_item` keep working on the inventory.

## 8. Steps

1. Enums and structures in the compiler (types, layout, defaults); no records yet. Test:
   layouts and sizes.
2. `.bmdata` parser, cooker, `DTAB` chunk, reader in the runtime (`src/data/`), `record<S>`
   in scripts. Test: a script reads fields on host and GBA with the same result; a wrong
   key gives defaults.
3. The `inventory` block, state transfer in parts, `give_item`/`has_item` on top of it,
   stats. Test: save on the host, load in mGBA; GBA-to-GBA carries the inventory.
4. Roles and equipment slots. Test: equipping changes a stat total; a hot reload of a
   changed record changes the total without a restart.
5. The Data tab in the editor.

Done when: a record changed in the editor (a sword's attack) is pushed to the running game
and the next hit uses it, on PC and GBA; a save game from before the change still loads.

## 9. Open questions

- Per-instance state of items (a sword with its own level, a durability) needs more than
  key and amount. Left out on purpose; if wanted, an entry gets a 16-bit `extra` whose
  meaning the structure declares.
- Whether records may live in room or area groups (shop tables used by one room). Global
  only to start: they are small.
