# Using Blutmond

How to build, run and connect things. The reasons behind the link's behaviour on a real GBA
are in [HARDWARE.md](HARDWARE.md); formats and protocols are in [spec/](spec/README.md).

## 1. Build

Packages (Ubuntu 24.04) are listed in the [README](../README.md). Then:

```
cmake -S . -B build-gba -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gba.cmake -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-gba
cmake -S . -B build -G Ninja -DBM_GBA_BUILD_DIR=$PWD/build-gba
cmake --build build
ctest --test-dir build
```

The GBA build comes first because the host build embeds the path of the multiboot image for
the editor and runs the GBA images in mGBA as tests. Without `-DBM_GBA_BUILD_DIR` the host
build still works; the emulator tests and the editor's GBA target are then missing.

What you get:

| File | What it is |
|---|---|
| `build/bmeditor` | the editor |
| `build/game` | the game on the PC |
| `build/bmcook` | the cooker: rooms, sprites, scripts and songs into a pack |
| `build/bmlink` | the link tool: boot, push, state, sync, GDB |
| `build-gba/game.gba` | the game for a cartridge or an emulator |
| `build-gba/game_mb.gba` | the game as a multiboot image (what is sent over the cable) |
| `build-gba/game_runtime_mb.gba` | the runtime alone as a multiboot image: no level inside, it waits for one over the cable (what the editor boots) |

## 2. Play on the PC

```
build/game                      # the test level in a window
build/game level.bmpk           # a pack of your own
```

Keys: arrows to move, X or Space is A (jump), Z is B, A and S are L and R, Return is Start,
Backspace is Select, F11 full screen, Escape ends (in the browser it only leaves full screen). Gamepads work. The window keeps a save game
in a file and listens for the link on `127.0.0.1:7414`.

Start opens the menu: the player's status, Equipment, Items and System (the language: English,
German, French). The test level's third room, behind a step at the east end of the second, is
the boss hall.

Holding L and R and pressing Select shows or hides the debug overlay (room, game frame,
position, health, and on the GBA how much of the frame the game took); `--overlay` shows it
from the start.

**The debug lobby**: holding L and R and pressing Start gives the Debug Mirror (Items). Using it
teleports, with a wave, to a lobby with four doors: Wave, Spotlight, Pulse and Dusk, a room
for each screen effect, and a teleporter at the east end back to the first room. `--room ID`
starts in that room (`--room debug_lobby`), `--debug-item` puts the mirror in the bag at the
start.

Sounds: each enemy struck sounds its own (an `Enemy` record's `hit_sound`: the bats squeak; the
moon bat and the training dummy name theirs in their scripts), an item picked up chimes (the
effect `pickup`, played by the engine), and the defeated boss gets a fanfare.

Every weapon has a list of **commands** (directions, then a button, like Down,
Down-Fwd, Fwd + B for the Short Sword's Lunge), and a charm worn gives its commands with any
weapon (the Crimson Charm absorbs blood, the Moon Charm flips gravity). While attacking the player cannot walk, but a run's speed fades rather than stopping, and in the air the player steers; each weapon's record says otherwise if wanted (`ground_move`, `air_move` in the Data tab). Up + B strikes above, Down + B sweeps low, and in the air stabs downwards (bouncing off what it hits). Start, Commands lists
what the player has now. The debug lobby has a Dagger and both charms lying about to try them.

**Abilities**: the boss hall has a door east into the Hall of Abilities. Its pedestals give
Double Jump (a jump in the air), Second Wind (a blow that would end you leaves 1 health),
Dash (L), Wall Jump (hold against a wall while falling to slide, jump to leap off) and Ground
Pound (Down and Jump in the air), each behind the obstacle the one before overcomes.

**The map**: the top right corner shows the rooms around the player as cells of a map, one
cell a screen, with their walls and doors (orange); the room the player is in is lighter, the
yellow square is where they are. Only cells the player has been in are shown; the save game
keeps them. Start, Map shows all of it and how much of it is explored. Where the rooms lie
comes from the project's world map (the World tab of the editor, section 3); a room that is
not on it (the debug rooms) has no small map.

Doors with a `panel` property (a sprite) open in a 3D look only as the player goes through
them: touching a side door, the player walks on through it while it swings into the room, a
door in the back wall opens in front of the player when Up is pressed in it; then the next room, where the door closes
behind. A, B or Start skips the walk. The panel is an affine sprite, scaled horizontally about
its hinge, standing on the ground. All doors of the test level have one (`door_panel`).

Without a window (for tests and scripts): `--frames N`, `--headless`, `--autoplay`,
`--snapshot-at N file`, `--load file`, `--listen PORT`, `--follow`, `--until-frame N`,
`--realtime`, `--random-keys N`. The comment at the top of `apps/game/main.c` describes them.

**Save games across versions**: before publishing a version whose save games should keep
loading after later changes, add its save layout to the project's history:
`build/bmschema --add assets/test_room/save.bmschema "1.0"` (built with the engine of that
version). A save of a layout in the history loads in later versions; one of an unknown layout
is refused and left untouched.

## 3. The editor

```
build/bmeditor                      # opens assets/test.bmproj
build/bmeditor game.bmproj
```

Options: `--theme classic|light|dark` (also in the View menu, remembered), `--target pc|gba`,
`--xboo device`, `--reset none|xboo`, `--gba-image file`, `--bmlink file`, `--game file`,
`--port N`. The last six can also stand in `~/.config/blutmond/editor.conf` as `key = value`.
`--action name` runs a menu action at the start (`help.version`, `run.start`, ...), `--room N`
shows the project's room N, `--record Name` opens a record in the Data tab, `--open FILE` opens a
sprite, script, song or record file in its tab, `--object-menu N`
opens the context menu of object N of the room shown, and with `--frames N --screenshot
out.ppm` the editor draws N frames, saves the picture and quits.
**Help > Version** shows the commit the editor was built from (marked if the tree had changes
not committed) and when that commit was first built there; the published pages show the
commit they were built from.

- **Run** (F6) cooks all rooms and plays them on the target next to the button: **PC** (the
  game window) or **GBA (Xboo)** (section 4).
- **Live**: every cook while something runs (a saved file, F5) is pushed in as a reload (mode
  `module`);
  the player stays where they are.
- **Sync** (Off, GBA > PC, PC > GBA): section 6.
- **Stop** ends the PC game and any sync. The GBA keeps running.
- **Export** (Ctrl+E, also in the File menu) cooks all rooms and writes the game as one GBA
  image, by default `<project name>.gba` next to the project file. It plays in any emulator
  (mGBA, NanoBoyAdvance, ...) and can be written to a flash cartridge. It is the cartridge
  image `build-gba/game.gba` with the pack appended; the editor finds it through the GBA build
  (`-DBM_GBA_BUILD_DIR`), `gba-rom` in `editor.conf` or `--gba-rom file`. On the command line
  the same is `build/bmexport build-gba/game.gba level.bmpk out.gba`. For real hardware the
  logo is still missing from the header (section 4): `tools/gbafix.py out.gba --logo FILE`.
- The tabs (Room, Animation, Script, Sound, Graphics, Data, World, and Project and State on
  the left) are described in the [README](../README.md).
- **Animation**, figures ([spec/sprite-format.md](spec/sprite-format.md) "Figures"): a sprite
  frame's **points** (where the layers of a figure hang) are listed under the boxes: add one,
  set its place, angle and whether what hangs there is in front or behind, drag it in the
  preview, or copy it to every frame of the animation shown. An animation can be a
  **direction set** (from, to): its steps are directions for aiming; move the **dial** to see
  which step an angle picks. Choose a `.bmfigure` (in the sprite chooser or the Project panel)
  to see the figure: its layers put together as the game does, playing the first layer's
  animations, the aiming layers turned to the dial, and what the pose and the figure cost. The
  player of the test level is `player.bmfigure`; `tools/split_sprite.py` makes legs and torso
  from a sprite drawn whole.
- **World**: the world map ([spec/world-format.md](spec/world-format.md)). The rooms lie on a
  grid of screens as the game's map shows them; rooms without a place of their own are put
  next to the room their door leads from (drawn lighter, "(doors)"). Drag a room to give it a
  place, drag the ground to look around, turn the wheel to zoom. The list on the right has
  every room: hide one from the map (debug rooms), put one that is not on the map next to it,
  give one back to its doors. "Fix all where they are" writes every place into the file. A
  red line marks a door whose room is not next to it on the map. New world map makes the file
  for a project that has none. Changes are saved at once and, with Live, the running game's map
  changes.
- **Data**: the records of the project, a table at a time (items, enemies, speakers, lines of
  dialogue). A record's fields are a form: texts and numbers are typed (Enter or leaving the
  field saves), enums, records, sprites and yes/no are chosen; a field the record does not set
  shows in grey what it gets from its base record or the default, and choosing "(...)" or
  emptying a field gives it back. Every change is written to its line of the `.bmdata` file
  and cooked, and with Live it reaches the running game. The preview shows the sprite a record
  names; for a line of dialogue the speaker's portrait, name and words.
- **Right click** (the context menu): on an entry of the Project panel, where to edit it (the
  Animation, Script, Sound or Data tab, the Room tab for a room), open it with the system's
  program, delete it; on an object in the Room view, its script and the records it uses (the
  ones its properties in Tiled name and the ones its script names, such as the lines it says
  and their speakers); on a record in the Data tab, new, duplicate and delete. A right drag in
  the Room view still moves the view.
- The window can have any size: the dividers between the project panel, the tabs, the side
  panels and Report/Messages can be dragged; side panels keep their width when the window
  grows; toolbars wrap onto a second row when they do not fit; tall side panels scroll. The
  menu, the toolbar and the status bar always stay in view; in a window narrower than about
  1000 pixels the area between them scrolls sideways instead of cutting panels off.

Messages from the tools appear in the Messages list at the bottom. A job that has to wait for
another one says so there.

## 4. A real GBA over the Xboo cable

### Once

- **The port.** `bmlink` needs read and write access to the parallel port device:

  ```
  sudo usermod -aG lp $USER        # then log in again
  ```

  or, until the next reboot, `sudo setfacl -m u:$USER:rw /dev/parport0`.
- **The logo.** A real GBA only starts an image with the logo in its header, and the logo is
  not in this repository. Put the 156 bytes, or any GBA image that has them, where `bmlink`
  looks: `~/.config/blutmond/logo.bin` (or pass `--logo FILE`). xcomms' source has the bytes
  in `good_header`.
- **The cable.** Four wires are needed (SC, SI, SO, ground). Do not push the plug further
  into the GBA than it goes by itself: it then loses contact on SO.

### Each time

1. Switch the GBA on without a cartridge (or with Start and Select held), so it waits at its
   BIOS screen.
2. Boot the game and talk to it:

   ```
   build/bmlink --xboo /dev/parport0 boot build-gba/game_mb.gba hello
   ```

   `boot` keeps asking for 20 seconds (`--wait N`), so you can also start it first and then
   switch the GBA on. At about 9 KB/s the 83 KB take roughly ten seconds.

From then on the GBA does not need to be touched: a `boot` while the game runs asks the
runtime to restart the GBA into its BIOS first. `bmlink reset` does only that.

In the editor: choose **GBA (Xboo)** and press **Run**. If no runtime answers, it boots one
(you have a minute to switch the GBA on), then pushes the level. It boots
`game_runtime_mb.gba`, which has no level of its own: the cable carries the runtime once
and the level once (with `game_mb.gba` the test level inside it went over the cable first,
and then the level was sent again). While the GBA waits for a level, or one arrives in
place of the running one, it shows a loading box: waiting for the PC, then how many KB and
chunks are there. Run does not replace a
runtime that is already running; after rebuilding the runtime, boot it once by hand as above.

### What bmlink can do

```
build/bmlink --xboo /dev/parport0 hello                       # what runs there
build/bmlink --xboo /dev/parport0 ping 5 stats                # speed and error counters
build/bmlink --xboo /dev/parport0 push level.bmpk restart     # a new level, from the start
build/bmlink --xboo /dev/parport0 push level.bmpk hot         # ... keeping the game's state
build/bmlink --xboo /dev/parport0 push level.bmpk module      # ... and the scripts that did not change
build/bmlink --xboo /dev/parport0 state-get now.bmst          # the game state into a file
build/bmlink --xboo /dev/parport0 state-put now.bmst          # and back
build/bmlink --xboo /dev/parport0 ppu-snapshot vram.bmps      # the video state (11 s)
build/bm_ppu_render vram.bmps screen.ppm                      # ... as a picture
build/bmlink --xboo /dev/parport0 reset                       # back to the BIOS
```

Several commands can follow each other in one call. Ctrl-C ends `bmlink` between two words
and gives the port back. Only one `bmlink` can use the port at a time; a second one says so.

The same commands work on the PC game with `--tcp 7414` instead of `--xboo ...`, and on an
image in the emulator with `--mgba build-gba/game.gba` (there also `run N`, `check`,
`screenshot`). The comment at the top of `tools/bmlink/bmlink.c` lists everything.

A multiboot GBA has about 84 KB for pushed packs; the whole test level is 90 KB, so
`game_mb` carries it without its debug rooms, the Hall of Abilities and the translations (the cooker's `--absent-room`
and `--absent-kind`). A pack that fits beside the running one arrives while the game plays
on; a larger one replaces it, the game standing still until it is there. Either way only the
chunks that changed are sent (when it replaces the running one: also those that moved). A
pack larger than the GBA takes at all is fitted: the room named with `restart:ID` comes
first, then room 0, then as many other rooms as fit; the rest are left out, and the game
does not go there. To play a left-out room, push again with that room:

```
bmlink --xboo /dev/parport0 push build/test-output/test_level_gba.bmpk restart:debug_wave
```

`push` reports how many chunks were left out. A cartridge GBA sending its game to a
multiboot one (section 8) fits the pack around the room it is in.

Or let the game ask: walking into a door to a room its pack does not have, the multiboot game
stops with a box ("The next room comes over the cable"). With

```
bmlink --xboo /dev/parport0 serve build/test-output/test_level_gba.bmpk
```

running (until Ctrl-C), the room comes by itself, a second or two later, and the game goes
on through the door with everything the player had. Without it, after 3 seconds the box
offers to try again or to cancel (back into the room).

## 5. Changing things while the game runs

With the game running (PC or GBA) and **Live** on in the editor, save a room in Tiled, a
sprite, a script or a song: the editor cooks and pushes what changed. Over the cable a
changed room takes a few seconds; the game does not restart and the player stays in place.
A script with errors is not pushed; the running game keeps the last version that compiled.
A changed script is one small chunk (each class has its own module); the objects of other
classes keep running where they were, and those of the changed class keep their variables
and start their state's code again.

By hand it is `bmcook` and then `bmlink ... push level.bmpk module`.

## 6. Sync: one plays, the other plays along

Either the PC game shows what happens on the GBA, or the GBA does what is played on the PC.
Both must run the same level.

In the editor, with the game running on the GBA, use the buttons next to Run:

| Button | Who plays | Who follows |
|---|---|---|
| **GBA > PC** | you, on the GBA | the PC game window |
| **PC > GBA** | you, in the PC game window | the GBA |
| **Off** | both, each on its own | |

The PC game is started if it is not running. Switching direction ends one and starts the
other; Off, Stop or closing the PC window end it, and each side plays on from where it is.
With Live on, a change is pushed to both sides and the sync resumes.

By hand:

```
build/game level.bmpk --follow &                         # waits for the GBA
build/bmlink --xboo /dev/parport0 mirror from 7414       # GBA > PC, until Ctrl-C
```

```
build/game level.bmpk &
build/bmlink --xboo /dev/parport0 mirror to 7414         # PC > GBA
```

What to expect:

- The side that follows is a moment behind (a few frames). It never does something
  different: keys are applied by game frame, not by arrival time.
- `bmlink` prints a line every five seconds: frames, packets, restarts, jumps. A **restart**
  means the follower's state differed and was loaded again; a **jump** means the follower was
  more than ¾ second behind and was brought to where the other one is. Both should stay at 0.
  Each one restarts the music on the follower.
- While a GBA follows, the PC game runs at the GBA's 59.73 frames per second.

## 7. GDB on the GBA

```
build/bmcook --native arm-none-eabi-gcc --native-elf scripts.elf -o level.bmpk ...   # scripts as machine code
build/bmlink --xboo /dev/parport0 gdb 2345
arm-none-eabi-gdb build-gba/game.elf -ex "target remote :2345" \
  -ex "add-symbol-file scripts.elf &bm_native_slot" -ex "break bat.ms:13"
```

Breakpoints work in ARM code in RAM (the native script module), not in Thumb code or ROM.
So far this ran against the emulator with a test client; see [HARDWARE.md](HARDWARE.md) §7.

## 8. GBA to GBA

With two GBAs on a link cable, Select + Start in the cartridge game (`build-gba/game.gba`)
brings the other GBA to the same room and position, booting it first if it has no cartridge.
So far only between two emulated GBAs:

```
build/bm_mgba_pair build-gba/game_send.gba build-gba/bootstrap_mb.gba --frames 1500
```

## 9. When something does not work

| What you see | What to do |
|---|---|
| `cannot open /dev/parport0 (Permission denied)` | section 4, "The port" |
| `/dev/parport0 is in use by another bmlink` | a run or a sync is still going: Sync Off or Stop in the editor, or end the other `bmlink` |
| `bmlink` starts and prints nothing at all | another program that is not `bmlink` has the port (xcomms, the `lp` printer driver: `sudo rmmod lp`) |
| `boot: no response from the GBA` | the GBA is not at its BIOS screen, or the cable: [HARDWARE.md](HARDWARE.md) §5 |
| `boot: no logo given` | section 4, "The logo" |
| `boot: the image was sent, but the runtime does not answer` | try `hello` again; if the GBA shows the game but stays silent, switch it off and on and boot again |
| `the slave does not answer` | no runtime is running there; boot one |
| `mirror: the device and the PC game run different content` | Run on the GBA once more (or push the same pack to both), then sync again |
| the editor's Run seems to do nothing | read the Messages list: it says what the run waits for |
| `region ewram overflowed` when building | the GBA build was configured without optimisation; configure a fresh `build-gba` as in section 1 |

`BMLINK_TRACE=40 build/bmlink --xboo /dev/parport0 hello` prints the first 40 words each way;
[HARDWARE.md](HARDWARE.md) §5 says how to read them.

## 10. In the browser

The editor and the PC game also build for the browser with Emscripten (WebGL 1). The pages
are published on GitHub Pages by `.github/workflows/pages.yml` on every push to `develop`, the
branch development happens on.

```
cmake -S . -B build-gba -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gba.cmake -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-gba
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release -DBM_USE_MGBA=OFF -DBM_GBA_BUILD_DIR=$PWD/build-gba
cmake --build build-web --target bmeditor game
python3 -m http.server -d build-web      # then open http://localhost:8000/bmeditor.html
```

`web/index.html` is the start page that links both. What is different in the browser:

- The editor opens the test project, which is packed into the page together with the game's
  cartridge image. Edits live in the page's memory and are gone when it is closed.
- **Export** downloads the `.gba` file.
- **Run** (F6, target PC) plays all rooms in a frame at the bottom right of the editor's page
  (`game.html?editor`, which can be dragged by its title bar and resized by the grip in its
  bottom right corner, down to the GBA's 240×160; the picture keeps its aspect). Click into it to play; click the
  editor to edit. With **Live** on, every cook (a saved script, sprite, song or record, F5)
  is sent to it and applied as a reload of mode `module`: the player stays where they are and
  scripts that did not change keep running. **Stop** or the frame's × ends it. The pack
  travels by `postMessage` (`apps/game/web_editor.h`), not over the live link.
- Running on the GBA and **Sync** need the desktop editor: a page cannot start programs or
  reach the Xboo cable. File dialogs are not there either; paths are typed into the form.
- The game page on its own plays the test level. It cannot listen for the live link, and its
  save game lasts as long as the page.

