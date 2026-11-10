# Blutmond specification

Version 0.1, draft. This directory holds the normative description of Blutmond's data formats,
protocols and language. The reasons behind them, the research and the roadmap are in
[../PLAN.md](../PLAN.md).

| Document | Covers |
|---|---|
| [asset-model.md](asset-model.md) | platform-neutral source assets, cooking per platform profile, overrides, change report |
| [room-format.md](room-format.md) | rooms: layers, collision cells, materials, physics, objects, engine classes, Tiled mapping |
| [pack-format.md](pack-format.md) | the cooked binary pack: header, table of contents, chunk types, compression, load order |
| [streaming.md](streaming.md) | live-link streaming: rooms as anchors, room chain, priorities, load profiles, testing single assets |
| [runtime-abi.md](runtime-abi.md) | API table, handles, memory regions, live reload modes |
| [state.md](state.md) | state arena, field flags, schema, state map, save games, migration |
| [link-protocol.md](link-protocol.md) | transports, boot, packets, error handling, reset setting |
| [mondskript.md](mondskript.md) | the scripting language, bytecode and native modules |

## Conventions

- **MUST**, **SHOULD**, **MAY** are used as in RFC 2119.
- All binary data is **little endian**. Offsets are in bytes from the start of the containing
  structure unless stated otherwise. Structures are aligned to 4 bytes; padding is zero.
- Integer types: `u8`, `u16`, `u32`, `s8`, `s16`, `s32`.
- Fixed point: `fix` is a signed 32-bit value with 8 fraction bits (24.8). `fix16` has 16
  fraction bits (16.16). Source formats (Tiled, text) store decimals; the cooker converts with
  round-to-nearest. Overflow wraps (two's complement), on every target.
- Positions are in pixels, times in frames (60 per second on the GBA), speeds in px/frame,
  accelerations in px/frame².
- Four-character codes (`'ROOM'`) are stored as four ASCII bytes in reading order.
- Every format carries a version. A reader MUST reject a major version it does not know and
  MUST accept higher minor versions, ignoring what it does not know.

## Status

Everything here is a draft until the matching milestone in the plan is done; changes are
expected. Changes to a format bump its version and are listed in the document's history
section.
