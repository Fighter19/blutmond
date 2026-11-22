# Plan: menus and UI

Status: step 1 built: windows (two, the OBJ window, outside) and colour effects (alpha,
brighten, darken, semi-transparent sprites) in the software PPU, three line-table slots on the
GBA (DMA 0 and 3, an H-blank interrupt), `bmWindow*`, `bmBlendSet` and `bmVideoSetLineTable`
in `video.h`; the scene `scene_effects` matches between the PC and mGBA
([../spec/gba-backend.md](../spec/gba-backend.md) §5 and §7). There is no shader PPU yet (the
PC game draws the software PPU's picture), so "the shader PPU" of section 2 is still to do
when there is one. Step 2 built: the font asset (`.bmfont`, `FONT`,
[../spec/pack-format.md](../spec/pack-format.md) §3.1) and the engine's font, the UI layer
(`src/ui/ui.h`: background 3, character block 2, screen block 25, palette bank 15; frames,
labels, numbers, text with placeholders `{0}`-`{7}` wrapped at spaces), and the debug overlay
in the game (L + R + Select, `--overlay`); the same picture on the PC and in mGBA. Where it
differs from sections 2 and 3: the font chunk is 1 bit per pixel and the runtime draws the
shadow (the test level's pack has to fit the 48 KB a multiboot GBA gives a pushed pack, and
had less than a kilobyte to spare); the UI keeps its map as one byte per cell for the 20
visible rows; no canvas, no darkening behind frames yet. Steps 3 to 6 are open. Milestone M13
in [../PLAN.md](../PLAN.md).

## Dependencies

Steps refer to section 9.

| Step | Needs |
|---|---|
| 1 (windows, blending, more line tables) | nothing from other plans; it is the part of milestone M1 that is still open |
| 2 (UI layer, font, text) | step 1 only for darkening behind windows |
| 3 (screens as scripts) | script classes that are not room objects: the kinds of class of [attachable-scripts.md](attachable-scripts.md) §2, built in its step 3 |
| 4 (lists bound to the inventory, Equipment menu) | [structures.md](structures.md) steps 3 and 4 |
| 5 (backdrops, effects) | step 1; `sin`/`cos` are shared with [layered-sprites.md](layered-sprites.md) step 4 |
| 6 (save in slices, UI tab) | nothing more |

Needed by: [first-items.md](first-items.md) steps 3 and 4, [dialogue.md](dialogue.md) steps
2 and 5, the "show boxes" option of [hitboxes.md](hitboxes.md).

## 1. What this is for

- A **Start menu**: Start pauses the game and opens a menu with the player's status and the
  entries Equipment, Items, Spells, System (save data, options).
- An **Equipment menu**: slots on one side, what fits the chosen slot on the other, the
  change in stats before it is confirmed.
- The pieces those are made of, so a game builds its own: title screen, shop, dialogue box,
  HUD.
- **Animated backdrops** behind menus (waves, drifting layers, colour cycling, sprites) that
  stay fluid: they run at full frame rate whatever the menu or the engine is busy with.

How "run in the background" is read here: the animation of a menu's backdrop and of its
widgets must not stop or stutter while something else takes time (writing a save, loading
icons, a link transfer, a slow script). Section 6 is built around that.

## 2. Where the UI lives on screen

The game uses backgrounds 0 to 2 for a room's layers, character blocks 0 and 1 for its
tiles and screen blocks 26 to 31 for its maps
([../spec/runtime-core.md](../spec/runtime-core.md) §7). Free and given to the UI:

| What | Where | Size |
|---|---|---|
| UI layer | background 3, priority 0, 256 x 256, screen block 25 | 30 x 20 cells visible |
| UI tiles | character block 2 | 512 tiles: font 128, frames and cursors 64, icons 128 (32 icons of 16 x 16), canvas for variable-width text 192 |
| UI palette | background bank 15 and sprite bank 13 | the palette packer gets 15 banks per area instead of 16 |
| UI sprites | the first 16 entries of the sprite table | cursors, highlights, backdrop sprites |
| Second backdrop map | screen block 24 | for full-screen menus |

Two kinds of screen:

- **Overlay** (HUD, dialogue box, a small pause window): only background 3. The room stays
  visible; the area behind a window is darkened with a window and brightness blending.
- **Full screen** (Start menu, Equipment, title): the room's backgrounds are hidden and the
  screen may use backgrounds 0 to 2 for its backdrop, with its own tiles in character
  block 0. When the screen closes, the room's video memory is rebuilt by the code that
  already does it after loading a state ([../spec/state.md](../spec/state.md) §5).

Needed in the PPU first (open from M1): windows and blending in the software PPU and the
shader PPU, two more line-table slots.

## 3. Screens and widgets

A screen is a **layout** (data) plus a **script** (behaviour).

Layout, a text asset (`.bmui`), cooked into a `UISC` chunk:

```
screen Equipment fullscreen backdrop=MenuWaves {
  frame   at 0,0   size 30,20  style=Panel
  label   at 2,1   text="EQUIPMENT"
  list    Slots    at 2,3  size 12,7  rows=7
  list    Choices  at 15,3 size 14,10 rows=5 icon
  text    Info     at 2,15 size 26,3  wrap
  gauge   Hp       at 2,12 size 10,1
  number  Attack   at 24,12 digits=3
  delta   AttackDelta at 27,12          # +3 in green, -2 in red
}
```

Widgets to start with: `frame`, `label` (fixed text), `text` (changing text, optional
wrapping), `number`, `delta`, `gauge`, `icon`, `list` (scrolls, optional icons and amounts),
`grid`, `cursor`. Positions are in 8-pixel cells; `text` in a canvas may be placed by pixel.
A widget only redraws when its content changed: an open menu that nobody touches costs
almost nothing.

**Text.** A font is an asset (`.bmfont`: an image, 8 x 8 or 8 x 16 cells, optional widths
per glyph) cooked into a `FONT` chunk. Fixed-width text is map entries pointing at glyph
tiles; variable-width text is drawn into canvas tiles. There are still no strings at run
time: a text is a string id, and placeholders in it (`"{0} x{1}"`) are filled by the
runtime from numbers and string ids the script hands over. Text ids keep translation
possible.

**Binding.** A list can be bound to the inventory: `Choices.bind(inventory, role = equip,
slot = Slots.selected)`. The runtime then fills titles, icons and amounts itself; the script
does not loop over items every frame.

## 4. Scripts

A screen's script is a Mondskript class with states and state code like any other
([../spec/mondskript.md](../spec/mondskript.md) §5):

```
class EquipmentMenu extends Screen;

event open()        { Slots.focus(); refresh(); }
event move(int dx, int dy) { play_sound(#Tick); refresh(); }
event confirm() {
  if focus_is(Slots) { Choices.focus(); }
  else { equip(Slots.selected, Choices.selected_record); Slots.focus(); }
}
event cancel() {
  if focus_is(Choices) { Slots.focus(); } else { close(); }
}

function refresh() {
  Info.set(Choices.selected_record.description);
  Attack.set(stat(#Attack));
  AttackDelta.set(stat_with(#Attack, Slots.selected, Choices.selected_record) - stat(#Attack));
}

state Opening { begin:
  tween(#Panel, #y, -160, 0, 12, #EaseOut);
  wait_tweens();
  gotostate Idle;
}
```

- Events: `open`, `close`, `tick`, `move(dx, dy)` (with key repeat done by the runtime),
  `confirm`, `cancel`, `key(name)` for the rest, `selected(widget)`.
- Screens form a stack of at most 4 (Start menu, Equipment on top, a confirmation on top of
  that). Only the top one gets input; all visible ones are drawn.
- New API entries: `open_screen(name)`, `close()`, widget calls (`set`, `focus`, `bind`,
  `show`, `hide`), `tween` and `wait_tweens`, `effect_set` (section 5), `equip`,
  `stat_with`.
- While a full-screen or pausing screen is open, the game frame is not run: the game state
  does not change, sound goes on. An overlay screen may let the game run (HUD).

The Start and Equipment menus ship as scripts and layouts in the engine's asset folder, as a
working default a game copies and changes.

## 5. Backdrops and effects

A backdrop is an asset (`.bmbackdrop`): up to three tile layers (as a small room without
collision), sprites, and **effects** with parameters:

| Effect | What the runtime does each frame |
|---|---|
| `scroll` | moves a layer by a speed (drifting clouds, parallax) |
| `wave` | fills a line table: `base + amplitude * sin(line * frequency + phase)` on a scroll register (water, heat, a flag) |
| `gradient` | fills a line table for a colour or the brightness register (sky, fade) |
| `cycle` | rotates a range of palette entries |
| `tile_anim` | replaces a tile's graphics every n frames |
| `sprite_path` | moves sprites along a curve (drifting petals, embers) |
| `blend` | changes the blend weights (pulsing glow) |

The per-line and per-sprite work is engine code (native, in IWRAM where it is hot). A script
only changes parameters, so it is cheap and can be slow without the picture stuttering:

```
class MenuWaves extends Effect;
var fix Phase;
event tick() {
  Phase += 0.03;
  effect_set(#Water, #phase, Phase);
  effect_set(#Glow, #weight, 8 + sin(Phase) * 4);
}
```

`sin`, `cos` (table, 256 steps per turn, `fix` result) and easing curves become API
entries. The same effects can be used by rooms later (raster effects, `EFCT` in
[../spec/pack-format.md](../spec/pack-format.md)); the UI is their first user.

## 6. Staying fluid

Three rules make "nothing stutters" hold:

1. **Two clocks.** The game frame counter stands still while a pausing screen is open. The
   UI has its own counter that advances with every vertical blank, also when a frame was
   missed, so animations skip ahead instead of slowing down.
2. **Present from the vertical blank.** Tweens and effects are advanced and written to the
   hardware by a short function (`bmUiPresent`: registers, line tables, a few sprites,
   palette entries). The main loop calls it once per frame. If the main loop has not come
   round by the next vertical blank, the interrupt calls it instead. It runs no script and
   touches only the UI's own data.
3. **Long work in slices.** Anything the UI starts that can take longer than a frame is cut
   up: writing a save (a part per frame), loading icons (four per frame), drawing canvas
   text (one widget per frame). Scripts have their instruction budget anyway.

Budget on the GBA: `bmUiPresent` at most 5 % of a frame with one wave and one gradient
table; an open menu with a bound list at most 15 % while scrolling. Both are measured in
mGBA as the sound engine's cost is.

## 7. State

- `ui`: the stack of screens, each screen's script object (state, place, variables), focus
  and selection of each list. Flags `snapshot`: a save state taken in a menu comes back in
  that menu, and the sync between GBA and PC
  ([../spec/link-protocol.md](../spec/link-protocol.md) §10) keeps working with menus open,
  because input events are derived from the mirrored keys.
- `ui_view`: tween positions, effect phases, the UI clock. Flags `transient`: presentation
  only, not compared, not saved. After loading a state they start from their rest values.

## 8. In the editor

A **UI** tab: the layout as text with a live preview drawn by the runtime's own code
(software PPU), widgets selectable and movable in the preview, backdrop effects with sliders
for their parameters, and "open in the running game" over the link.

## 9. Steps

1. PPU: windows, blending, three line-table slots; software PPU and GBA match in the test
   scene.
2. UI layer: font asset and `FONT` chunk, `frame`, `label`, `number`, text with
   placeholders; a debug overlay (frame time, room) as the first user.
3. Screens: `UISC`, the stack, input events with key repeat, `Screen` classes, pause.
   The Start menu with status page.
4. Lists bound to the inventory, icons with the icon cache, the Equipment menu with stat
   preview.
5. Backdrops and effects, `bmUiPresent` from the vertical blank, tweens, `sin`/`cos`.
6. Save from the menu in slices; the UI tab.

Done when: Start opens the menu over the paused game on GBA and PC; a weapon can be changed
and the next hit uses it; the backdrop's wave runs at full rate while the save is written
(measured: no vertical blank without a present); a snapshot taken inside the Equipment menu
loads on the other machine and shows the same screen.

## 10. Open questions

- Variable-width text costs drawing time and canvas tiles; if 8 x 8 text reads well enough
  on the device, the canvas can wait.
- Affine backgrounds (rotation, zoom) for backdrops need mode 1 or 2 and are not built;
  left to M11.
- Whether the HUD belongs on background 3 (shares tiles with menus) or on sprites (keeps
  the layer free for dialogue). Start with background 3.
