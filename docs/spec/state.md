# Game state

Version 0.2. Describes the state as the runtime keeps and serialises it (`src/world/state.c`,
the field table in `src/world/game.c`). Version 0.1 of this document described a larger
design (a generated arena, a schema chunk, migration); what of it is not built is listed in
section 7.

## 1. What state is

The game's state is a set of named **fields** with a type, a count and flags. On every
machine they serialise to the same bytes, so a state written on the PC loads on the GBA and
back, and two machines given the same state and the same keys stay identical
([runtime-core.md](runtime-core.md)).

Fields are grouped in **blocks**. The game has one block, `game`. The test game adds a second
one, `autopilot` (`jumpHold`, `idle`, both u16, snapshot), when it is played by its autopilot
or could be.

## 2. Flags

| Bit | Flag | Meaning |
|---|---|---|
| 0 | `save` | written to the save game |
| 1 | `link` | sent when one device brings another to where it is |
| 2 | `snapshot` | part of a save state |
| 3 | `transient` | never written; 0 after a load |
| 4 | `derived` | never written; worked out again after a load |

`save` and `link` imply `snapshot`. A field without flags is `snapshot`. A field with
`transient` or `derived` is never written, whatever else it has.

Writing takes a **mask** of the bits 0-2; a field is written if it has (with the rule above)
any flag of the mask. The three uses: `save` (1) for the save game, `link` (2) for a transfer
between devices, `snapshot` (4) for everything.

## 3. Serialised form

Everything little endian.

```
0    "BMST"
4    u8  version (1)
5    u8  flag mask
6    u16 payload size in bytes
8    u32 schema hash
12   u32 CRC-32 of the payload
16   payload
```

The payload is the selected fields: block after block, field after field in table order,
array elements one after the other, nothing in between.

| Type | Number | Bytes | |
|---|---|---|---|
| u8, s8 | 0, 1 | 1 | |
| u16, s16 | 2, 3 | 2 | |
| u32, s32 | 4, 5 | 4 | |
| bool | 6 | 1 | 0 or 1 |
| fix | 7 | 4 | signed 24.8 |

**Schema hash.** A CRC-32 ([pack-format.md](pack-format.md) §1) run over, for each block: its
name with the terminating zero byte; then for each field of its table, selected or not: the
name with its zero byte, then four bytes: type number, flags, count low byte, count high
byte. Field names are exactly as in the table below. Offsets in memory are not part of it.

**Reading** fails with "bad header" (too short, other magic or version), "other schema" (the
hash differs, or the payload size is not what the mask selects) or "bad checksum". Nothing is
changed in those cases.

## 4. The fields of `game`

In serialisation order. Count is the array length.

| Field | Type | Count | Flags |
|---|---|---|---|
| `roomIndex` | u32 |  | save link |
| `player.x` | fix |  | save link |
| `player.y` | fix |  | save link |
| `player.vx` | fix |  | link |
| `player.vy` | fix |  | link |
| `player.onGround` | bool |  | link |
| `player.climbing` | bool |  | link |
| `player.inWater` | bool |  | derived |
| `player.jumpCutDone` | bool |  | snapshot |
| `player.facing` | s8 |  | save link |
| `player.coyote` | u8 |  | snapshot |
| `player.jumpBuffer` | u8 |  | snapshot |
| `player.dropThrough` | u8 |  | snapshot |
| `player.flipped` | bool |  | snapshot link |
| `player.safeX` | s32 |  | save |
| `player.safeY` | s32 |  | save |
| `frame` | u32 |  | snapshot |
| `keysPrevious` | u16 |  | snapshot |
| `hurtTimer` | u16 |  | snapshot |
| `lockTimer` | u16 |  | snapshot |
| `animHeld` | u8 |  | snapshot |
| `health` | s16 |  | save link |
| `mana` | s16 |  | save link |
| `speedPercent` | s16 |  | snapshot |
| `resists` | u8 | 4 | snapshot |
| `returnPending` | u8 |  | snapshot |
| `lockGround`, `lockAir` | u8 |  | snapshot |
| `pickupSound` | u8 |  | snapshot |
| `doorSeq`, `doorTimer`, `doorArrive`, `doorArriveTimer` | u8 |  | snapshot |
| `cmdDirs` | u8 | 8 | snapshot |
| `cmdTimes` | u16 | 8 | snapshot |
| `cmdHead`, `cmdCount` | u8 |  | snapshot |
| `cmdLast` | u32 |  | snapshot |
| `cmdDone` | u16 |  | snapshot |
| `fxKind` | u8 |  | snapshot |
| `fxA`, `fxB` | s16 |  | snapshot |
| `teleTimer` | u8 |  | snapshot |
| `teleRoom` | u32 |  | snapshot |
| `teleX`, `teleY` | s16 |  | snapshot |
| `fxApplied` | u8 |  | transient |
| `noticeKey` | u32 |  | snapshot |
| `noticeTitled` | u8 |  | snapshot |
| `noticeText` | u16 |  | snapshot |
| `noticeCount` | u8 |  | snapshot |
| `noticeTimer` | u16 |  | snapshot |
| `mapExplored` | u8 | 64 | save, link |
| `mapRevealed` | u8 | 8 | save, link |
| `savedRoom` | u32 |  | save |
| `savedX`, `savedY` | s32 |  | save |
| `dialogue` | u8 |  | snapshot |
| `dialogueSpeaker`, `dialogueLine` | u16 |  | snapshot |
| `dialoguePortrait`, `dialogueVoice` | u16 |  | snapshot |
| `dialogueShown`, `dialogueLength` | u8 |  | snapshot |
| `doorCooldown` | u16 |  | snapshot |
| `saveCooldown` | u16 |  | transient |
| `deaths` | u32 |  | save |
| `roomChanges` | u32 |  | save |
| `hurts` | u32 |  | save |
| `saves` | u32 |  | save |
| `playerAnim.anim` | u8 |  | snapshot |
| `playerAnim.finished` | u8 |  | snapshot |
| `playerAnim.step` | u16 |  | snapshot |
| `playerAnim.timer` | u16 |  | snapshot |
| `actors.moduleHash` | u32 |  | snapshot |
| `actors.count` | u8 |  | snapshot |
| `actors.classIndex` | u16 | 8 | snapshot |
| `actors.state` | u8 | 8 | snapshot |
| `actors.wait` | u8 | 8 | snapshot |
| `actors.pc` | u16 | 8 | snapshot |
| `actors.timer` | u16 | 8 | snapshot |
| `actors.fields` | s32 | 64 | snapshot |
| `actors.x` | fix | 8 | snapshot |
| `actors.y` | fix | 8 | snapshot |
| `actors.facing` | s8 | 8 | snapshot |
| `actors.hidden` | u8 | 8 | snapshot |
| `actors.touching` | u8 | 8 | snapshot |
| `actors.anim` | u8 | 8 | snapshot |
| `actors.animFinished` | u8 | 8 | snapshot |
| `actors.animStep` | u16 | 8 | snapshot |
| `actors.animTimer` | u16 | 8 | snapshot |
| `actors.health` | s32 | 8 | snapshot |
| `actors.team` | u8 | 8 | snapshot |
| `actors.invulnerable` | u8 | 8 | snapshot |
| `actors.affinity` | u8 | 8 | snapshot |
| `actors.knock` | fix | 8 | snapshot |
| `actors.steady` | u8 | 8 | snapshot |
| `actors.spawned`, `actors.gone` | u8 | 8 each | snapshot |
| `actors.seed` | u32 |  | snapshot |
| `actors.storeCount` | u8 |  | save |
| `actors.storeKey` | u32 | 16 | save |
| `actors.storeValue` | s32 | 16 | save |
| `inventory.count` | u8 |  | save, link |
| `inventory.key` | u32 | 64 | save, link |
| `inventory.amount` | u8 | 64 | save, link |
| `inventory.equipped` | u32 | 8 | save, link |
| `inventory.quick` | u32 | 4 | save, link |
| `inventory.statBase` | s16 | 16 | save, link |
| `inventory.statBonus` | s16 | 16 | snapshot |
| `attachments.classKey` | u32 | 12 | snapshot |
| `attachments.state`, `attachments.wait` | u8 | 12 each | snapshot |
| `attachments.pc`, `attachments.timer` | u16 | 12 each | snapshot |
| `attachments.fields` | s32 | 96 | snapshot |
| `attachments.lifetime` | u16 | 12 | snapshot |
| `hits.source`, `hits.slot` | u8 | 8 each | snapshot |
| `hits.name` | u16 | 8 | snapshot |
| `hits.damage` | s16 | 8 | snapshot |
| `hits.element`, `hits.anim` | u8 | 8 each | snapshot |
| `hits.frames` | u16 | 8 | snapshot |
| `hits.reach` | s16 | 8 | snapshot |
| `hits.aim` | u8 | 8 | snapshot |
| `hits.aimNext` | u8 | 9 | snapshot |
| `hits.struck` | u16 | 8 | snapshot |
| `screens.count` | u8 |  | snapshot |
| `screens.classKey` | u32 | 4 | snapshot |
| `screens.state`, `screens.wait` | u8 | 4 each | snapshot |
| `screens.pc`, `screens.timer` | u16 | 4 each | snapshot |
| `screens.fields` | s32 | 32 | snapshot |
| `screens.repeat` | u8 |  | transient |

Sizes with this table: a save game is 655 bytes of payload, a link transfer 501, a snapshot
2205 (2211 with the autopilot); each plus the 16-byte header. The buffers for a snapshot and a
pushed state hold 3072 bytes. Attachment slots 0 to 7 are the
player's, 8 to 11 the room's. A snapshot is larger than one
link packet: `STATE_DATA` and `STATE_PUT` carry it in parts ([link-protocol.md](link-protocol.md)).

The inventory ([data-format.md](data-format.md), `src/world/inventory.h`): `count` entries in
the order their kinds were first given, each a key (the FNV-1a hash of a record's or item's
name) and an amount from 1 to 255; an entry that reaches 0 is removed and the ones behind it
move up. `equipped` and `quick` hold keys (0 none) and are not used by the engine yet;
`statBase` holds the player's own stat values, numbered by an enum the game declares.

The world map ([world-format.md](world-format.md) §4): `mapExplored` has a bit per cell of the
map (bit `cell % 8` of byte `cell / 8`, cells numbered as in the `WRLD` chunk), set when the
player has been in it; `mapRevealed` a bit per room of the map, set by `map_reveal`.

`actors.fields` holds 8 values per scripted object, object 0 first. The meaning of the
fields is in [runtime-core.md](runtime-core.md).

## 5. After a load

Loading a state only sets fields. Then the runtime enters the stored room again (collision,
objects, sprites, sound, the room on screen) without moving the player, sets `saveCooldown`
to 0, and updates camera and sprites.

Scripted objects continue where the state has them, unless the state does not fit: another
script module (`actors.moduleHash` differs from the pack's), another number of scripted
objects in the room, or another class at some position. Then they start fresh, as on entering
the room.

Not part of the state: the position in the music. A load starts the room's song from its
beginning.

## 6. Uses

- **Save game**: block `game` only, mask `save`, at offset 0 of the save memory (1024 bytes
  are read). Loading one also sets `vx` and `vy` to 0 and starts the scripted objects fresh.
- **Snapshot**: all blocks, mask `snapshot`: save states, the editor, the input sync
  ([link-protocol.md](link-protocol.md) §10).
- **Link**: all blocks, mask `link`: GBA to GBA ([link-protocol.md](link-protocol.md) §9).
  Note what it leaves out: the scripted objects, the random seed, the frame counter and the
  timers are `snapshot` only. The receiving device gets the room and the player; its scripted
  objects keep the state they had if they fit the room it ends up in, and start fresh
  otherwise. The inventory goes along. A transfer after which both devices must stay identical needs
  a snapshot.
- **State checksum** in the input sync: the CRC-32 of the whole serialised snapshot (header
  included) of block `game` alone.

## 7. Planned, not built

- A schema chunk (`SCHM`) written by the cooker, and layouts generated from it.
- State of scripted objects per class instead of fixed arrays of 8.
- Migration between schema versions: a state of another schema is refused.
- Save slots, a profile field and compression in the save game.
- The state map in the editor shows the table with sizes and totals; it does not show values
  of a running game.
