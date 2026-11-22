# Plan: dialogue and cutscenes

Status: a first part built: `say(speaker, line)` shows a box with a name and a line until A
(`dialogue_done()`), `player_lock(frames)`; the boss's intro uses them. Languages (step 4)
are built differently from section 4: a translation file per language keyed by the original
text, not by line ids; text is UTF-8 on the device (decoded per character), so fonts of other
scripts can be added in parts; the cooker reports characters no font has; the language is an
option in the save memory ([../spec/pack-format.md](../spec/pack-format.md) §3.2). Everything
else is open. Milestone M19 in [../PLAN.md](../PLAN.md).

## Dependencies

Steps refer to section 9.

| Step | Needs |
|---|---|
| 1 (flags and counters) | nothing from other plans; stable flag numbers across versions lean on [versioning.md](versioning.md) step 2 |
| 2 (`say` and the dialogue box) | [ui.md](ui.md) steps 2 and 3; speakers as records: [structures.md](structures.md) step 2; variable-width text: the canvas of ui.md |
| 3, 4 (talk files, languages) | nothing more |
| 5 (cutscenes) | the scriptable player with `control` and `walk_to`: [attachable-scripts.md](attachable-scripts.md) step 7; fades and letterbox: ui.md step 1 |
| 6 (skipping) | nothing more |
| 7 (bubbles, emotes, puppets) | `spawn` from attachable-scripts.md step 5 |
| 8 (Talk and Cutscene views) | nothing more |

`set_palette` and layered portraits come from [layered-sprites.md](layered-sprites.md)
steps 1 and 3; plain sprites as portraits work without them. Nothing depends on this plan.

## 1. What this is for

- **Dialogue**: a box with a speaker's name and portrait, text that appears letter by
  letter, pages, choices, and consequences (an item given, a flag set).
- **Short lines in the world**: a bubble over a character while the game goes on.
- **Cutscenes**: characters walk, turn and talk, the camera moves, the screen fades, music
  changes; the player's control is taken away and given back.
- **Tools**: writing and translating the text, seeing it in the box before it is on the
  device, playing and stepping through a cutscene in the editor.

One decision carries the whole plan: **a conversation and a cutscene are scripts**, run by
the interpreter that runs everything else, as state code with latent functions
([../spec/mondskript.md](../spec/mondskript.md) §5). So a save state taken in the middle of
a sentence continues there, the sync between GBA and PC keeps working, a changed line is
pushed like any script, and there is no second engine to keep exact on two machines.

## 2. Writing dialogue

Writers do not write Mondskript. A conversation is a **talk file** (`.bmtalk`), plain text:

```
talk ElderIntro

Elder: The moon has turned again, {player}.
Elder (worried): You should not be out here{pause 20}... not tonight.

? What do you say?
  - I'm looking for the crypt key.             -> key
  - Who are you?                               -> who
  - Goodbye.                    [if met_elder] -> end

who:
Elder (smile): Only the keeper of this hill.
set met_elder
-> start

key:
Elder: Then take it. {give IronKey}{sound Chime}It opens the door below.
set has_crypt_key
```

- `Name:` starts a line by that **speaker**; `(worried)` picks an expression. Text runs to
  the end of the paragraph.
- `? ... - ...` is a choice of up to 4 answers; `[if ...]` hides one; `-> label` jumps.
  `start` and `end` exist by themselves.
- `set`, `clear`, `add` change story flags and counters (section 5); `[if ...]` and
  `if ... { }` test them, items (`has(IronKey)`) and stats.
- **Codes in the text**: `{pause n}`, `{speed n}`, `{colour n}`, `{shake}`, `{wait}` (wait
  for the button here), and values filled in when shown: `{player}`, `{item IronKey}`,
  `{count potions}`, `{button Attack}` (the glyph of the key that button is mapped to).
- **Things that happen at that point of the line**: `{give X}`, `{take X}`, `{sound X}`,
  `{music X}`, `{anim Elder bow}`, `{call name}` (a function of the surrounding script).

The cooker turns a talk file into a script class (`extends Talk`): each label becomes a
state, each line a `say(...)`, each choice a `choose(...)`. The generated script can be
looked at (`bmcook --talk-script-out`) and debugged like a written one. Whoever prefers can
write that class by hand; `say("...")` also works in any object's state code for a
one-line signpost.

**Speakers** are records ([structures.md](structures.md)):

```
struct Speaker { text name; sprite portrait; sound voice; byte colour; byte side; }
record Elder { name = "Elder"  portrait = "faces/elder"  voice = #BlipLow }
```

A portrait is a sprite whose animations are the expressions (`neutral`, `worried`,
`smile`); a second step in an animation is the open mouth, shown while letters appear.

## 3. The dialogue box

An overlay screen ([ui.md](ui.md) §2) that ships with the engine as layout and script, to be
restyled or replaced by a game:

```
screen DialogueBox overlay {
  frame   at 1,13  size 28,6  style=Panel
  icon    Face     at 2,14  size 4,4
  label   Name     at 7,13
  text    Body     at 7,14  size 21,4  canvas
  cursor  More     at 27,18
  list    Answers  at 16,8  size 13,4  hidden
}
```

- **Typewriter.** Letters appear at a speed in letters per frame (default 1, the game's
  option "text speed" scales it). The confirm button shows the rest of the page at once;
  on a full page it turns to the next; holding cancel runs fast.
- **Wrapping is done by the cooker**, which knows the font's widths: every line is stored
  already broken into rows and pages for the box's size. Lines with values filled in at
  run time are broken on the device, word by word. A word that cannot fit a row and a text
  that needs more pages than allowed are reported when cooking, per language.
- **Voice**: the speaker's blip every few letters, not on spaces.
- **Position**: bottom by default; the box moves to the top when the speaker stands in the
  lower part of the screen. `side` puts the portrait left or right.
- **Cost.** A page of 21 x 4 cells of drawn text is 84 canvas tiles (of the 192 the UI
  has); a letter costs a few hundred cycles to draw, so one letter per frame is far below
  1 % of a frame. The room keeps running underneath or stands still (section 6).

**Bubbles.** `bark(actor, text, frames)` shows a short line in a small frame above an
actor, following it, for a time, without stopping anything: sprites of the UI's 16, at most
2 at once, one row of at most 12 letters each. For shouts, hints and background chatter.

## 4. Text and languages

- Every line has an **id** that stays the same when the wording changes. The editor writes
  it into the talk file when a line is created (`Elder: Take it. #a3f1`), so a translation
  does not lose its place when the original is edited; the cooker marks translations whose
  original changed since as "to review".
- One **text file per language** (`text/de/ElderIntro.bmtext`): id, original, translation.
  Missing lines fall back to the project's main language and are listed in the report.
- Cooked: one `STRS` chunk per language (category `text`, so a cartridge can hold all and
  the link sends only the current one). The language is a game option kept in the save
  memory outside the save games.
- Text is stored in the font's own character numbers, not UTF-8, so the device does no
  decoding. The cooker reports characters a font lacks. Languages that need another font
  name one.
- The same mechanism serves every other text: item names, menu labels.

## 5. Story flags

Conversations need to remember. Today scripts have 16 saved values in all
([../spec/runtime-core.md](../spec/runtime-core.md) §8); that is not enough.

- **Flags**: named bits, declared in a project file (`flags.bmdata`: `flag met_elder`,
  optionally grouped by chapter). 512 of them, 64 bytes of state.
- **Counters**: named 16-bit numbers (`counter potions_given`), 32 of them.
- Both `save link`. Scripts read and write them (`flag(#met_elder)`, `set_flag`,
  `counter_add`); talk files by name. A flag keeps its number once assigned (stored in the
  file), so saves survive new flags ([versioning.md](versioning.md) §7).
- The editor's State tab lists them with their values in the running game and lets them be
  switched, which is how a later part of the story is tested without playing up to it.

## 6. Cutscenes

A cutscene is a script class (`extends Cutscene`) whose state code directs others:

```
class CryptOpens extends Cutscene;

state Play { begin:
  letterbox(true);
  var elder = actor(#Elder);            // the object named "Elder" in the room
  direct(player);  direct(elder);       // their own scripts rest; they do what is said here

  start_walk(player, 180, 1.0);         // these two walk at the same time
  start_walk(elder, 210, 0.75);
  wait_walks();
  face(player, elder);  face(elder, player);

  talk(#ElderIntro);                    // a whole conversation, choices included

  camera_to(320, 96, 60, #EaseInOut);   // pan to the crypt door in 60 frames
  shake(3, 30);  sound(#StoneGrind);
  play_anim(actor(#CryptDoor), #open);  finish_anim_of(actor(#CryptDoor));
  sleep(30);
  camera_follow(player, 40);

  set_flag(#crypt_open);
  letterbox(false);
  end();                                // releases everyone
}
```

**What a cutscene can direct**

| | Functions |
|---|---|
| Characters | `direct(a)`, `release(a)`; `walk_to` / `start_walk` + `wait_walks`, `jump`, `face`, `play_anim`, `finish_anim_of`, `move_to` (placed at once), `show`, `hide`, `set_palette`, `emote(a, #Surprise)` (a small sprite above the head) |
| Extras | `spawn_puppet(sprite, x, y)`: a figure that exists only for the scene, with no script of its own |
| Talking | `say(speaker, text)`, `talk(name)`, `choose(...)`, `bark` |
| Camera | `camera_to(x, y, frames, ease)`, `camera_follow(a, frames)`, `camera_lock(bool)`, `shake(strength, frames)` |
| Screen | `fade_out(frames)`, `fade_in(frames)`, `flash(frames)`, `letterbox(bool)`, `tint(palette, frames)` |
| Sound | `music(name)`, `music_fade(frames)`, `sound(name)` |
| Flow | `sleep`, `wait_button`, `wait_until`, `goto_room(room, door)` (the scene goes on in the new room), `end()` |

- **At the same time.** State code does one thing after the other. For things that should
  overlap, every lasting action has a form that starts it and returns (`start_walk`,
  `camera_to`, `fade_out`), and a form that waits (`wait_walks`, `wait_camera`,
  `wait_fade`). That covers "both walk while the camera pans" without parallel scripts.
- **Directed characters.** `direct(a)` rests the character's own script and, for the
  player, takes the buttons away; movement still goes through the body step, so a directed
  walk climbs slopes and stops at walls like a played one. `end()` releases everyone and
  hands the camera back.
- **The rest of the room** stands still during a cutscene by default (objects are not
  ticked, animations go on); `world(true)` lets it run for scenes that happen in the
  middle of play.
- **Camera.** Today it follows the player inside camera zones
  ([../spec/runtime-core.md](../spec/runtime-core.md) §7). It gains a mode "directed":
  a target, a time and an easing curve, still limited to the room.
- **Fades and letterbox** use the brightness register and a window over two bars
  ([ui.md](ui.md) step 1 brings both).

### Starting one

- The engine class `Trigger`, which does nothing today, gets its meaning: properties
  `cutscene` or `talk`, `on` (`enter`, `use`), `once`, `if` (a flag test). `once` is
  remembered as a flag.
- An object with a `talk` property starts that conversation when the player presses Up in
  front of it; this is how a simple NPC needs no script at all.
- From any script: `play_cutscene(name)`, `talk(name)`.
- A room property `on_enter` names a cutscene checked when the room is entered.

### Skipping

Start skips a cutscene. A skipped scene must leave the world exactly as a watched one:
flags set, items given, characters where they ended up. So skipping is **fast-forward**:
the same script runs on without drawing, with waits cut short and text passed over, as many
steps per frame as fit, behind a quick fade, until `end()`. There is no second "what to do
when skipped" code that can go out of step with the scene.

- A **choice** stops the fast-forward: the scene shows again at that question.
- A scene can mark a stretch `no_skip { }`.
- The same fast-forward serves the editor's scrubbing (section 8).

## 7. State

All `snapshot`, in the game's state block, so nothing here breaks save states or the sync:

- the running cutscene and conversation as script objects (class, state, place, timer,
  variables), nested at most 3 deep (a cutscene, the conversation in it, a question in
  that);
- the box: line id, page, letters shown, wait, the answer cursor;
- who is directed, the camera's mode, target and time, fade level, letterbox;
- flags and counters (`save link`).

Saving the game is not offered while a cutscene runs; a save point's scene ends first. A
save state (snapshot) is.

## 8. In the editor

**Talk view** (in the Script tab for `.bmtalk` files)

- The text on the left with highlighting for speakers, codes and jumps; an **outline** of
  labels and choices with their jumps as a small graph, clicking jumps there.
- A **live box** on the right: the line under the cursor in the real dialogue box, drawn by
  the runtime's own code, with wrapping, pages, portrait and colours, and a play button for
  the typewriter and voice. A language switch shows the same line in each language.
- **Play through**: the conversation runs in the preview with clickable answers; flags it
  reads can be set beforehand.
- The report lists: lines that overflow the box, by language; missing and "to review"
  translations; characters the font lacks; labels never reached; flags read but never set.

**Cutscene view** (in the room view, for a `Cutscene` class)

- Play, pause, step by frame, and a **scrub bar**: jumping to a frame runs the scene from
  its start by fast-forward (or from the nearest of the snapshots the editor keeps every
  few seconds).
- The paths of `walk_to` and `camera_to` are drawn into the room; their end points can be
  dragged, which rewrites the numbers in the script.
- Objects of the room are offered by name when typing `actor(#`.
- The script line being executed is highlighted while the scene plays, in the editor's
  preview and in the game running on the PC or the GBA.
- "Play from here" starts the running game at this room with this cutscene, with the flags
  as set in the State tab.

## 9. Steps

1. Flags and counters: declaration file, state, script functions, State tab.
2. `say` and the dialogue box with fixed-width text: typewriter, pages, wrapping in the
   cooker, name and portrait, voice. An NPC with a `talk` property.
3. Talk files: parser, compilation to a `Talk` class, choices, conditions, the codes in
   text, values filled in at run time.
4. Text per language: line ids, text files, `STRS` per language, the option, the report.
5. Cutscenes: `Cutscene` class, `direct`, the walking and facing functions, the directed
   camera, fades, letterbox, `Trigger`, room `on_enter`.
6. Skipping by fast-forward; `no_skip`.
7. Bubbles, emotes, puppets.
8. The Talk view and the Cutscene view.

Done when: in the demo an elder gives the crypt key in a conversation with a choice, in two
languages; a cutscene with the player, the elder and a camera pan opens the crypt; watching
it and skipping it leave the same game state (compared by checksum); a snapshot taken in
the middle of a sentence continues on the other machine; a changed line is on the GBA
seconds after saving the talk file; and all of it gives the same checksums on GBA and PC.

## 10. Open questions

- **A message log** (reading back what was said) needs the ids of the last lines kept:
  cheap, left out until wanted.
- **Variable-width text** reads much better in dialogue than 8 x 8 letters; step 2 starts
  fixed-width only because the canvas is the later step in [ui.md](ui.md). If the canvas
  comes first, start with it.
- **Parallel tracks** (a real timeline with several scripts at once) are avoided by the
  start-and-wait pairs. If scenes outgrow that, a cutscene could own attachments, each
  running a track.
- **Lip movement and portraits on the device** cost sprite tiles per speaker shown; two
  portraits at once is the planned limit.
