# Blutmond specification

Version 0.1, draft. This directory holds the normative description of Blutmond's data formats,
protocols and language. The reasons behind them, the research and the roadmap are in
[../PLAN.md](../PLAN.md).

| Document | Covers |
|---|---|
| [asset-model.md](asset-model.md) | platform-neutral source assets, cooking per platform profile, overrides, change report |
| [project-format.md](project-format.md) | the project file: rooms, asset folders, editor operations on them |
| [runtime-core.md](runtime-core.md) | what the runtime does each frame, exactly: fixed point, collision, the player's body, doors, camera, video memory, scripted objects, sprites |
| [gba-backend.md](gba-backend.md) | the hardware interface and its GBA implementation: memory, start code, interrupts, video, sound, serial port, the video snapshot |
| [room-format.md](room-format.md) | rooms: layers, collision cells, materials, physics, objects, engine classes, Tiled mapping |
| [sprite-format.md](sprite-format.md) | sprites: sheet, frames, anchors, hitboxes, animations; the cooked `SPRT` chunk |
| [sound-format.md](sound-format.md) | songs and sound effects: instruments, notes, the mixer and sequencer, the cooked `SONG` chunk |
| [data-format.md](data-format.md) | structures and records: record files, the cooked `DTAB` chunk, reading records from scripts |
| [pack-format.md](pack-format.md) | the cooked binary pack: header, table of contents, chunk types, compression, load order |
| [streaming.md](streaming.md) | planned: live-link streaming with rooms as anchors, room chain, priorities, load profiles |
| [runtime-abi.md](runtime-abi.md) | live reload as built (content pool, restart, hot, state); planned: API table, handles, residency |
| [state.md](state.md) | the game state: fields and flags, the serialised form, schema hash, save game, snapshot, what is rebuilt after loading |
| [link-protocol.md](link-protocol.md) | transports, multiboot, words and packets, error handling, debugging, GBA to GBA, input mirror |
| [mondskript.md](mondskript.md) | the scripting language, bytecode and native modules |

## Conventions

- **MUST**, **SHOULD**, **MAY** are used as in RFC 2119.
- All binary data is **little endian**. Offsets are in bytes from the start of the containing
  structure unless stated otherwise. Structures are aligned to 4 bytes; padding is zero.
- Integer types: `u8`, `u16`, `u32`, `s8`, `s16`, `s32`.
- Fixed point: `fix` is a signed 32-bit value with 8 fraction bits (24.8). `fix16` has 16
  fraction bits (16.16). Source formats (Tiled, text) store decimals; the cooker converts with
  round-to-nearest. Overflow wraps (two's complement), on every target.
- Positions are in pixels, times in frames (59.7275 per second on the GBA), speeds in px/frame,
  accelerations in px/frame².
- Four-character codes (`'ROOM'`) are stored as four ASCII bytes in reading order.
- Every format carries a version. A reader MUST reject a major version it does not know and
  MUST accept higher minor versions, ignoring what it does not know.

## Implementing the runtime from these documents

To write the GBA runtime again, read in this order: [gba-backend.md](gba-backend.md) (the
machine), [pack-format.md](pack-format.md) §0 and the chunk formats (room-format.md §7,
sprite-format.md, sound-format.md, mondskript.md §11), [runtime-core.md](runtime-core.md)
(the game), [state.md](state.md), [link-protocol.md](link-protocol.md) §2 to §4 and
[runtime-abi.md](runtime-abi.md) §0 (the link and reloads). Each document says at its top or
in a section "what is built" where it describes a plan and not the code. Where a document
and the code differ, that is a bug in one of them; the tests in `tests/` are the reference
for behaviour.

## Status

Everything here is a draft until the matching milestone in the plan is done; changes are
expected. Changes to a format bump its version and are listed in the document's history
section.
