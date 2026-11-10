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
    u16  flags;                // bits 0-1: storage mode (0 rom, 1 ram, 2 stream)
    u16  reserved;
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

Every asset is stored **exactly once** in a pack: identical assets are merged by content hash,
and identical 8×8 tiles (including flipped copies) are stored once per level. The only
exception is the opt-in described in §2.5 for streamed packs; it never applies to ROM packs.

The levels serve two purposes that must not be confused: they decide **what is resident in
VRAM and RAM** at run time (all storage modes), and they decide **how the file is ordered**
(only relevant for streamed packs, §2.4).

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

### 2.4 Storage modes and file order

A pack is cooked for one storage mode (`flags` in the header, bit 0–1):

| Mode | Where the pack is | Access | Consequences |
|---|---|---|---|
| `rom` | cartridge ROM (release) | memory mapped, random access is cheap | chunks are used **in place**: uncompressed tiles and maps go from ROM to VRAM by DMA, tables are read directly; only compressed chunks are unpacked. Nothing is copied to RAM just to keep it, so no asset ever needs a second copy. File order only matters for the cooker's convenience |
| `ram` | EWRAM (multiboot) | random access | the whole pack (global + current area + current room, or all of it if it fits) is in RAM; same rules as `rom` |
| `stream` | CF/SD card, live link | sequential is fast, seeking is slow (measured in SceneLoaderGBA with a CF card) | the ordering rules below apply |

For `stream` packs:

- chunks of one group MUST be contiguous and in the order the loader uses them: `VLAY` (area
  groups), `ROOM`, `PALE`, `TILE`, `TMAP`, `COLL`, `SHAP`, `OBJS`, `EFCT`, then sprites, sound
  and scripts;
- groups are ordered global, then each area followed by its rooms;
- the loader reads one group as a single sequential stream and only keeps the chunks it still
  needs.

### 2.5 Copies (streamed packs only, opt-in)

In `stream` mode an asset shared by several areas sits on the global level and therefore stays
in RAM for the whole game. If RAM is short, the developer MAY allow the cooker to place a copy
in each area group instead (`share = per_area`), so it is only resident inside those areas. This
trades card space for RAM. The cooker lists every copy in its report with the bytes it costs.
`rom` and `ram` packs MUST NOT contain copies; the cooker ignores `per_area` for them.

## 3. Chunk types

| Id | Content |
|---|---|
| `VLAY` | VRAM and palette layout of an area, section 2.3 |
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
- 0.1.2: storage modes `rom`, `ram`, `stream`; assets stored exactly once, copies only as an
  opt-in for streamed packs.
- 0.1.1: global, area and room levels; deduplication; dependency lists; fixed VRAM layout
  per area (`VLAY`).
