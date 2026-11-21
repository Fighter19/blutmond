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

## 2. Play on the PC

```
build/game                      # the test level in a window
build/game level.bmpk           # a pack of your own
```

Keys: arrows to move, X or Space is A (jump), Z is B, A and S are L and R, Return is Start,
Backspace is Select, F11 full screen, Escape ends. Gamepads work. The window keeps a save game
in a file and listens for the link on `127.0.0.1:7414`.

Without a window (for tests and scripts): `--frames N`, `--headless`, `--autoplay`,
`--snapshot-at N file`, `--load file`, `--listen PORT`, `--follow`, `--until-frame N`,
`--realtime`, `--random-keys N`. The comment at the top of `apps/game/main.c` describes them.

## 3. The editor

```
build/bmeditor                      # opens assets/test.bmproj
build/bmeditor game.bmproj
```

Options: `--theme classic|light|dark` (also in the View menu, remembered), `--target pc|gba`,
`--xboo device`, `--reset none|xboo`, `--gba-image file`, `--bmlink file`, `--game file`,
`--port N`. The last six can also stand in `~/.config/blutmond/editor.conf` as `key = value`.

- **Run** (F6) cooks all rooms and plays them on the target next to the button: **PC** (the
  game window) or **GBA (Xboo)** (section 4).
- **Live**: every cook while something runs (a saved file, F5) is pushed in as a hot reload;
  the player stays where they are.
- **Sync** (Off, GBA > PC, PC > GBA): section 6.
- **Stop** ends the PC game and any sync. The GBA keeps running.
- The tabs (Room, Animation, Script, Sound, Graphics, and Project and State on the left) are
  described in the [README](../README.md).

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
(you have a minute to switch the GBA on), then pushes the level. Run does not replace a
runtime that is already running; after rebuilding the runtime, boot it once by hand as above.

### What bmlink can do

```
build/bmlink --xboo /dev/parport0 hello                       # what runs there
build/bmlink --xboo /dev/parport0 ping 5 stats                # speed and error counters
build/bmlink --xboo /dev/parport0 push level.bmpk restart     # a new level, from the start
build/bmlink --xboo /dev/parport0 push level.bmpk hot         # ... keeping the game's state
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

A multiboot GBA has 48 KB for a pushed pack (the test level is 39 KB).

## 5. Changing things while the game runs

With the game running (PC or GBA) and **Live** on in the editor, save a room in Tiled, a
sprite, a script or a song: the editor cooks and pushes what changed. Over the cable a
changed room takes a few seconds; the game does not restart and the player stays in place.
A script with errors is not pushed; the running game keeps the last version that compiled.

By hand it is `bmcook` and then `bmlink ... push level.bmpk hot`.

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
  -ex "add-symbol-file scripts.elf &bm_native_slot" -ex "break bat.ms:14"
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
