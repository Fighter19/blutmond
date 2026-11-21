# Plan: versioning and incremental updates

Status: plan, nothing built. Milestone M16 in [../PLAN.md](../PLAN.md).

## 1. What this is for

Today a change reaches a running GBA in seconds, but only into RAM
([../spec/runtime-abi.md](../spec/runtime-abi.md) §0). Getting a game onto a cartridge means
writing the whole image again, and nothing says which version a cartridge, a pack or a save
game is. This plan adds:

- **versions** for everything that can differ, and rules for what works with what;
- a **ROM layout and update format** in which a small change is a small write;
- ways to **apply** an update: by file, by a cartridge writer, or over the link cable to the
  cartridge in the GBA;
- **save games that survive** an update.

Facts about particular flash cartridges and writers in this plan are assumptions until tried
on the hardware at hand; section 9 lists what has to be measured first.

## 2. What has a version

| Thing | Version | Where it is stored | Changes when |
|---|---|---|---|
| Runtime | `major.minor.patch` and a build id (hash of the image) | a fixed block after the cartridge header; the header's version byte (0xBC) carries the patch number | the engine is rebuilt |
| Script API | major, minor = number of API entries | runtime and every script module | an entry is appended (minor) or changed (major) |
| Pack format | major, minor | pack header (there today) | the container changes |
| Link protocol | number | `HELLO` (there today) | packets change |
| Content | the project's `version = "1.2.0"`, a build number counted up by the cooker, and a content id (CRC over all chunk hashes) | a `META` chunk (listed in pack-format.md, not written yet) | anything is cooked |
| State layout | schema hash (there today) plus a schema number counted up when the hash changes | save game header | a state field changes |
| Chunk | hash (there today) | table of contents | its bytes change |

**Rules**, checked where things meet (most are carried today and not compared):

- Runtime and pack: pack format major equal; the pack's API major equal and API minor not
  above the runtime's. A pack that fails is refused with a message on screen and over the
  link, not started.
- Runtime and script module: the same API rule; for records, the layout hash
  ([structures.md](structures.md) §4).
- Editor and device: `HELLO` grows by runtime version, build id, content build number and
  content id. The editor shows them and warns when its own build of the runtime is newer
  than the one running.
- A newer runtime must run older packs of the same majors. That is the promise of
  [../PLAN.md](../PLAN.md) §6 ("fix engine bugs without rebuilding games"), and from now on
  a test keeps a pack of each released version and loads it.

**The build manifest.** Every cook and every ROM build writes a manifest next to its output
(`game.bmbuild`, text): the versions above, every chunk with name, hash, size and offset,
and a hash per flash sector of the image. It is what later builds diff against, and what
`bmlink` compares a device with. Released manifests are kept in the project
(`releases/1.2.0.bmbuild`), they are small.

## 3. A cartridge image that updates well

Flash memory is erased in sectors (commonly 64 KB or more) and erasing is the slow part.
Bits can be programmed from 1 to 0 without erasing. So:

```
0x000000  runtime                          padded to a reserved size (default 192 KB)
          directory A, directory B         one sector each
          base pack
          update pack 1, update pack 2 ... each starting where the previous one ended
          free space                       left erased (0xFF)
```

- **The runtime has a fixed, reserved size**, so a new runtime never moves the content.
- **Free space stays erased**, and padding inside the image is 0xFF, not 0. Appending data
  there needs no erase at all.
- **The directory** lists the packs in order (offset, size, CRC, content build number) and
  has a sequence number and a CRC of its own. There are two copies; an update writes the
  older one, and the runtime uses the valid one with the higher number. If power is lost
  during an update, the previous directory is still valid and the game starts as before.
- The sector size is a setting of the device profile; layout decisions are made in whole
  sectors.

### Pack sets

The runtime mounts the base pack and the update packs as one **pack set**. An update pack
has a complete table of contents for the new version of the game, but holds only the chunks
that changed; the entries of unchanged chunks point into an earlier pack of the set.

- This is the same idea as the live link's transfer (new table, copy what is already there:
  [../spec/link-protocol.md](../spec/link-protocol.md) §3), by reference instead of by
  copy, which is possible because ROM is read in place.
- Chunk indices stay what they are for the runtime: it reads the newest table and follows
  each entry to the pack that holds the bytes. Nothing else in the engine changes.
- Format change: a table entry gets a "source pack" number (pack format minor 2; four bits
  taken from the compression byte, 0 meaning this pack). The pack's CRC covers its table
  and its own chunks.
- An update for a changed script is its module (a few hundred bytes with one module per
  script, [attachable-scripts.md](attachable-scripts.md) §4), the string table if a name
  was added, and the table of contents: about 1 to 2 KB for the demo.

### Consolidating

Update packs pile up. When free space runs low, after 16 packs, or for a release, the cooker
writes a new base pack with a **stable layout**: given the manifest of what is on the
cartridge, every unchanged chunk keeps its offset, a changed chunk stays in place if it
still fits (chunks get a little slack, by default 1/16 of their size, at least up to the
next 256 bytes), and only what cannot stay moves to the end. The result differs from the
old image in as few sectors as possible. This is the `relayout` idea of
[../spec/pack-format.md](../spec/pack-format.md) §2.6 with "what is already there" as the
layout to keep.

## 4. Update files

`bmpack update old.bmbuild new.gba -o game-1.2.0-to-1.2.1.bmup`

```
"BMUP", format version
base:   runtime build id, directory sequence number, content id      (what it applies to)
result: content id, image CRC
sector size
operations, in order:
  program  offset, length, bytes        into erased space, no erase needed
  rewrite  sector offset, bytes         erase and write a whole sector
  directory bytes                       always last
```

- By default the tool makes an **append update** (one `program`, one `directory`). With
  `--consolidate` it makes a stable-layout update (`rewrite` for each differing sector).
- An update only applies to the exact base it names. `bmpack update` can chain or squash
  updates between any two kept manifests.
- For people who only have the ROM file, the same step also writes a standard binary patch
  between the two images, and the full image.

## 5. Getting it onto the cartridge

| Way | For | How | Cost of a small change |
|---|---|---|---|
| **RAM only** | iterating | the live link as today; nothing is kept | 0.4 to 3.4 s (measured, [../HARDWARE.md](../HARDWARE.md) §6) |
| **Over the link, into the cartridge** | development on a real cartridge; updating a friend's GBA | section 6 | the append: about 1 s plus transfer of 1 to 2 KB at 9 KB/s |
| **Cartridge writer on the PC** | writers that can address sectors | `bmflash apply update.bmup --device <profile>`; a device profile says how to call the writer's tool for "erase sector" and "write range" | the changed sectors |
| **File on an SD card** | flash cards that load a ROM file | copy `game.gba`; the editor's "Export" names it by version and can copy it to a mounted card | a file copy; incremental writing gains nothing here |

For SD cards the versions and the save rules of section 7 are what matters. For everything
that is real flash, the update file is.

## 6. Updating over the link

The cartridge's flash cannot be rewritten by code that runs from it. So the update is done
by a program in RAM: the **updater**, a small multiboot image with the link, the flash
driver and nothing of the game. It is sent like the game is today (`bmlink boot`), or the
running cartridge game copies its own updater part to RAM and jumps there when asked.

New packets, handled only by the updater:

| Packet | Content |
|---|---|
| `FLASH_INFO` | answer: chip identification, size, sector size, whether the chip is supported; the runtime version block and the valid directory |
| `FLASH_SUMS` | request: range; answer: a CRC per sector. The PC can find what differs without a manifest and without reading the cartridge out |
| `FLASH_ERASE` | sector |
| `FLASH_PROGRAM` | offset and bytes, which must lie in erased space; answered with a CRC read back from the cartridge |
| `FLASH_DONE` | start the cartridge game again |

`bmlink flash update.bmup` checks the base with `FLASH_INFO`, runs the operations, verifies
each by CRC, writes the directory last and restarts. `bmlink flash game.gba` without an
update file compares sector CRCs and writes what differs, which is also how a cartridge
with an unknown state is brought to a known one.

- **Flash drivers** sit behind a new part of the hardware interface
  (`bmHalFlashIdentify/Erase/Program`). Chips are recognised by their identification and
  only those tried on real cartridges are enabled; an unknown chip is reported and not
  touched.
- **The editor**: a "Keep on cartridge" button next to Run. Run pushes to RAM as now; Keep
  writes what was pushed as an update pack. With "Live" and "Keep" both on, every saved
  change is on the cartridge a few seconds later.
- **GBA to GBA**: a GBA with a newer build offers the update to another one over the link
  cable ([../spec/link-protocol.md](../spec/link-protocol.md) §9) at the 60 KB/s measured
  there, by the same packets, with the sending game as master.

## 7. Save games across versions

- The save header gets the schema number, the content build number and the runtime version
  that wrote it.
- **Schema history.** When the cooker sees the state layout change, it adds the new layout
  to `schema-history` in the project (field names, types, counts, flags). From two entries
  it derives the migration by itself: fields with the same name are copied (converted if
  the type grew), new ones get their defaults, removed ones are dropped. A rename, or
  anything cleverer, is a line the developer writes
  (`rename player.hp -> player.health`, `record ShortSword -> IronSword`). The migrations
  from every released schema to the current one are cooked into a `MIGR` chunk.
- On load: same hash, load; older and a migration exists, migrate and load; newer than the
  game, refuse with a message and leave the save untouched. This is the `hot_migrate` idea
  of [../spec/state.md](../spec/state.md), applied to saves first.
- Inventory entries whose record no longer exists are kept in the save and hidden, so
  going back to a version that has them loses nothing.
- A test loads a save of every released version in the current build.

## 8. Steps

1. Versions: the runtime version block, `META` chunk, build numbers, manifest; the
   compatibility checks with messages; `HELLO` extended; shown by `bmlink hello` and the
   editor.
2. Schema history, `MIGR`, save migration. Test: a save from the tag
   `milestone-gba-hardware` loads after a field was added.
3. Image layout with reserved runtime size, directories, 0xFF padding; pack sets in the
   runtime (source pack in the table). Test on host and in mGBA: base plus two update
   packs plays like the consolidated pack, same state checksums.
4. `bmpack update`, `.bmup`, stable layout and consolidation. Test: applying the update to
   the old image file gives the new image, byte for byte; a one-script change touches one
   program operation and the directory.
5. The flash interface with a model of a flash chip on the host (erase sets 0xFF, program
   can only clear bits, a write to a busy chip fails), the updater image, the `FLASH_*`
   packets, `bmlink flash`. Test against the model, including power loss at every step.
6. Real cartridges: identify, measure erase and program times, enable the chips that work.
   Editor: "Keep on cartridge", Export.

Done when: with a game on a flash cartridge in a GBA on the Xboo cable, a changed script is
written to the cartridge in under 5 seconds and is there after switching the GBA off and
on; no sector outside the appended range and the directory was erased; pulling the cable in
the middle leaves the previous version running; a save game made before the update loads
after it.

## 9. To find out first

- Which flash cartridges and writers are at hand, their chips, sector sizes, and erase and
  program times. Everything in section 3 is parameterised by the sector size, but the
  defaults (64 KB sectors, 192 KB for the runtime) should come from real parts.
- Whether the cartridges at hand let a program in RAM write them at all (some need an
  unlock sequence of their own, some only map part of the chip).
- Whether mGBA can stand in for a writable cartridge in tests, or the host model of step 5
  is the only automatic test.
- How large the updater is. The link and CRC code are about 6 KB today; the aim is under
  16 KB so it loads in two seconds.
