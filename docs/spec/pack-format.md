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
struct BmPackHeader {          // 36 bytes
    char magic[4];             // "BMPK"
    u16  format_major;         // 0 in this draft
    u16  format_minor;         // 1
    u16  abi_major;            // runtime API version the pack needs (runtime-abi.md)
    u16  abi_minor;
    char profile[4];           // "GBA ", "SNES", "HOST"
    u32  total_size;           // whole pack in bytes
    u32  chunk_count;
    u32  toc_offset;           // usually 36
    u16  flags;                // bits 0-1: storage mode (0 rom, 1 stream)
    u16  reserved;
    u32  crc32;                // CRC-32 (IEEE) of everything after this header
};

struct BmChunkEntry {          // 24 bytes
    char id[4];                // chunk type, section 3
    u32  offset;               // from the start of the pack
    u32  size;                 // stored size
    u32  unpacked_size;        // size after decompression (= size when not compressed)
    u8   compression;          // section 4
    u8   category;             // streaming.md §3: level, graphics, objects, actors, ...
    u16  group;                // load group, section 2; 0 = global
    u32  hash;                 // CRC-32 of the unpacked data; used by the live link
};
```

Every reference inside a chunk is either a chunk index (`u16`, position in the table of
contents) or an offset relative to the start of that chunk. Absolute addresses MUST NOT appear
in a pack.

## 2. Groups, areas and sharing

Many rooms use the same graphics: all rooms of a castle wing share one tileset, every room
shows the player and the HUD, enemies appear in many rooms. Shared data MUST be stored once in
the pack and SHOULD stay in VRAM across room changes.

### 2.1 Three levels

| Level | Group value | Content | Lifetime at run time |
|---|---|---|---|
| global | 0 | class table, strings, schema, player and HUD graphics, common sprites, fonts, instruments and samples used by several areas | loaded once, never evicted |
| area | 1–255 | everything shared by the rooms of one **area** (room property `area`, see [room-format.md](room-format.md) §1): tilesets, palettes, enemy sprites, the area's songs | loaded when entering the area, kept while inside it |
| room | 256 and up | what only this room uses: its tile maps, collision, objects, room-specific tiles, effects | loaded per room |

The cooker decides the level of each asset from its use: used by rooms of one area → area
level; used by rooms of several areas → global level; used by one room → room level. The
developer MAY pin an asset to a level (asset property `share = global | area | room`).

Every asset is stored **exactly once** in a pack, in every storage mode: identical assets are
merged by content hash, and identical 8×8 tiles (including flipped copies) are stored once per
level.

The levels decide **what is resident in VRAM and RAM** at run time and where shared data sits
in VRAM (§2.3). How the content of a `stream` pack reaches the GBA is decided per room, not per
level ([streaming.md](streaming.md)).

### 2.2 Dependencies instead of guessing

Each `ROOM` chunk lists every chunk it needs outside its own group (`deps`, see
[room-format.md](room-format.md) §7). The runtime loads exactly that list; nothing is found by
naming conventions.

### 2.3 Fixed VRAM layout per area

Tile maps store VRAM tile numbers and palette bank numbers directly, so the cooker plans the
VRAM of each area once and writes it as a `VLAY` chunk in the area group:

```c
struct BmVramLayout {
    u16 shared_tile_base, shared_tile_count;   // area tiles: fixed place in BG VRAM
    u16 room_tile_base,   room_tile_count;     // the rest: per room (and tile streaming)
    u16 shared_obj_base,  shared_obj_count;    // sprite tiles of the area
    u16 room_obj_base,    room_obj_count;
    u8  bg_palette_shared_mask;                // bit n: BG palette bank n is area-wide
    u8  pad;
    u16 obj_palette_shared_mask;
    u16 screen_blocks;                         // bit n: screen block n holds tile maps
    u16 pad2;
};
```

- Global data (player, HUD, font) has fixed places that are the same in every area.
- Area tiles and area palette banks are at the same place in every room of the area, so tile
  maps of those rooms reference them without any rebasing at load time.
- Room tiles go into the room region; rooms larger than that use the tile cache (streaming)
  inside the room region only.
- The palette packer works over **all rooms of an area at once**, so a shared tileset gets the
  same palette banks in every room.

### 2.4 Storage modes

A pack is cooked for one storage mode (`flags` in the header, bits 0–1):

| Mode | Where the pack is | Consequences |
|---|---|---|
| `rom` | cartridge ROM; the only release mode | random access is cheap, so chunks are used **in place**: uncompressed tiles and maps go from ROM to VRAM by DMA, tables are read directly, only compressed chunks are unpacked. Nothing is copied to RAM just to keep it. Every asset exists exactly once |
| `stream` | on the PC, during development | the runtime was booted by multiboot or runs from a cartridge, and the PC sends chunks over the live link as described in [streaming.md](streaming.md). The GBA only holds what the current scene needs |

A multiboot image that carries all content in RAM is not a supported release mode; multiboot
exists only together with the live link.

### 2.5 File order of stream packs

For `stream` packs the chunks are written in **room chain** order ([streaming.md](streaming.md)
§1): global data first, then for each room in the chain the chunks it adds, followed by its own
level data, with an `RCHN` chunk at the start that lists the chain and each room's added and
dropped chunks. The PC reads the pack in that order and skips every chunk the GBA already has
or the session's load profile excludes. No asset is stored twice; a room that needs an asset
from far back in the chain references it by chunk index.

## 3. Chunk types

| Id | Content |
|---|---|
| `VLAY` | VRAM and palette layout of an area, section 2.3 |
| `RCHN` | room chain for streaming: order of rooms, chunks each room adds and drops (`stream` packs only) |
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
- 0.1.3: only `rom` (release) and `stream` (live link, multiboot); stream packs in room
  chain order with `RCHN`; chunk category in the table of contents; no copies in any mode.
- 0.1.2: storage modes `rom`, `ram`, `stream`; assets stored exactly once, copies only as an
  opt-in for streamed packs.
- 0.1.1: global, area and room levels; deduplication; dependency lists; fixed VRAM layout
  per area (`VLAY`).
