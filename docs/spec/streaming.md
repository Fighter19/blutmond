# Content streaming

Version 0.1, draft.

How content reaches the runtime when it does **not** come from a cartridge ROM. Multiboot is
only supported in this context: the runtime is booted over the link (see
[link-protocol.md](link-protocol.md) §2) and the PC then **streams** content to it. A multiboot
release that carries all content in RAM is not a supported mode; releases use ROM packs
([pack-format.md](pack-format.md) §2.4).

Goals:

- keep the RAM footprint small: only what the current scene needs is on the GBA;
- send little over a slow link (estimated 10–30 KB/s over the Xboo cable);
- avoid switching the direction of the link: the PC pushes, the GBA does not ask asset by
  asset;
- make it possible to load only parts of the game (only the level, one asset).

## 1. Rooms as anchors

Most rooms share most of their assets; what differs between neighbouring rooms is mainly the
level data (maps, collision, objects) and a few assets. Streaming is therefore planned around
**rooms as anchor points**:

- For every room the cooker knows its **asset set**: every chunk the room needs, split into
  categories (§3).
- Rooms are put into a **room chain**: an order in which neighbouring rooms follow each other
  (following doors, the world map and, where known, the intended play order). Each room in the
  chain stores only its **delta** to the previous one: chunks to add, chunks that can be
  dropped, and its own level data.

```
room A: full asset set + level data A
room B: +{enemy sprite X}  −{song 1}  + level data B
room C: +{}                −{}        + level data C          (same assets as B)
room D: +{tileset 2, song 2} −{tileset 1} + level data D       (next area)
```

Walking along the chain only transfers the deltas. Jumping to a room that is not the next in
the chain falls back to the difference between what the GBA has and that room's asset set
(§2), so the chain is an optimisation, never a requirement.

## 2. The PC drives, the GBA reports

The PC keeps a **mirror of the GBA's residency** (what is loaded where, with hashes; see
[runtime-abi.md](runtime-abi.md) §4) and the room graph. It decides what to send; the GBA does
not request assets one by one.

- The GBA reports only compact **status** when polled: current room, player position (coarse),
  free memory per region, and events such as "entered door to room X" or "out of memory".
- The PC computes the difference between the mirror and what is needed, orders it by priority
  (§4) and sends it as one continuous stream of chunks.
- Acknowledgements are collected per **window** of packets ([link-protocol.md](link-protocol.md)
  §4), not per packet, so the link only turns around once per window instead of once per
  packet.
- Status from the GBA is picked up while polling for those window acknowledgements, so there is
  no extra round trip for it.

## 3. Categories and load profiles

Every chunk belongs to one category:

| Category | Content |
|---|---|
| `level` | tile maps, collision, custom shapes, raster effects of a room |
| `graphics` | tilesets, palettes, VRAM layout |
| `objects` | object placement (`OBJS`) |
| `actors` | classes, sprites and animations of enemies and NPCs |
| `player` | player class, sprites, animations, HUD |
| `scripts` | script modules other than actors and player |
| `music` | songs, instruments and samples used only by songs |
| `sfx` | sound effects and their samples |
| `text` | strings, dialogue, fonts |

A live-link session selects a **load profile**; categories outside it are skipped by the PC and
never sent:

| Profile | Categories | Use |
|---|---|---|
| `view` | `level`, `graphics` | look at a room on the device: scroll with the D-pad, optional collision overlay |
| `walk` | `view` + `player`, `sfx` | walk through rooms and test collision and physics |
| `objects` | `walk` + `objects`, placeholders for actors | see where objects are; actors whose class is not loaded are drawn as boxes |
| `full` | everything | play |
| custom | any combination | e.g. `full` without `music` to save RAM and link time |

The runtime MUST run with any profile: missing classes become placeholders, missing songs are
silent, missing text shows its string ID.

## 4. Priorities

Within what the profile allows, the PC sends in this order:

1. what the **current room** needs to be shown: VRAM layout, palettes, tiles, maps, collision;
2. what it needs to be played: player, objects, actor classes of objects in or near the view;
3. sound: SFX, then the room's song;
4. **prefetch**: deltas of the rooms reachable through the current room's doors, nearest door
   first (by distance from the player), as far as free memory allows;
5. nothing else. Rooms further away are not preloaded.

When the player goes through a door, the transition starts as soon as priority 1 of the new
room is complete; the rest follows while the room is already visible.

Eviction on the GBA follows the reverse order: first prefetched data of rooms the player moved
away from, then data of the previous room not needed by the current one. Data of the current
room is never evicted. The PC tells the runtime what to drop; its mirror stays exact.

## 5. Testing single assets

Every asset MUST be testable on its own, on the host and on the device, without loading a
game:

| Asset | Test scene |
|---|---|
| tileset, palette | tile viewer with palette banks, as cooked for the profile |
| sprite, animation | animation player on a neutral background, hitboxes shown |
| room | the room in profile `view` or `walk` |
| actor class | the class in a **test room** (flat floor, walls, the player optional) with its `defaultproperties` or chosen properties |
| song, instrument, SFX | sound player with the mixer's CPU and memory use |
| raster effect | effect on a test pattern |

The cooker can build a **mini pack** for one asset plus its dependencies (the test room and
the player come from a small built-in test pack). Over the live link the editor sends it with
the packet `TEST` ([link-protocol.md](link-protocol.md)), and the runtime opens the matching
test scene. On the host, the same mini pack runs in the host build, so tests can run in CI.

## 6. Size and RAM accounting

The editor shows for the current room and profile: bytes to send, expected time on the current
transport, RAM and VRAM used on the GBA, and what prefetching would add. It warns when a room
cannot be shown with the selected profile in the available memory.

## History

- 0.1: first draft.
