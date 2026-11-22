# Runtime ABI and live reload

Version 0.2, draft.

## 0. What is built

Most of this document is the design. Built so far (`src/link/live.h`,
`apps/game/game_live.c`):

- **Two content buffers.** A pack pushed over the link is put together in the buffer the
  running pack does not use ([link-protocol.md](link-protocol.md) §3). After a successful
  `APPLY` that buffer holds the running pack and the other one is free for the next. The
  first pack is the one built into the image and lies in neither.
- **The region table** has four entries: the link's own memory (runtime), the two content
  buffers and the buffer for a pushed state (content). Nothing reads it yet.
- **What the game provides** (`BmLiveHooks`): its state blocks ([state.md](state.md));
  "use this pack" for the modes restart and hot; "state was loaded, rebuild what is derived";
  and frame, room and position for `STATUS`.
- **restart**: the game starts over with the new pack ([runtime-core.md](runtime-core.md) §6,
  "start"), in the room whose ID was given or in room 0. If that fails, the old pack is
  started again and the answer is "pack refused".
- **hot**: the state is kept. The current room is looked up by its ID in the new pack (not
  there: refused, nothing changes); then everything derived is rebuilt as after loading a
  state ([state.md](state.md) §5): the room, video memory, sprites, the script module (it
  is verified again), the song. If that fails the old pack is put back the same way.
- **state**: the pushed state is read into the game's blocks and the same rebuilding follows.
  A state of another layout is refused before anything is changed.
- Pack versions: only `format_major` is checked. `abi_major`, `abi_minor` and the profile are
  carried and reported in `HELLO`, and nothing compares them yet.

Not built: the API table of section 1 (scripts call the runtime by the numbers in
[runtime-core.md](runtime-core.md) §8; the module's header says how many it knows), handles
(section 2; objects are referred to by their index in the room), the residency table
(section 4; a room change loads the whole room anew, palette and tiles included), the watchdog, the mode `hot_migrate`, and storage back
ends (section 7; a pack is always one block of memory).

The runtime is loaded once (multiboot or cartridge). Content (packs, script modules, state)
can be replaced while it runs, without a reset. This document defines the contract that makes
that possible.

## 1. API table

The runtime exports one table of function pointers, `BmApi`, like a syscall table.

```c
struct BmApi {
    u16 abi_major;
    u16 abi_minor;
    u32 function_count;
    void *functions[];        // in a fixed, documented order
};
```

- Script modules (bytecode and native) and packs reach the runtime **only** through this table.
  A pointer to it is passed to a module when it is loaded.
- New functions are only appended; `abi_minor` goes up. Removing or changing a function bumps
  `abi_major`.
- A runtime MUST load packs and modules with the same `abi_major` and an `abi_minor` lower than
  or equal to its own.

## 2. Handles

References from the runtime to content, and between entities, are handles:

```
u16 index | u16 generation      (32 bits)
```

The generation of a slot goes up whenever the slot is freed and on every content reload. A
handle whose generation does not match is invalid; using it is a checked error (it never
touches the wrong object). The runtime MUST NOT keep raw pointers into content across frames.

## 3. Memory regions

The runtime keeps a region table. Every byte of RAM, VRAM, palette and OAM belongs to exactly
one region:

| Region | Owner | On "restart content" |
|---|---|---|
| runtime code and data | runtime | kept |
| live-link handler, IRQ table, stack | runtime | kept |
| content area (packs in RAM) | content | replaced |
| script module slots (IWRAM, EWRAM) | content | unloaded, refilled |
| state arena | content | reset to defaults, or kept/migrated depending on the mode |
| VRAM, palettes, OAM, line tables | runtime, filled from content | cleared and rebuilt by the room loader |
| sound voices, mixer buffers, sound DMA and timers | runtime | voices stopped, mixer reset |
| caches (tile cache, collision changes) | runtime | dropped |

Rules:

- Content MUST NOT register interrupt handlers or use DMA channels or timers directly; it asks
  the runtime through the API table. On reload the runtime can therefore switch all of them off.
- The live-link handler runs from the serial interrupt with high priority, so the link works
  while game logic is paused or stuck in a loop. The interrupt only receives words and keeps
  whole packets in a few buffers; the packets are handled at the next frame boundary
  (`bmLiveFrame`). While the game does not reach one, the buffers fill and the runtime answers
  "busy" ([link-protocol.md](link-protocol.md) §3), so nothing is lost; handling packets while
  the game hangs (memory reads, `WATCHDOG`) needs a second, lower-priority context and is
  future work.
- Implementation (version 0.2, `src/link/live.h`): the region table lists the link's own
  memory (runtime) and two content buffers and the state buffer (content). Packs arrive in the
  buffer the running pack does not use; after a successful `APPLY` the buffers swap roles.
- A watchdog (timer interrupt checking a frame counter) reports a hang over the link.

## 4. Room changes and residency

The runtime keeps a **residency table**: which chunk is loaded where (RAM, VRAM tiles, palette
banks, OBJ VRAM), with the chunk's hash. On a room change:

1. the new room's needs are its own group plus its `deps` list;
2. chunks that are already resident with the same hash stay where they are, with no upload;
3. missing chunks are loaded, preferably while the door transition fades out;
4. chunks no longer needed are freed: room level always, area level when the area changes,
   global level never.

Moving between rooms of the same area therefore only uploads the new room's maps, collision
and room-specific tiles. Entering another area also loads its `VLAY` and shared graphics.

With streamed content ([streaming.md](streaming.md)) the PC keeps a mirror of this table and
decides what is loaded and dropped; the runtime follows `DROP` and reports `STATUS`.

The same table drives live reloads: when a shared chunk changes, every room that depends on it
sees the new version, and only that chunk is sent and uploaded.

## 5. Reload modes

A reload happens at a frame boundary: stop the frame, mute sound, blank the screen, apply,
restart. It is only applied after all data for it has arrived and been checked (see
[link-protocol.md](link-protocol.md)); a failed transfer leaves the running game untouched.

| Mode | Data | Effect on the state |
|---|---|---|
| `restart` | changed chunks | state arena reset to defaults; start at the given room and `PlayerStart` (or save point) |
| `hot` | changed chunks; schema hash unchanged | state kept; the current room is reloaded around the player |
| `hot_migrate` | changed chunks + migration table | state converted with the table (see [state.md](state.md)), then like `hot` |
| `module` | changed chunks (usually one script module and the strings) | like `hot`, but scripted objects are looked at one by one: an object whose class's module did not change keeps everything; one whose module changed but whose variables are the same (names, types, flags) keeps its variables and starts the code of its state (found by name, else the start state) from the beginning; any other starts anew |
| `state` | state arena or part of it | the given state is loaded, the room is reloaded |

Version 0.2 implements `restart`, `hot` (the current room is found again by its ID in the new
pack; scripted objects start anew if any script changed), `module` and `state`;
`hot_migrate` answers "mode not supported". The editor pushes with `module`.

## 6. Native modules

Native (AOT) modules can write anywhere and are therefore not protected by the rules above. In
development the editor uses verified bytecode by default; native modules are for performance
tests and release builds. If a native module breaks the runtime, recovery needs a reset (see
the reset setting in [link-protocol.md](link-protocol.md)).

## 7. Storage back ends

The runtime reads content through one interface, so new sources can be added without touching
the loaders:

```c
struct BmStorage {
    u16  layout;                                        // layouts it can read (bit mask)
    int  (*open)(void *ctx, const struct BmPackHeader *h);
    int  (*read_chunk)(void *ctx, u16 index, void *dst, u32 offset, u32 size);
    const void *(*map_chunk)(void *ctx, u16 index);     // in-place access, or NULL
};
```

Back ends in this version: `rom` (maps chunks in place, any layout) and `link` (chunks arrive
from the live link). A CF/SD back end would read the `sequential` layout
([pack-format.md](pack-format.md) §2.6) and use a small read buffer.

## History

- 0.2: section 0, what is built.
- 0.1: first draft.
- 0.1.1: residency table and room changes (§4).
- 0.1.2: storage back end interface (§7).
