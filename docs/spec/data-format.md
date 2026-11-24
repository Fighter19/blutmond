# Records

Version 0.1, draft. Structures and records: data a game declares for itself (weapons, spells,
enemy kinds), edited as text, cooked into the pack and read by scripts. The design and what
is still to come are in [../plans/structures.md](../plans/structures.md).

## 1. Structures

Declared in Mondskript ([mondskript.md](mondskript.md) §11, "Structures"): field types,
layout, base structures and defaults are specified there. The engine knows no structure by
name.

## 2. Record files (`.bmdata`)

Text, UTF-8, one table per file.

```
# enemies.bmdata
table Enemy

record Bat {
  damage = 1
  speed = 0.75
}

record FastBat : Bat {        # starts as a copy of Bat
  damage = 2
  speed = 1.25
}
```

- The first word is `table`, followed by the name of a structure the scripts declare. Two
  files MAY name the same table; their records are put together.
- `record Name { ... }` or `record Name : Other { ... }`. Without `: Other` a record starts
  with the structure's defaults; with it, as a copy of `Other`, which MUST be a record of the
  same table (in any file, before or after).
- Inside: `field = value`, one per line or several on a line. A field of an inner structure is
  written `bonus.strength`, one element of an array `combo[1]`; an array without an element
  sets every element. Fields that are not named keep their value.
- Values, by the field's type: numbers (`-5`, `0x1F`, `1.25` for `fix`), checked against the
  type's range; `true`/`false`; `#Name` for `name`; `"text"` for `text`; `"name"` or `#name`
  for `sprite` and `sound`; a class name for `attach`; a record's name (`FlameBrand` or
  `#FlameBrand`) for `record<S>`, which MUST be a record of `S`; an enum value (`Fire` or
  `Element.Fire`).
- Comments: `#` followed by anything but a letter or `_`, and `//`, to the end of the line.
- Names are unique in the whole project. A record is identified by the FNV-1a hash (32 bits)
  of its name, its **key**; two names with the same key, or a name whose key is 0, are an
  error.

The cooker reports every problem with file and line, and per structure the number of records
and their size.

## 3. The `DTAB` chunk

One per structure of the script module, also for one without records (its defaults are
needed). Global group, category `objects`. Read in place.

```
u16 structure            string id of the structure's name
u16 record_count
u16 record_size          bytes, a multiple of 4, at most 64
u16 field_count          at most 32
u32 layout_hash          CRC-32, see below
fields   { u16 name; u8 type; u8 count; u16 offset; u16 detail; } [field_count]
keys     { u32 key; u16 name; u16 0; } [record_count]       ascending by key
defaults u8[record_size]                                     every field at its default
records  u8[record_count * record_size]                      in the order of the keys
roles    (only for a structure with roles, section 6)
         u8[4] "ROLE"
         u8  roles           BM_DATA_ROLE_*: 1 item, 2 equip, 4 usable, 8 stat_source
         u8  slot            the equipment slot; 0xFF: each record's, from the field below
         u16 slot_field      offset of that field (one byte, a value of enum Slot)
         u8  stat_count      at most 16
         u8  0; u16 0
         { u16 offset; u8 type; u8 0; } [stat_count]   where stat i is in a record
```

- `type` is a `BmDataType` (`src/data/data_types.h`), `count` the number of elements (1 for a
  single value), `offset` from the start of a record. `detail` is the string id of the enum,
  inner structure or `record<S>` target, else 0xFFFF. The field list is that of the structure
  with its base's fields first; fields of inner structures are not listed separately.
- Values inside a record are stored as [mondskript.md](mondskript.md) §11 says: little
  endian, `text`, `name`, `sprite`, `sound` and `attach` as string ids (0xFFFF none),
  `record<S>` as the record's key (0 none), enums as one byte.
- `layout_hash`: CRC-32 (pack-format.md) over, per field in order, its name, a zero byte, the
  type, the count, the offset (u16), the detail's name and a zero byte. Two packs whose
  structure has the same hash lay its records out the same way.

A reader MUST refuse a chunk whose sizes do not add up, whose record size is not a multiple of
4 or above 64, whose fields lie outside a record, or whose keys are not strictly ascending. A
roles block is there if the chunk goes on after the records with "ROLE"; a reader MUST refuse
one whose stats lie outside a record or whose slot is 8 or more (and not 0xFF); a chunk
without it has no roles.

## 4. Reading records

`src/data/data.h`:

- the table of a structure is found by its name's string id among the pack's `DTAB` chunks;
- a record is found by key (binary search);
- a key that is not in the table reads the structure's **defaults**, so a save game made with
  other records, or a record that was removed, never makes a script read something undefined;
- a value is read by offset and type and widened to 32 bits (signed types sign-extended).

For finding a record by key, every table of the pack is opened and checked once (at most 32;
`bmDataPrepare` when a game starts or a state is loaded, else at the first lookup). Finding a
table by its structure looks there too. A table once found is remembered (four at a time) for that pack: the same place, size and
checksum. A pack pushed over the link has another checksum, so after a hot reload of a changed
table the next read sees the new values.
- `equip(slot, record)` finds the table of a record of any structure by its key, and in it the
  first field of type `attach`: the script that comes with the record (first-items.md).

## 5. Records in scripts

```
var(Combat) record<Enemy> Kind = #Bat;

event touch() { hurt_player(Kind.damage); }

for e in records(Enemy) { ... }
```

- `record<S>` is a type of variables, parameters and results; its value is a key (module
  variable type 7). `#Name` where a `record<S>` is expected is that record's key; `==` and
  `!=` compare keys.
- `value.field` reads a field; `.inner.field` reads into an inner structure, `.array[2]` one
  element (the index is a constant); a `record<T>` field can be followed further
  (`held.upgrade.attack`). The result's type follows the field: `bool`; whole numbers of every
  size as `int`; `fix`; `name` for `name`, `sprite`, `sound` and `attach`; `text`; the enum;
  `record<T>`.
- `for x in records(S) { ... }` visits every record of `S` in the order of their keys. Not in
  state code, like every `for`.
- The compiler turns these into three runtime functions (runtime-core.md §8):
  `record_get(table, key, field)` with `field = offset | type << 16`, `record_count(table)`
  and `record_at(table, index)`; `table` is the structure's name as a string id.
- An object property in Tiled named like a `var(Category) record<S>` variable sets it to the
  record with that name (the property's text is hashed).

## 6. Roles

A structure can say what its records are to the engine, after its base:

```
enum Slot { Weapon, Spell, Head, Body, Accessory }        // the equipment slots, at most 8
enum Stat { MaxHealth, MaxMana, Attack, Defence, Luck }   // the stats, at most 16

struct Weapon is Item : item, equip(Weapon), stat_source(stats) { ... StatBlock stats; }
struct Armour is Item : item, equip(slot), stat_source(stats) { Slot slot; StatBlock stats; }
struct Consumable is Item : item, usable { ... }
```

| Role | Needs (checked by the compiler) | What the engine does |
|---|---|---|
| `item` | fields `title` (text), `icon` (sprite), `max_stack` (a whole number) | nothing yet: it marks what can be carried |
| `equip(X)` | an `enum Slot` (at most 8 values); X is a value of it (every record goes into that slot) or a field of type `Slot` (each record says) | `equip(slot, record)` puts a record of the structure only into its slot (false otherwise) |
| `usable` | a field of type `attach` | nothing yet: `use_item` attaches the first `attach` field, as for any record |
| `stat_source(F)` | F (default `stats`) is an inner structure of at most 16 whole numbers (`bool` to `uint`), each a single value | while a record is equipped (an ability: while it is owned), field i of F is added to stat i |
| `ability` | not `equip` as well | a record acts while the player owns it: its stats count (with `stat_source`), and the class its first `attach` field names is attached in one of the 4 ability slots (attachments 8 to 11), in the order the abilities were obtained, with the record in its first `record<S>` variable; one no longer owned is detached, the ones after it move up. After a save game is read and a pack pushed, the slots follow what is owned again |

- Roles are not inherited from a base. A role given twice, an unknown role or one whose needs
  are not met is an error. A stat whose field is named unlike the value of `enum Stat` with
  its number (ignoring case and `_`: `max_health` and `MaxHealth` are alike) is a warning.
- **Equipping**: a record of a structure with roles goes only into the slot its role says; a
  structure without roles (a game that declares none) may go anywhere, as before.
- **Equipment that shows** ([../plans/layered-sprites.md](../plans/layered-sprites.md) §5): an
  equipped record with a field `look_palette` (type `name`, not `#default`) has the player's
  layers drawn in that palette of their sprites where a sprite has it (the last slot that names
  one); a palette a script set with `layer_palette` comes first. Derived like the stat totals.
- **Stat totals**: `stat(i)` is the player's own value plus what attachments add plus what
  equipment and abilities add. The equipment's part is derived (not saved, not in snapshots): worked out
  again whenever something is equipped or taken off, a state or save game is loaded, and a
  pack is pushed (a changed record counts at once). So a save game with armour on counts it
  once after loading.

## History

- 0.2: roles (section 6) and the roles block of `DTAB`.

- 0.1: structures, record files, the `DTAB` chunk, reading records from scripts.
