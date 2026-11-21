# Plan: versioning and incremental updates

Status: plan, nothing built. Milestone M16 in [../PLAN.md](../PLAN.md).

## Dependencies

Steps refer to section 8.

| Step | Needs |
|---|---|
| 1 to 4 (versions, save migration, pack sets, update files) | nothing from other plans |
| 5 (flash model, updater, `FLASH_*` packets) | nothing from other plans |
| 6 (real cartridges) | a flash cartridge and the answers of section 9 |
| small updates for one changed script | [attachable-scripts.md](attachable-scripts.md) step 1 (one module per class); without it an update carries the whole script module |

Needed by: [structures.md](structures.md) (renamed records) and [dialogue.md](dialogue.md)
(flag numbers) rely on step 2 to keep old saves loading.

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

What flash memory allows decides the design (sources and what is not verified: section 10):

- **Erasing** works on whole sectors only and sets them to 0xFF. Sector sizes differ by
  chip: 128 KB on the S29GL256 common in GBA cartridges, 4 KB on others (SST39VF). Erasing
  is slow (around half a second per sector on the large-sector chips) and is what wears
  flash out.
- **Programming** only clears bits and needs no erase if the place is still erased. It
  works word by word at any address.
- **A programmed unit should not be programmed again** before its sector is erased. Some
  chips protect each 32-byte unit with an error-correcting code, and a second programming
  of that unit switches the protection off for it. This is the alignment that matters: not
  for where a write may start, but for never touching a unit twice.

So the rules of this design:

1. An update is **appended into erased space**. Nothing that is already on the cartridge is
   modified to add it: not the old pack, not an index, not a "superseded" mark.
2. There is **no directory**. No sector is rewritten on every update, so none wears faster
   than the others.
3. Everything appended is made of **64-byte units, each programmed exactly once**.
4. An erase touches **one sector of the update area**, and only when the next update does
   not fit into what is still erased.

```
runtime            reserved size (default 192 KB, whole sectors); holds the layout block
update area        a ring of whole sectors, fixed place and size
base pack A
base pack B        optional second place for a base pack (consolidation, below)
free space         left erased
```

The **layout block** is part of the runtime image and never changes after the build: where
the update area is and how large, where base pack A and B are. The sector size is not in
it; the updater reads it from the chip (section 6).

### Update records

The update area holds a sequence of records. Each record is:

| Part | Size | Content |
|---|---|---|
| head | 64 bytes | `BMUR`, format, **sequence number** (counts up with every record), length of the whole record, kind, the base pack it builds on (offset and CRC), content build number, CRC of the head |
| body | a multiple of 64 bytes | kind "update": an update pack (below). Kind "base": nothing; the head names the base pack that is valid from now on |
| tail | 64 bytes | `BMUT`, the sequence number again, CRC of the body |

- Head, body and tail are separate units, written in this order. **A record counts only
  with a correct tail.** A record cut off by a lost cable or power is simply not there, and
  the one before it stays in use.
- A record starts on a 64-byte boundary. The first record of a sector starts at the
  sector's first byte. A record may run on into the following sectors; it does not wrap
  around the end of the area (it starts at the area's beginning instead).
- **An update pack is cumulative**: it has a complete table of contents for the new version
  and carries every chunk that differs from the **base pack**; all other entries point into
  the base pack. It never refers to an earlier update. So only the newest record matters,
  and every older one is free to be erased.

### Finding the newest record

At start the runtime does this, reading straight from ROM:

1. Read the 64 bytes at the start of every sector of the update area (4 to 64 sectors,
   section 9) and keep the valid head with the highest sequence number.
2. From there, step from record to record by the length in each head, as long as heads are
   valid and their numbers go up.
3. Take the last record whose tail is correct. Check its body's CRC. If that fails, take
   the one before.
4. No valid record: the base pack A alone is the game.

That is at most a few dozen reads of 64 bytes and one CRC over the newest update, well
under a frame for the sizes expected. The result (which base, which update pack) is kept
in RAM; the cartridge is not searched again.

Anything unexpected at a place where a head should be (not erased, not a valid head) ends
step 2; a newer record can then only start at a sector boundary, where step 1 has already
seen it.

### Adding an update

1. The place for the new record is right after the newest one, if the record fits into
   the rest of that sector and that rest is still erased.
2. Otherwise the record starts at the next sector of the ring (a record larger than a
   sector therefore always starts at a sector's first byte). The sectors it needs are
   erased first if they are not blank. They never hold the record in use: the area has at
   least two sectors and a record may be at most a quarter of it. An erase that was cut
   off leaves a sector that is not blank, and it is simply erased again next time.
3. Program head, body, tail. Read back and compare the CRCs.

So most updates program a few kilobytes and erase nothing. With 128 KB sectors and updates
of 2 KB, one sector is erased about every 60 updates; with 4 KB sectors, one per update,
but there an erase is quick and small. In both cases the erases go round the ring, so the
wear is spread evenly over the area.

### Pack sets

The runtime mounts the base pack and the newest update pack as one **pack set**:

- This is the same idea as the live link's transfer (new table, take from the old pack what
  did not change: [../spec/link-protocol.md](../spec/link-protocol.md) §3), by reference
  instead of by copy, which is possible because ROM is read in place.
- Chunk indices stay what they are for the runtime: it reads the update's table and follows
  each entry either into the update or into the base. Nothing else in the engine changes.
- Format change: a table entry gets a flag "in the base pack" (pack format minor 2). The
  update pack's CRC covers its table and its own chunks; the head names the base by CRC, so
  an update can never be combined with the wrong base.
- An update for one changed script is its module (a few hundred bytes with one module per
  script, [attachable-scripts.md](attachable-scripts.md) §4), the string table if a name
  was added, and the table of contents: about 1 to 2 KB for the demo.

### Consolidating

A cumulative update grows as the game moves away from its base. When it passes a quarter of
the update area, or for a release, a new base pack is written:

1. The new base goes into the base place that is not in use (B if A is, and the other way
   round), erased first. The current game keeps running from the old one.
2. The cooker lays the new base out **stably**: given the manifest of the old base, every
   unchanged chunk keeps its offset, a changed chunk stays in place if it still fits
   (chunks get a little slack), and only what cannot stay moves to the end. Sectors that
   come out identical are copied on the device from the old base to the new place; only the
   others travel over the cable.
3. A record of kind "base" is appended to the update area. From the moment its tail is
   written, the new base is the game. Until then the old one is, whatever happens.

A cartridge too small for two base packs can consolidate in place (erase and rewrite the
sectors that differ). That is not safe against a lost cable: the game may not start until
the update is run again, which the updater can always do because it runs from RAM.

## 4. Update files

`bmpack update old.bmbuild new.bmbuild -o game-1.2.0-to-1.2.1.bmup`

```
"BMUP", format version
applies to: runtime build id, base pack CRC
result:     content build number, content id
either      an update pack                    (the record is made on the device, which
                                               knows the sequence number and the place)
or          a new base pack, as a list of sectors: "same as old sector n" or the bytes
```

- An update is always made against the **base pack**, not against the previous update, so
  it applies to a cartridge whatever updates it has seen since that base.
- For people who only have the ROM file, the same step also writes a standard binary patch
  between two full images, and the full image. A full image is: runtime, an erased update
  area, the consolidated base pack.

## 5. Getting it onto the cartridge

| Way | For | How | Cost of a small change |
|---|---|---|---|
| **RAM only** | iterating | the live link as today; nothing is kept | 0.4 to 3.4 s (measured, [../HARDWARE.md](../HARDWARE.md) §6) |
| **Over the link, into the cartridge** | development on a real cartridge; updating a friend's GBA | section 6 | sending 1 to 2 KB at 9 KB/s and programming them; now and then one sector erase |
| **Cartridge writer on the PC** | writers that can write a range without erasing the chip | `bmflash apply update.bmup --device <profile>`: reads the update area, finds the place as section 3 does, writes one record | the same; depends on the writer |
| **File on an SD card** | flash cards that load a ROM file | copy `game.gba`; the editor's "Export" names it by version and can copy it to a mounted card | a file copy; incremental writing gains nothing here |

For SD cards the versions and the save rules of section 7 are what matters. For everything
that is real flash, the update record is.

## 6. Updating over the link

The cartridge's flash cannot be rewritten by code that runs from it. So the update is done
by a program in RAM: the **updater**, a small multiboot image with the link, the flash
driver and nothing of the game. It is sent like the game is today (`bmlink boot`), or the
running cartridge game copies its own updater part to RAM and jumps there when asked.

New packets, handled only by the updater:

| Packet | Content |
|---|---|
| `FLASH_INFO` | answer: chip identification, size, the sector layout, the largest buffered write, whether the chip is supported; the runtime's version and layout blocks; the newest record's head and where the next one would go |
| `FLASH_SUMS` | request: range; answer: a CRC per sector. The PC can compare a cartridge with an image without reading it out |
| `FLASH_APPEND` | an update pack, in parts. The updater builds the record: finds the place, erases the next sector of the ring if it has to, programs head, body and tail, and answers with the CRC read back |
| `FLASH_ERASE`, `FLASH_PROGRAM`, `FLASH_COPY` | one sector; bytes into erased space; a sector copied from another place on the cartridge. For base packs and for bringing a cartridge with unknown content to a known image |
| `FLASH_DONE` | start the cartridge game again |

`bmlink flash update.bmup` checks runtime and base with `FLASH_INFO` and sends the update.
`bmlink flash game.gba` compares sector CRCs and writes what differs.

**The flash driver** sits behind a new part of the hardware interface
(`bmHalFlashIdentify/Erase/Program`):

- It asks the chip for its layout (the Common Flash Interface: regions of sectors with
  count and size, and the size of the write buffer) instead of assuming one.
- Many cartridges have data lines 0 and 1 swapped, which changes every command value and
  every value read from the chip's own tables, but not the data. The driver detects it from
  the answer to the query (`QRY` reads as `RQZ`).
- Programming is word by word to start with. Buffered programming is faster and is used
  where the chip offers it; a buffered write must stay inside one aligned buffer block, so
  the driver cuts writes there. Records are aligned to 64 bytes anyway.
- Only command sets and chips tried on real cartridges are enabled. An unknown chip is
  reported and not touched.

**The editor**: a "Keep on cartridge" button next to Run. Run pushes to RAM as now; Keep
appends what was pushed as an update record. With "Live" and "Keep" both on, every saved
change is on the cartridge a few seconds later.

**GBA to GBA**: a GBA with a newer build offers the update to another one over the link
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
3. Pack sets in the runtime (table entries that point into the base pack); the layout
   block; update records and the search of section 3, reading from an image in memory.
   Test on host and in mGBA: base plus update plays like the consolidated pack, same state
   checksums; with a cut-off record, a torn head and a wrong body CRC the previous record
   is used.
4. `bmpack update`, `.bmup`, stable layout. Test: a one-script change gives an update of
   the expected size; base A to base B with identical sectors marked as copies.
5. The flash interface with a model of a flash chip on the host: erase sets a sector to
   0xFF, programming can only clear bits, programming a 64-byte unit twice is an error, the
   sector size is a parameter (4 KB, 64 KB, 128 KB), power can be cut after any word. The
   updater image, the `FLASH_*` packets, `bmlink flash`. Test against the model: a
   thousand updates in a row, counting erases per sector, with power cuts at random
   places; after each, the newest complete update or the one before is found.
6. Real cartridges (section 9): identify, measure, enable the chips that work. Editor:
   "Keep on cartridge", Export.

Done when: with a game on a flash cartridge in a GBA on the Xboo cable, a changed script is
written to the cartridge in under 5 seconds and is there after switching the GBA off and
on; the write erased nothing, or one sector of the update area; no byte outside the new
record changed; pulling the cable in the middle leaves the previous version running; a save
game made before the update loads after it.

## 9. To find out first

- Which flash cartridges and writers are at hand, and for each chip: its sector layout, its
  command set, whether lines are swapped, erase and program times.
- Whether those cartridges let a program in RAM write them at all (some need an unlock
  sequence of their own, some map only part of the chip, some keep save data in the same
  chip, which must then stay clear of the update area).
- Whether programming the erased rest of a partly used sector is reliable on each chip
  (it is what flash is specified to do, but cheap cartridges are worth a test of a
  thousand appends with read-back).
- Whether mGBA can stand in for a writable cartridge in tests, or the host model of step 5
  is the only automatic test.
- How large the updater is. The link and CRC code are about 6 KB today; the aim is under
  16 KB so it loads in two seconds.
- Default sizes. With 128 KB sectors the update area needs at least two sectors; four
  (512 KB) is the proposal. With 4 KB sectors, 16 to 64 sectors.

## 10. What was checked

Checked against data sheets and the gbadev documentation when this plan was written
(November 2026). The data sheets were read as search excerpts, not in full:

| Statement | Source |
|---|---|
| Programming only changes bits from 1 to 0; erasing a sector sets it to 0xFFFF | S29GL-S data sheet; gbadev "Bootleg Carts" |
| S29GL-S: buffered programming of up to 512 bytes, inside one aligned 512-byte line; 1 to 256 words anywhere in it | S29GL-S data sheet |
| S29GL-S: a 32-byte page has an error-correcting code; programming a page more than once switches it off for that page until the sector is erased | S29GL064S and S29GL-S data sheets |
| S29GL256 as used in GBA cartridges: 32 MB, sectors of 0x20000 (128 KB) | the FlashGBX profile quoted in a write-up on Chisflash cartridges |
| SST39VF640x: uniform sectors of 2 KWord (4 KB), blocks of 32 KWord, programming word by word | SST39VF640x data sheet |
| MX29GL256F: sector erase 0.5 s typical, 3.5 s at most | Macronix data sheet |
| The chip reports its sector regions and its write-buffer size (2^N bytes at query offset 0x2A, regions from 0x2C) | CFI publication 100; U-Boot `cfi_flash.h` |
| Many GBA cartridges swap data lines 0 and 1 (commands and query answers change, data does not); flash code must run from RAM; cartridges are 4 to 32 MB | gbadev "Bootleg Carts" |

Sources: [S29GL064S data sheet](https://www.infineon.com/assets/row/public/documents/10/49/infineon-s29gl064s-64-mbit8-mbyte3-datasheet-en.pdf),
[S29GL-S data sheet](https://www.farnell.com/datasheets/2785316.pdf),
[MX29GL256F data sheet](https://www.macronix.com/Lists/Datasheet/Attachments/8527/MX29GL256F,%203V,%20256Mb,%20v1.5.pdf),
[SST39VF640x data sheet](https://www.farnell.com/datasheets/9872.pdf),
[CFI publication 100](https://freecalypso.org/pub/embedded/flash/cfi_100_20011201.pdf),
[gbadev: Bootleg Carts](https://gbadev.net/gbadoc/bootleg-carts/introduction.html),
[Chisflash write-up](https://gitendo.gitlab.io/blog/posts/writing-32mb-eeprom-to-chisflash/).

Not checked, and marked as assumptions above: how many erase cycles each chip is specified
for (data sheets of this kind usually say around 100,000 per sector); sector erase time of
the 4 KB chips; which chips are in the cartridges at hand; whether any of them forbids a
second programming of a larger unit than 32 bytes (the 64-byte rule would then grow);
writers' abilities; mGBA.

With those numbers: one erase per sector for every turn of the ring. At 2 KB per update and
four sectors of 128 KB that is about 250 updates per turn, so 100,000 cycles would be 25
million updates. A directory sector rewritten on every update would have been worn out
250 times sooner.
