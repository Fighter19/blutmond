# Plans

Designs for what is not built yet. Each one says what it needs from the others and in which
steps it can be built. The overall plan and the milestones are in [../PLAN.md](../PLAN.md);
what is built is specified in [../spec/](../spec/README.md). When a plan is built, its
content moves into the spec and the plan shrinks to what is still open.

| Plan | Covers |
|---|---|
| [structures.md](structures.md) | custom structures and records: the data behind weapons, spells, abilities, consumables and armour |
| [ui.md](ui.md) | menus: screens, widgets, text, the Start and Equipment menus, animated backdrops that keep running |
| [attachable-scripts.md](attachable-scripts.md) | scripts that are attached to the player, an object or an item at run time; one module per script |
| [first-items.md](first-items.md) | the first weapons, spells, abilities, consumables and armour, with their numbers and scripts |
| [versioning.md](versioning.md) | versions of runtime, content and save games; incremental updates of a game on a flash cartridge |
| [hitboxes.md](hitboxes.md) | hit, hurt and guard boxes per animation frame, how hits are decided, projectiles, editing and showing boxes in the editor |
| [layered-sprites.md](layered-sprites.md) | figures made of several sprite layers, attachment points, direction sets for aiming, palette variants, enemy variants as data |
| [lighting.md](lighting.md) | one light per room: baked into the palettes or a light layer of normal tiles blended over the room (both built, to compare), lit palettes with normals for sprites, figure palettes with a bank per piece (open); the torch halls |
| [dialogue.md](dialogue.md) | dialogue (text box, speakers, choices, story flags, translation), cutscenes that direct actors and the camera, and their editor views |
| [3d-sprites.md](3d-sprites.md) | sprites rendered by Blender from a posed 3D model: palette, outline and normal map from the render (built; open: light slots, the editor) |

## How the plans depend on each other

Every plan has a section "Dependencies" with a table per step. The milestone numbers (M12 to
M19) name the plans; they are **not** the order to build in, because the plans lean on each
other step by step, not as wholes. "A needs B" below means: some step of A cannot be
finished before some step of B.

| Plan | Needs | Is needed by |
|---|---|---|
| structures | (versioning for renames) | ui, attachable-scripts, first-items, layered-sprites, dialogue |
| attachable-scripts | structures, hitboxes, layered-sprites | ui, structures, first-items, hitboxes, layered-sprites, dialogue |
| ui | attachable-scripts, structures | first-items, dialogue, hitboxes |
| hitboxes | attachable-scripts, structures, layered-sprites | first-items, layered-sprites, attachable-scripts |
| layered-sprites | structures, attachable-scripts, hitboxes | first-items, dialogue |
| versioning | a flash cartridge | structures, dialogue |
| dialogue | ui, structures, attachable-scripts | |
| first-items | all but versioning and dialogue | |
| lighting | (layered-sprites' palette table and figures) | |

Several pairs need each other (structures and attachable-scripts, attachable-scripts and
hitboxes, hitboxes and layered-sprites). That is no contradiction: in each pair the early
steps of both are free, and only later steps wait.

### Engine work several plans share

Do each once, with the first plan that reaches it:

| Work | Wanted by |
|---|---|
| Windows and blending in the PPU, three line-table slots (open from M1) | ui 1, dialogue 5 |
| State transfer in several packets | structures 3, and every plan that adds state |
| Base classes other than `Actor`, and "who may call" per API entry | attachable-scripts 3, ui 3, dialogue 2 and 5 |
| Enums and `struct` in the compiler | structures 1 |
| Optional handles and the first query | layered-sprites 6, dialogue 5 (`actor(#Name)`) |
| Objects created at run time, the object pool from 8 to 16 | attachable-scripts 5, hitboxes 4, layered-sprites 4, dialogue 7 |
| Health for the player and objects | attachable-scripts 5, hitboxes 2 |
| `sin`/`cos` and angles as tables | ui 5, layered-sprites 4, hitboxes 4 |
| Sprite format version 2 (boxes, points, palettes) | hitboxes 1, layered-sprites 1 and 3 |
| Hardware sprites in use from 48 to 64, sheets loaded per room | layered-sprites 2 and 4, ui 2 |
| The body step reading its numbers from the state | attachable-scripts 4 and 7 |

### An order that works

Each round only needs the rounds before it. Inside a round the items are independent and
can be built in any order or side by side.

| Round | Steps |
|---|---|
| 1 | structures 1 to 3 · attachable-scripts 1 to 3 · ui 1, 2 · hitboxes 1 · layered-sprites 1 to 3 · versioning 1, 2 · dialogue 1 |
| 2 | structures 4 · attachable-scripts 4, then 5 together with hitboxes 2 · ui 3 · versioning 3, 4 · first-items 1 |
| 3 | attachable-scripts 6, 7 · layered-sprites 4, 5 · ui 4, 5 · dialogue 2 to 4 · first-items 2 |
| 4 | hitboxes 3, 4 · attachable-scripts 8 · dialogue 5 to 7 · first-items 3 to 5 · versioning 5 |
| 5 | layered-sprites 6 · first-items 6, 7 |
| 6 | the editor views (structures 5, then hitboxes 5; ui 6, layered-sprites 7, dialogue 8) · versioning 6 on real cartridges |

The editor views are listed last because nothing waits for them; in practice each is best
built right after the runtime part it shows, since it is what makes that part testable.
