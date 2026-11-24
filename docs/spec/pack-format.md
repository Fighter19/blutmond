# Pack format

Version 0.2, draft. Section 0 says exactly what is built; the sections after it also describe
what is planned and say so.

A pack is the cooked, platform-specific form of a project's content (or part of it, such as one
level). It is position independent: it can lie in ROM, in EWRAM, or arrive over the link, and a
newer runtime can load packs made for an older one (see [runtime-abi.md](runtime-abi.md)).

The layout grew out of the tagged archive of the earlier SceneCompiler project (`GARCHIVE` with
`PALE`, `MTIL`, `INDX`, `PIXL`, `AMAP`).

## 0. What is built

Everything is little endian. This is all a reader of today's packs needs, together with the
chunk formats named in section 3.

- **Header and table**: as in section 1. `format_major` 0 (another value is refused),
  `format_minor` 2, `abi_major` 0, `abi_minor` 1, `flags` = the layout the cooker was asked
  for in bits 0-3 (0 unless told otherwise; nothing reads it), bit 4 (`0x10`) an update pack
  (below), `toc_offset` 36.
- **Left-out chunks** (minor 2): the top two bits of an entry's `compression` byte are flags;
  the compression itself is the low six. Bit 6 (`0x40`, absent): the chunk is not in this
  pack; `offset` and `size` are 0, the rest of the entry (id, unpacked size, hash) is as in
  the full pack. Reading it gives no data (`bmPackChunkData`, `bmPackChunkUnpack`). The table
  keeps every entry, so chunk indices, room numbers and string ids are those of the full pack.
  A room whose `ROOM` chunk is left out is not entered (`bmGameFindRoom` does not find it, a
  door there does not open); a left-out `LANG` is not offered. `STRS` is never left out.
  The cooker writes such a pack when told `--absent-room ID` (the room and every chunk only
  left-out rooms name) or `--absent-kind KIND` (every chunk of that kind); the multiboot game
  `game_mb` carries the test level without its five debug rooms, the Hall of Abilities and
  the translations (76 KB instead of 89 KB). The live link leaves chunks out the same way to fit a pack to a
  runtime (a fitted push, [runtime-abi.md](runtime-abi.md) §0).
- **Update packs** (minor 2, reader and `bmPackMakeUpdate` only; nothing sends them yet):
  bit 7 (`0x80`, in base) of an entry: the chunk lies in a base pack, at `offset` there. Such
  a pack is opened with `bmPackOpenSet` and its base, which must not be an update itself;
  every in-base chunk must lie inside the base.
- **Opening a pack** fails if: the magic is wrong; `total_size` is larger than the data or
  smaller than 36; the table does not lie inside `total_size`; a chunk does not lie inside
  `total_size`; or (when asked to check) the CRC is wrong. Bytes after `total_size` are
  ignored.
- **`crc32`**: the standard CRC-32 (zlib's) of the bytes from offset 36 to `total_size`: the
  table, the chunks and the zero padding between them.
- **`hash`** of a chunk: the same CRC-32 of its `size` bytes.
- **Chunks** follow the table in table order, each padded with zeros to a multiple of 4.
- **Compression**: the cooker compresses `TMAP` and `COLL` chunks with LZ77 (1, section 4)
  where that makes them smaller, and only those whose rooms fit the runtime's unpack buffer:
  every room that reads the chunk must have all its `TMAP`s and its `COLL` (each counted
  once per use, rounded up to 4 bytes) at most `BM_PACK_ROOM_UNPACK_BYTES` (24 KB) unpacked.
  A larger room's chunks stay uncompressed and are read in place. Every other chunk is
  written with 0 and `unpacked_size = size`. `hash` and `size` are of the compressed bytes.
  Entering a room unpacks its compressed chunks into the buffer (on the GBA with the BIOS,
  `LZ77UnCompWram`: about 310,000 cycles for the test level's largest room, 20 KB);
  `bmPackChunkData` gives no data for a compressed chunk, `bmPackChunkUnpack` does. On the
  test level: 55 KB of maps and collision become 7 KB, the pack 122 KB becomes 74 KB.
- **Categories**: 0 level, 1 graphics, 2 objects, 3 actors, 4 player, 5 scripts, 6 music,
  7 sound effects, 8 text, 9 meta. The runtime does not read them.
- **Groups**: 0 global; `1 + n` for the n-th area; `256 + n` for the n-th room. The runtime
  does not read them either: a room names the chunks it needs.
- **Strings (`STRS`)**: `u16 count; u16 0; u32 offset[count]`, each offset from the start of
  the chunk to a NUL-terminated string. A string id is its index. The first `STRS` chunk in
  the table is the pack's string table; ids that do not exist read as "no string". The
  cooker writes it last.
- **Rooms** are numbered by the order of their `ROOM` chunks in the table, from 0. Room 0 is
  where a new game starts.
- **What the cooker writes**, in this order: per area `VLAY`, `PALE`, `TILE`, then per room of
  the area (if lit: its light layer's `TMAP` and its `LITE`) its `TMAP`s, `COLL`, `OBJS`, `ROOM`; then every `SPRT`, every `FIGR`, every `SONG`, every `FONT`,
  every `MODB`, optionally `MODN`, every `DTAB`, the `LANG`s, and `STRS`.
- **What the runtime reads**: `ROOM`, `TMAP`, `COLL`, `OBJS`, `LITE`, `PALE`, `TILE`, `SPRT`, `FIGR`, `SONG`,
  `FONT`, `MODB`, `MODN`, `DTAB`, `LANG`, `WRLD`, `MIGR`, `STRS`. `VLAY` is written but not read; where things go in video memory is
  fixed in the runtime ([runtime-core.md](runtime-core.md) §6).
- **Limits**: 3 graphics layers per room; 255 areas; the tiles of an area must fit the
  profile (GBA: 1024); 15 background palette banks (the 16th is the UI's); a pack pushed over the link has at most 512 chunks
  and must fit the runtime's content pool ([runtime-abi.md](runtime-abi.md) §0).
- **Sharing**: tiles are merged (with flips) per area and palettes are packed per area. A
  room's `TMAP` or `COLL` chunk that is byte for byte one an earlier room has is not written
  again: the room's header refers to the earlier chunk, which stays in the group of the room
  that came first (the four effect rooms of the test level share their maps and collision:
  22 KB less). Other identical assets are not merged across chunks, and nothing decides
  between global, area and room level: sprites, songs and scripts are always global.

Not built: `RCHN`, `META`, `SHAP`, `CLAS`, `SCHM`, `EFCT`, `SFX `; the layouts other
than writing chunks in the order above; `bmpack relayout`; the `stream` way of section 2.4 as
described there (the link sends whole packs, chunk by chunk, skipping what the runtime has:
section 5).

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
    u16  format_minor;         // 2
    u16  abi_major;            // runtime API version the pack needs (runtime-abi.md)
    u16  abi_minor;
    char profile[4];           // "GBA ", "SNES", "HOST"
    u32  total_size;           // whole pack in bytes
    u32  chunk_count;
    u32  toc_offset;           // usually 36
    u16  flags;                // bits 0-3: layout (section 2.6): 0 rom, 1 room_chain
    u16  reserved;
    u32  crc32;                // CRC-32 (IEEE) of everything after this header
};

struct BmChunkEntry {          // 24 bytes
    char id[4];                // chunk type, section 3
    u32  offset;               // from the start of the pack
    u32  size;                 // stored size
    u32  unpacked_size;        // size after decompression (= size when not compressed)
    u8   compression;          // section 4 (bits 0-5); bit 6 absent, bit 7 in base (§0)
    u8   category;             // streaming.md §3: level, graphics, objects, actors, ...
    u16  group;                // load group, section 2; 0 = global
    u32  hash;                 // CRC-32 of the unpacked data; used by the live link
};
```

Every reference inside a chunk is either a chunk index (`u16`, position in the table of
contents, which is fixed by the cooker and never changes when the file is re-laid out, §2.6)
or an offset relative to the start of that chunk. Absolute addresses MUST NOT appear
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

### 2.4 How content reaches the runtime

Two ways are supported in this version. Each uses its own layout (§2.6), recorded in the
header's `flags`:

| Way | Where the pack is | Consequences |
|---|---|---|
| `rom` | cartridge ROM; the only release mode | random access is cheap, so chunks are used **in place**: uncompressed tiles and maps go from ROM to VRAM by DMA, tables are read directly, only compressed chunks are unpacked. Nothing is copied to RAM just to keep it. Every asset exists exactly once |
| `stream` | on the PC, during development | the runtime was booted by multiboot or runs from a cartridge, and the PC sends chunks over the live link as described in [streaming.md](streaming.md). The GBA only holds what the current scene needs |

A multiboot image that carries all content in RAM is not a supported release mode; multiboot
exists only together with the live link.

### 2.5 File order for the live link (`room_chain` layout)

For streamed packs the chunks are written in **room chain** order ([streaming.md](streaming.md)
§1): global data first, then for each room in the chain the chunks it adds, followed by its own
level data, with an `RCHN` chunk at the start that lists the chain and each room's added and
dropped chunks. The PC reads the pack in that order and skips every chunk the GBA already has
or the session's load profile excludes. No asset is stored twice; a room that needs an asset
from far back in the chain references it by chunk index.

### 2.6 Content and layout are separate

The strategies above (ROM in place, room chain for the live link) are **layouts**, and a layout
can be changed or added without cooking again. A pack therefore has two independent parts:

- **Content**: the chunks themselves and the table of contents. The order of the table of
  contents is the **logical order**; it is fixed by the cooker, and chunk indices refer to it.
  Each chunk carries its category, group and hash. Nothing in the content depends on where a
  chunk lies in the file.
- **Layout**: where each chunk's bytes lie in the file (`offset` in the table of contents),
  the layout ID in the header, and optional layout metadata chunks (such as `RCHN`).

Rules:

- A reader MUST find chunks only through the table of contents and MUST NOT assume that
  chunks follow each other in any order.
- Readers that need a particular order (a sequential source) MUST check the layout ID and
  refuse layouts they cannot read efficiently, instead of misreading them.
- The tool `bmpack relayout <in> <layout> <out>` rewrites only the offsets, the layout ID and
  the layout metadata. Chunk contents, indices and hashes stay the same, so a re-laid-out pack
  is the same pack for the runtime, the live link (which compares hashes) and save games.

Layouts:

| Id | Name | Order | Used by |
|---|---|---|---|
| 0 | `rom` | any; the cooker groups global, area and room data for readability | cartridge release |
| 1 | `room_chain` | room chain order with `RCHN` (§2.5) | live link |
| 2 | `sequential` | reserved: one group after another in load order, for sources where seeking is slow (CF/SD on flash carts, as measured in SceneLoaderGBA) | not planned yet |
| 3–15 | – | reserved for future layouts | |

A new layout is added by defining its order and metadata here, adding it to `bmpack relayout`,
and, if the runtime reads it directly, adding a storage back end (see
[runtime-abi.md](runtime-abi.md) §7). The cooker and the content format do not change.

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
| `LITE` | the light of a lit room ([room-format.md](room-format.md) §1, §7) |
| `EFCT` | raster effect: per-line register tables |
| `SPRT` | one sprite: palette bank, tiles, frames as hardware sprite pieces, hitboxes, animations ([sprite-format.md](sprite-format.md)) |
| `SONG` | one song or sound effect: instruments, note events and its samples ([sound-format.md](sound-format.md)) |
| `SFX ` | sound effects |
| `MODB` | script module: classes, variables, states, functions and bytecode ([mondskript.md](mondskript.md) §11); one per class, in the order of the scripts' file names |
| `MODN` | the same scripts as machine code for one machine, one image with a module per `MODB`, with the list of addresses to patch when it is loaded ([mondskript.md](mondskript.md) §12); optional |
| `DTAB` | the records of one structure, read in place ([data-format.md](data-format.md) §3) |
| `FONT` | one font, or a part of one, section 3.1 |
| `LANG` | one language: translations of the pack's texts, section 3.2 |
| `MIGR` | the save layouts of earlier versions ([state.md](state.md) §7) |
| `WRLD` | world map: where the rooms lie, their cells' walls and doors ([world-format.md](world-format.md) §3) |

Readers MUST skip chunk types they do not know.

### 3.1 Fonts

A font is an image of glyph cells and a file `name.bmfont` next to it:

```xml
<font version="1" name="font" image="font.png" cell_w="8" cell_h="8">
 <range first="32" count="95"/>      <!-- code points, in the order of the cells -->
</font>
```

Cells are read row by row from the top left of the image; the ranges give their characters
(Unicode code points up to 0xFFFF; numbers decimal or `0x...`). Bright opaque pixels (alpha
and luminance from 128) are the ink, the rest is background; a drawn shadow is ignored, the
runtime adds its own. The font's name is the `name` attribute, else the file's stem. **One
font may be made of several files** with the same name and different characters (ASCII in
one, accented letters in another, kana in a third): each is a chunk, and a reader looks in all
of them. Two files of one font that share a character are an error. The chunk (category
graphics):

```
u16 name            string id
u8  cell_w, cell_h  8 and 8 (8 x 16 is allowed in the file; the runtime reads 8 x 8 only)
u8  2               version
u8  0
u16 range_count, glyph_count
u16 0
ranges { u16 first code point; u16 count; u16 first glyph; u16 0 } [range_count], by code point
u8  rows[glyph_count * cell_h]   per glyph one byte per row: bit x set = ink at pixel x
```

The runtime (`src/ui/ui.h`) makes a 4 bpp tile of a glyph when it is drawn: ink colour 1, a
shadow (colour 2) where the pixel above left is ink and the pixel is not, the ground
transparent or the frame's inside (colour 3). Only the glyphs on screen are tiles (a cache of
224), so a font can have more characters than video memory holds. The engine's font is
`assets/engine/font.bmfont` (ASCII) and `font_latin.bmfont` (the letters and quotes French and
German need), drawn by `tools/make_engine_font.py`.

### 3.2 Languages

The pack's strings (`STRS`) are the **main language**. A translation file per further
language, `xx.bmlang`, UTF-8:

```
# de.bmlang
language de "Deutsch"
"Short Sword" = "Kurzschwert"
```

Each line pairs a text of the main language, exactly as it is in the scripts and records, with
its translation (`\"`, `\\` and `\n` inside quotes; `#` starts a comment). A translation of a
text the pack does not have is reported as a warning. With at least one language file the
cooker writes a `LANG` chunk (category text) for the main language (code `en`, name `English`,
no entries) and then one per file, in the order given:

```
u16 code            string id ("de")
u16 name            string id ("Deutsch"), in that language
u16 count
u16 0
entries { u16 string id; u16 0; u32 offset of the translation in the chunk } [count], by id
texts               UTF-8, each ending in a zero byte
```

Languages are numbered in the order of the `LANG` chunks, 0 the main one. A text is shown in
a language through its string id: the entry with that id, else the main language's string
(`src/ui/text.h`). Text is UTF-8 on every machine; a character the font lacks is drawn as `?`.
The language chosen is an option, kept in the save memory apart from the save game
(runtime-core.md §6).

**Other scripts (Japanese).** Nothing in the format is Latin-only: a `ja.bmlang` with UTF-8
text works as it is, and the cooker reports, per language, the characters no font of the pack
has. What Japanese still needs: a font with kana and the kanji used (another `.bmfont` of the
font, ranges U+3040-U+30FF and the kanji), glyphs larger than 8 x 8 for kanji to be legible (the
file takes 8 x 16 cells; the runtime draws 8 x 8 only so far, a 16 x 16 glyph would be four
cache tiles), at most 224 different characters on one screen (the glyph cache), and line
breaks between any two characters rather than at spaces.

## 4. Compression

| Value | Method | Notes |
|---|---|---|
| 0 | none | |
| 1 | LZ77, GBA BIOS format | u32 `0x10 \| unpacked_size << 8`, then groups of a flag byte (high bit first) and eight items: a literal byte, or for a set flag two bytes: length - 3 in the top 4 bits, distance - 1 in the other 12 (copies may overlap). Unpacked into EWRAM with the BIOS (`LZ77UnCompWram`); `LZ77UnCompVram` would need distances of 2 or more |
| 2 | LZ4 block format | faster to decompress into EWRAM |

The cooker uses 1 for `TMAP` and `COLL` (section 0); 2 is not built.

## 5. Delta transfer

The live link (see [link-protocol.md](link-protocol.md)) compares `hash` values per chunk and
only sends chunks that changed (same hash and same size count as unchanged). Chunks that did
not change keep their content but MAY move; the receiver therefore rebuilds the pack from the
new table of contents and the chunks it already has.

## History

- 0.2: section 0, what is built, from the code.
- 0.1: first draft.
- 0.1.4: content and layout separated (§2.6); layout ID in the header; `relayout` tool;
  `sequential` layout reserved for CF/SD.
- 0.1.3: only `rom` (release) and `stream` (live link, multiboot); stream packs in room
  chain order with `RCHN`; chunk category in the table of contents; no copies in any mode.
- 0.1.2: storage modes `rom`, `ram`, `stream`; assets stored exactly once, copies only as an
  opt-in for streamed packs.
- 0.1.1: global, area and room levels; deduplication; dependency lists; fixed VRAM layout
  per area (`VLAY`).
