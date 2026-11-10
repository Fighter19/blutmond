# Pack format

Version 0.1, draft.

A pack is the cooked, platform-specific form of a project's content (or part of it, such as one
level). It is position independent: it can lie in ROM, in EWRAM, or arrive over the link, and a
newer runtime can load packs made for an older one (see [runtime-abi.md](runtime-abi.md)).

The layout grew out of the tagged archive of the earlier SceneCompiler project (`GARCHIVE` with
`PALE`, `MTIL`, `INDX`, `PIXL`, `AMAP`).

## 1. Layout

```
BmPackHeader
BmChunkEntry toc[chunk_count]
chunk data ...            (each chunk 4-byte aligned, zero padded)
```

```c
struct BmPackHeader {          // 32 bytes
    char magic[4];             // "BMPK"
    u16  format_major;         // 0 in this draft
    u16  format_minor;         // 1
    u16  abi_major;            // runtime API version the pack needs (runtime-abi.md)
    u16  abi_minor;
    char profile[4];           // "GBA ", "SNES", "HOST"
    u32  total_size;           // whole pack in bytes
    u32  chunk_count;
    u32  toc_offset;           // usually 32
    u32  crc32;                // CRC-32 (IEEE) of everything after this header
};

struct BmChunkEntry {          // 24 bytes
    char id[4];                // chunk type, section 3
    u32  offset;               // from the start of the pack
    u32  size;                 // stored size
    u32  unpacked_size;        // size after decompression (= size when not compressed)
    u16  compression;          // section 4
    u16  group;                // load group, section 2; 0 = global
    u32  hash;                 // CRC-32 of the unpacked data; used by the live link
};
```

Every reference inside a chunk is either a chunk index (`u16`, position in the table of
contents) or an offset relative to the start of that chunk. Absolute addresses MUST NOT appear
in a pack.

## 2. Load order and groups

Reading a pack sequentially is fast, seeking asset by asset is slow (this was measured in
SceneLoaderGBA with a CF card). Therefore:

- Chunks are grouped: group 0 holds global data (class table, string table, shared tiles and
  instruments), group *n* holds everything room *n* needs that is not global.
- Within the file, chunks of one group MUST be contiguous and in the order the loader uses
  them: `ROOM`, `PALE`, `TILE`, `TMAP`, `COLL`, `SHAP`, `OBJS`, `EFCT`, then sound and scripts.
- The loader reads one group as a single sequential stream and only keeps the chunks it still
  needs.

## 3. Chunk types

| Id | Content |
|---|---|
| `META` | project name, build time, cooker version, source hash (for the live link) |
| `STRS` | string table: `u16 count`, offsets, UTF-8 or profile encoding |
| `CLAS` | class table: class IDs, names (string IDs), field layouts, component lists, method table offsets |
| `SCHM` | state schema, see [state.md](state.md) |
| `PALE` | palettes in the profile's colour format (BGR555 on GBA/SNES) |
| `TILE` | tile graphics, 4 or 8 bpp, profile tile format |
| `TMAP` | tile map of one layer, profile map entry format |
| `ROOM`, `COLL`, `SHAP`, `OBJS` | room data, see [room-format.md](room-format.md) §7 |
| `EFCT` | raster effect: per-line register tables |
| `SPRT` | sprite frames and meta-sprite layouts |
| `ANIM` | animations: frame lists, timing, hitboxes per frame |
| `SONG` | music sequence |
| `INST` | instruments |
| `SMPL` | samples (8-bit signed PCM, loop points) |
| `SFX ` | sound effects |
| `MODB` | script module, bytecode, see [mondskript.md](mondskript.md) |
| `MODN` | script module, native code with relocations |
| `WRLD` | world map: rooms, positions, door links, map screen data |

Readers MUST skip chunk types they do not know.

## 4. Compression

| Value | Method | Notes |
|---|---|---|
| 0 | none | |
| 1 | LZ77, GBA BIOS format | can be decompressed straight to VRAM with the BIOS (`LZ77UnCompVram`) |
| 2 | LZ4 block format | faster to decompress into EWRAM |

The cooker picks per chunk; tile data for VRAM SHOULD use 1.

## 5. Delta transfer

The live link (see [link-protocol.md](link-protocol.md)) compares `hash` values per chunk and
only sends chunks that changed. Chunks that did not change keep their content but MAY move; the
receiver therefore rebuilds the pack from the new table of contents and the chunks it already
has.

## History

- 0.1: first draft.
