# Game state

Version 0.1, draft.

## 1. State arena

All mutable game state lives in one **state arena**: a contiguous block (EWRAM on the GBA, with
an optional hot part in IWRAM) that contains no pointers. References are handles (see
[runtime-abi.md](runtime-abi.md)) or offsets. The arena can be copied with `memcpy` and has the
same layout on every target of the same profile.

The layout is generated, never written by hand: from engine types and from the fields of
Mondskript classes ([mondskript.md](mondskript.md)).

## 2. Field flags

Every field carries one or more flags:

| Flag | Meaning |
|---|---|
| `save` | written to the save game (SRAM/flash) |
| `link` | sent on link transfer (GBA to GBA, editor to GBA) |
| `snapshot` | part of a save state (debugging, "state" reload mode) |
| `transient` | not saved anywhere; reset to its default on load |
| `derived` | not saved; recomputed after loading by the owning code |

`save` and `link` imply `snapshot`. A field without any flag is `snapshot` only, and the editor
MUST mark it so missing `save` flags stand out.

## 3. Schema

The cooker writes the schema as the `SCHM` chunk:

```c
struct BmSchemaHeader {
    u32 schema_hash;           // CRC-32 over the field list; equal hash = same layout
    u32 arena_size;
    u16 field_count;
    u16 pad;
};
struct BmSchemaField {         // 16 bytes
    u16 name;                  // string ID
    u16 owner;                 // class ID, or 0xFFFF for engine globals
    u32 offset;                // within the arena (for per-object fields: within the record)
    u16 size;
    u8  type;                  // bool, s8..s32, u8..u32, fix, fix16, handle, enum, array
    u8  flags;                 // bit 0 save, 1 link, 2 snapshot, 3 transient, 4 derived
    u16 count;                 // array length, 1 otherwise
    u16 persistent_scope;      // 0 global, 1 per persistent object ID, 2 per entity slot
};
```

Generated C headers declare the same layout as `struct`s with `_Static_assert` on every
offset and size.

## 4. State map

The editor shows the schema as a **state map**: a table (field, owner, type, offset, bytes,
flags) and a memory bar of the arena, with totals for `save`, `link` and `snapshot`. It shows
the size of a save game per slot and the size of a link transfer.

## 5. Per-object state

State of placed objects that must survive (defeated, collected, opened, broken blocks) is kept
per **persistent ID** of the object (see [room-format.md](room-format.md) §5), in a table per
room. Objects that are added later simply have no entry yet.

## 6. Save games

A save game contains a header (magic `BMSV`, schema hash, profile, slot, CRC-32) and the `save`
fields in schema order. Loading a save with a different schema hash uses the migration table
of the build that wrote it, if the game ships one; otherwise the fields are matched by owner and
name, unknown ones are dropped and new ones get their defaults.

## 7. Migration table

Used by the `hot_migrate` reload mode and for old save games. A list of operations from the old
to the new layout:

```
COPY  old_offset new_offset size
FILL  new_offset size value (from defaultproperties)
CONV  old_offset new_offset from_type to_type   (e.g. int to fix)
```

The editor generates it by matching fields by owner and name.

## History

- 0.1: first draft.
