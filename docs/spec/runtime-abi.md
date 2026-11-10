# Runtime ABI and live reload

Version 0.1, draft.

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
  while game logic is paused or stuck in a loop.
- A watchdog (timer interrupt checking a frame counter) reports a hang over the link.

## 4. Reload modes

A reload happens at a frame boundary: stop the frame, mute sound, blank the screen, apply,
restart. It is only applied after all data for it has arrived and been checked (see
[link-protocol.md](link-protocol.md)); a failed transfer leaves the running game untouched.

| Mode | Data | Effect on the state |
|---|---|---|
| `restart` | changed chunks | state arena reset to defaults; start at the given room and `PlayerStart` (or save point) |
| `hot` | changed chunks; schema hash unchanged | state kept; the current room is reloaded around the player |
| `hot_migrate` | changed chunks + migration table | state converted with the table (see [state.md](state.md)), then like `hot` |
| `module` | one script module | module replaced in its slot; entities whose current state code changed restart that state at its beginning |
| `state` | state arena or part of it | the given state is loaded, the room is reloaded |

## 5. Native modules

Native (AOT) modules can write anywhere and are therefore not protected by the rules above. In
development the editor uses verified bytecode by default; native modules are for performance
tests and release builds. If a native module breaks the runtime, recovery needs a reset (see
the reset setting in [link-protocol.md](link-protocol.md)).

## History

- 0.1: first draft.
