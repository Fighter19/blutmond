/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// What scripts can call and read (docs/spec/mondskript.md): one list, used by the compiler
// (names, argument and result types), the verifier (indices, argument counts) and the runtime
// (the functions behind them). The order is the API index stored in bytecode: add at the end,
// never reorder (runtime-abi.md).

// Value types, as the compiler and the class table name them.
typedef enum BmScriptType {
  BM_SCRIPT_VOID = 0,
  BM_SCRIPT_BOOL,
  BM_SCRIPT_INT,
  BM_SCRIPT_FIX,    // 24.8
  BM_SCRIPT_NAME,   // string id, written #Name
  BM_SCRIPT_TEXT,   // string id, written "text"
  BM_SCRIPT_ENUM,   // a value of an enum (0, 1, ...); which enum only the compiler knows
  BM_SCRIPT_RECORD, // record<S>: the key of a record (FNV-1a of its name), 0 none
  BM_SCRIPT_HANDLE, // Actor: a handle to an object that was there when it was checked (mondskript.md §6)
  BM_SCRIPT_OPTIONAL, // Actor?: a handle or null (0); checked with `if a := h { ... }`
} BmScriptType;

// X(name, result type, argument count, argument types (4)..., latent)
// Latent functions may only be called from state code; they can take more than one frame.
#define BM_SCRIPT_API(X) \
  X(sleep,             VOID, 1, INT,   VOID,  VOID,  VOID, 1) \
  X(finish_anim,       VOID, 0, VOID,  VOID,  VOID,  VOID, 1) \
  X(play_anim,         VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(anim_done,         BOOL, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(player_x,          FIX,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(player_y,          FIX,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(player_distance,   INT,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(move_toward_player, VOID, 1, FIX,   VOID,  VOID,  VOID, 0) \
  X(move_toward,       BOOL, 3, FIX,   FIX,   FIX,   VOID, 0) \
  X(hurt_player,       VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(enter_door,        VOID, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(hide,              VOID, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(show,              VOID, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(face_player,       VOID, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(log,               VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(random,            INT,  1, INT,   VOID,  VOID,  VOID, 0) \
  X(touching_player,   BOOL, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(give_item,         VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(has_item,          BOOL, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(abs,               INT,  1, INT,   VOID,  VOID,  VOID, 0) \
  X(play_sound,        VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(play_music,        VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(record_get,        INT,  3, INT,   INT,   INT,   VOID, 0) \
  X(record_count,      INT,  1, INT,   VOID,  VOID,  VOID, 0) \
  X(record_at,         INT,  2, INT,   INT,   VOID,  VOID, 0) \
  X(item_count,        INT,  1, RECORD, VOID,  VOID,  VOID, 0) \
  X(give,              INT,  2, RECORD, INT,   VOID,  VOID, 0) \
  X(take,              BOOL, 2, RECORD, INT,   VOID,  VOID, 0) \
  X(stat,              INT,  1, ENUM,  VOID,  VOID,  VOID, 0) \
  X(add_stat,          VOID, 2, ENUM,  INT,   VOID,  VOID, 0) \
  X(attach,            BOOL, 2, INT,   NAME,  VOID,  VOID, 0) \
  X(attach_for,        BOOL, 3, INT,   NAME,  INT,   VOID, 0) \
  X(detach,            VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(impulse,           VOID, 2, FIX,   FIX,   VOID,  VOID, 0) \
  X(room_attach,       BOOL, 2, INT,   NAME,  VOID,  VOID, 0) \
  X(room_detach,       VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(arm,               VOID, 3, NAME,  INT,   ENUM,  VOID, 0) \
  X(disarm,            VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(hit_box,           VOID, 4, INT,   INT,   INT,   ENUM, 0) \
  X(equip,             BOOL, 2, INT,   RECORD, VOID,  VOID, 0) \
  X(owner_lock,        VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(owner_play_anim,   VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(owner_busy,        BOOL, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(ui_frame,          VOID, 4, INT,   INT,   INT,   INT,  0) \
  X(ui_label,          VOID, 3, INT,   INT,   TEXT,  VOID, 0) \
  X(ui_number,         VOID, 4, INT,   INT,   INT,   INT,  0) \
  X(ui_text,           VOID, 4, INT,   INT,   INT,   TEXT, 0) \
  X(ui_gauge,          VOID, 4, INT,   INT,   INT,   INT,  0) \
  X(ui_clear,          VOID, 4, INT,   INT,   INT,   INT,  0) \
  X(ui_show,           VOID, 1, BOOL,  VOID,  VOID,  VOID, 0) \
  X(open_screen,       BOOL, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(close_screen,      VOID, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(language,          INT,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(set_language,      VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(language_count,    INT,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(language_name,     TEXT, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(equipped,          RECORD, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(record_text,       TEXT, 2, RECORD, NAME,  VOID,  VOID, 0) \
  X(record_int,        INT,  2, RECORD, NAME,  VOID,  VOID, 0) \
  X(heal,              VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(max_health,        INT,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(spawn,             BOOL, 3, NAME,  FIX,   FIX,   VOID, 0) \
  X(spawn_with,        BOOL, 2, NAME,  RECORD, VOID,  VOID, 0) \
  X(destroy,           VOID, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(floor_y,           FIX,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(say,               VOID, 2, TEXT,  TEXT,  VOID,  VOID, 0) \
  X(dialogue_done,     BOOL, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(player_lock,       VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(player_health,     INT,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(add_resist,        VOID, 2, ENUM,  INT,   VOID,  VOID, 0) \
  X(set_affinity,      VOID, 2, ENUM,  ENUM,  VOID,  VOID, 0) \
  X(strike_back,       BOOL, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(add_speed,         VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(mana,              INT,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(max_mana,          INT,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(restore_mana,      VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(spend_mana,        BOOL, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(use_item,          BOOL, 1, RECORD, VOID,  VOID,  VOID, 0) \
  X(return_to_save,    VOID, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(quick_slot,        RECORD, 1, INT,  VOID,  VOID,  VOID, 0) \
  X(set_quick_slot,    VOID, 2, INT,   RECORD, VOID,  VOID, 0) \
  X(set_knockback,     VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(say_line,          VOID, 1, RECORD, VOID,  VOID,  VOID, 0) \
  X(say_as,            VOID, 2, RECORD, TEXT,  VOID,  VOID, 0) \
  X(fade_music,        VOID, 2, INT,   INT,   VOID,  VOID, 0) \
  X(music_intensity,   VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(teleport,          BOOL, 3, NAME,  INT,   INT,   VOID, 0) \
  X(screen_effect,     VOID, 3, NAME,  INT,   INT,   VOID, 0) \
  X(flip_gravity,      BOOL, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(aim,               VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(held,              BOOL, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(on_ground,         BOOL, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(bounce,            VOID, 1, FIX,   VOID,  VOID,  VOID, 0) \
  X(lock_move,         VOID, 2, ENUM,  ENUM,  VOID,  VOID, 0) \
  X(ui_map,            VOID, 4, INT,   INT,   INT,   INT,  0) \
  X(map_percent,       INT,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(map_reveal,        BOOL, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(set_damage,        VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(damage_is,         BOOL, 1, ENUM,  VOID,  VOID,  VOID, 0) \
  X(owner_velocity,    VOID, 2, FIX,   FIX,   VOID,  VOID, 0) \
  X(hit_option,        VOID, 2, NAME,  INT,   VOID,  VOID, 0) \
  X(hit_knock,         VOID, 2, FIX,   FIX,   VOID,  VOID, 0) \
  X(fly,               VOID, 2, FIX,   FIX,   VOID,  VOID, 0) \
  X(fall,              VOID, 1, FIX,   VOID,  VOID,  VOID, 0) \
  X(lifetime,          VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(alive,             INT,  1, NAME,  VOID,  VOID,  VOID, 0) \
  X(effect,            VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(set_palette,       VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(flash,             VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(sin,               FIX,  1, INT,   VOID,  VOID,  VOID, 0) \
  X(cos,               FIX,  1, INT,   VOID,  VOID,  VOID, 0) \
  X(angle_to_point,    INT,  2, FIX,   FIX,   VOID,  VOID, 0) \
  X(layer_aim,         VOID, 2, NAME,  INT,   VOID,  VOID, 0) \
  X(layer_turn,        VOID, 3, NAME,  INT,   INT,   VOID, 0) \
  X(layer_angle,       INT,  1, NAME,  VOID,  VOID,  VOID, 0) \
  X(layer_play,        VOID, 2, NAME,  NAME,  VOID,  VOID, 0) \
  X(layer_show,        VOID, 2, NAME,  NAME,  VOID,  VOID, 0) \
  X(layer_hide,        VOID, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(layer_palette,     VOID, 2, NAME,  NAME,  VOID,  VOID, 0) \
  X(layer_point_x,     FIX,  2, NAME,  NAME,  VOID,  VOID, 0) \
  X(layer_point_y,     FIX,  2, NAME,  NAME,  VOID,  VOID, 0) \
  X(layer_point_angle, INT,  2, NAME,  NAME,  VOID,  VOID, 0) \
  X(spawn_at_point,    BOOL, 3, NAME,  NAME,  NAME,  VOID, 0) \
  X(fly_angle,         VOID, 2, INT,   FIX,   VOID,  VOID, 0) \
  X(handle_valid,      BOOL, 1, OPTIONAL, VOID, VOID, VOID, 0) \
  X(nearest_enemy,     OPTIONAL, 2, INT, NAME,  VOID,  VOID, 0) \
  X(distance_to,       INT,  1, HANDLE, VOID, VOID,  VOID, 0) \
  X(angle_to,          INT,  1, HANDLE, VOID, VOID,  VOID, 0) \
  X(owner_face,        VOID, 1, HANDLE, VOID, VOID,  VOID, 0) \
  X(layer_track,       VOID, 3, NAME,  HANDLE, INT,  VOID, 0) \
  X(marker_show,       VOID, 2, HANDLE, NAME, VOID,  VOID, 0) \
  X(marker_hide,       VOID, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(self,              HANDLE, 0, VOID, VOID, VOID,  VOID, 0) \
  X(color_correction,  BOOL, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(set_color_correction, VOID, 1, BOOL, VOID, VOID,  VOID, 0) \
  X(has_color_correction, BOOL, 0, VOID, VOID, VOID,  VOID, 0) \
  X(save_game,         VOID, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(character,         NAME, 0, VOID,  VOID,  VOID,  VOID, 0) \
  X(set_character,     BOOL, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(has_character,     BOOL, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(character_count,   INT,  0, VOID,  VOID,  VOID,  VOID, 0) \
  X(owner_turn,        VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(set_light,         VOID, 3, INT,   INT,   INT,   VOID, 0) \
  X(flag,              BOOL, 1, NAME,  VOID,  VOID,  VOID, 0) \
  X(set_flag,          VOID, 2, NAME,  BOOL,  VOID,  VOID, 0) \
  X(set_glow,          VOID, 1, INT,   VOID,  VOID,  VOID, 0) \
  X(draw_in_front,     VOID, 1, BOOL,  VOID,  VOID,  VOID, 0) \
  X(set_snow,          VOID, 1, INT,   VOID,  VOID,  VOID, 0)

// record_get(table, key, field), record_count(table), record_at(table, index): what the
// compiler makes of record<S> values (docs/plans/structures.md §6). table: string id of the
// structure's name; field: offset | type << 16 (BmDataType).
//
// item_count(record), give(record, amount), take(record, amount): the inventory
// (src/world/inventory.h); an argument of type RECORD takes a record of any structure (or
// #Name). stat(value), add_stat(value, amount): the player's own stat values, numbered by the
// values of an enum the game declares (by convention `enum Stat { ... }`, at most 16).
//
// attach(slot, #Class), attach_for(slot, #Class, frames), detach(slot): the player's
// attachment slots (src/world/attachments.h); attach replaces what is in the slot and is false
// if there is no Attachment class of that name. impulse(vx, vy): added to the player's speed
// once. room_attach(slot, #Class), room_detach(slot): the same for the room's 4 slots; the
// room detaches them when it is left.
//
// arm(#box, damage, element), disarm(#box), hit_box(reach, frames, damage, element): hits
// (src/world/hits.h, docs/plans/hitboxes.md §3). arm makes the hit boxes of that name on the
// frames the figure shows live as one hit, until disarm, the figure's animation changing or
// ending, or the figure being struck; hit_box is a hit without a drawing: a rectangle from the
// figure's anchor `reach` pixels ahead (a negative reach: that far on both sides), as high as
// its body, for `frames` frames. From an attachment the figure is the player.
//
// aim(#Up | #Down | #Low | #Ahead): the figure's next hit_box lies above its head, below its
// feet, ahead in the lower half of its body, or ahead as usual (src/world/hits.h); used up by
// that hit_box. held(#Up | #Down | #Left | #Right | #Fwd | #Back | #Jump | #Attack | ...): the
// d-pad or a game button is held now (Fwd and Back: the way the player faces). on_ground(): the
// player stands. bounce(speed): the player's upward speed is `speed` now (a stab downwards that
// strikes).
//
// lock_move(ground, air): how the player moves while the lock owner_lock just set holds them
// (src/world/game.h: on the ground 0 slide, 1 stop, 2 keep, 3 walk; in the air 0 steer, 1 keep,
// 2 drift; the enums GroundMove and AirMove of the test level's data). owner_lock sets slide
// and steer, player_lock slide and drift.
//
// flip_gravity(): the player's gravity turns over (standing on ceilings, src/world/body.h) or
// back; true if it now pulls up.
//
// equip(slot, record): puts a record the player owns into equipment slot 0 to 7 (0: empties
// it); the script its first `attach` field names is attached in the player's attachment slot of
// the same number, its first record<S> variable set to the record. False if the player has none.
// owner_lock(frames), owner_play_anim(#anim), owner_busy(): an attachment's (the player's or
// the room's: a bench to sit on) actions on the player (attachable-scripts.md §3): no walking or jumping for that many frames; an animation
// of the player's sprite that the engine does not replace until the lock is over (or, without a
// lock, until it ends); whether a lock is on. Nothing from an object.
//
// Hit options (hitboxes.md §3): hit_option(#Pierce, 0) (not stopped by guard boxes),
// hit_option(#Multi, n) (strikes the same figure again every n frames), hit_option(#Stop, n)
// (the game holds still n frames when it strikes), hit_option(#Reflect, 0) (turns a projectile
// around instead of striking it), hit_knock(x, y) (the push of what it strikes, fix) apply to the
// next arm or hit_box of the figure that calls them (the player for its attachments).
// Projectiles (hitboxes.md §5): fly(vx, vy), fall(gravity) and lifetime(frames) make the engine
// move an object (it gets wall() when its middle enters a solid cell) and destroy it after that
// many frames; an object made with spawn takes the team of whoever made it, and an attachment
// that made it hears struck() of its hits too; alive(#Class) counts that class's objects.
// effect(#sprite): that sprite's first animation once at the object's anchor, in front of the
// figures (as the engine's sparks and debris: bmGameEffect); only seen, not state.
// set_palette(#name): the object is drawn with that palette of its sprite (layered-sprites.md
// §2; one it does not have: its own), as the room object's property `palette` does.
// flash(frames): it is drawn all white for that many frames.
//
// Angles and the player's figure (layered-sprites.md §6; src/world/figure.h): angles are whole
// numbers, 256 a turn, 0 to the right, clockwise on screen (64 down, 192 up); sin(a), cos(a) are
// fix from tables, the same on every machine. angle_to_point(x, y): from the figure's anchor (an
// attachment: the player's) to a place in the room. layer_aim(#layer, angle) sets the angle of a
// layer of the player's figure (a layer whose animation is a direction set shows the step
// nearest to it), layer_turn(#layer, angle, speed) turns it there by at most `speed` a frame,
// layer_angle(#layer) reads it. layer_play(#layer, #anim): the layer plays that animation of its
// sprite on its own (a name its sprite does not have: back to what the figure says);
// layer_show(#layer, #sprite) shows another sprite there (#default: the figure's again) and
// shows a hidden layer, layer_hide(#layer) hides it (what hangs on it stays in place),
// layer_palette(#layer, #palette) a palette of its sprite (#default). layer_point_x/y(#layer,
// #point): where that point of the layer's current frame is (room pixels, fix; without it: the
// layer's anchor), layer_point_angle its world angle. spawn_at_point(#Class, #layer, #point):
// spawn at the point, the new object's `angle` the point's angle (facing that way).
// fly_angle(angle, speed): fly at `speed` (fix, a frame) in that direction. The layer functions
// work from the player's attachments; from an object they do nothing yet (objects show one
// sprite) and read 0.
//
// Filters (attachable-scripts.md §3): the player's attachments get damage_in(amount) in slot
// order before the player is struck (after defence and resistance), damage_out(amount) before
// a hit of the player strikes something (before its defence and weakness); set_damage(n) in
// them changes the amount the next one gets and the strike uses (0 or less: no strike);
// damage_is(element) tells the element. owner_velocity(vx, vy): an attachment sets the
// player's speed (a dash).
//
// The world map (src/world/map.h, docs/spec/world-format.md): ui_map(x, y, w, h) draws it into
// w x h cells of the UI layer, centred on the player, whose cell blinks with the game frame (a
// screen draws it again every few frames); map_percent() is how much of it was explored;
// map_reveal(#room_id) shows a room's cells without exploring them (false if it is not on the
// map).
//
// Screens and the UI layer (src/ui/ui.h; ui.md §3-4): ui_frame(x, y, w, h), ui_label(x, y,
// text), ui_number(x, y, value, digits), ui_text(x, y, w, text) (wrapped, down to the last
// row), ui_gauge(x, y, w, percent), ui_clear(x, y, w, h), ui_show(on); open_screen(#Class)
// puts a Screen class on the stack (false if there is none of that name or the stack is full),
// close_screen() closes the top one. language(), set_language(i), language_count(),
// language_name(i): the texts' language (an option, text.h). color_correction(),
// set_color_correction(on), has_color_correction(): the colours corrected for the GBA's screen
// (an option too; has_: the pack holds a correction, game.h). equipped(slot): the record in an
// equipment slot (0: none); record_text(record, #field), record_int(record, #field): a field of
// a record of any structure, by name (a text's string id; 0 if there is no such field). heal(n),
// max_health(), player_health(): the player's health.
//
// Objects made at run time (attachable-scripts.md §6): spawn(#Class, x, y) an Actor class at a
// place, spawn_with(#Class, record) at the object's own anchor with the new one's first
// record<S> variable set (an item drop); false when all 8 objects are in use. destroy(): the
// object is gone. floor_y(): the top of the floor below the object's anchor (fix; the room's
// bottom if there is none). say(speaker, line): the dialogue box with the speaker's name and the
// line; dialogue_done() becomes true when the player has pressed A. player_lock(frames): the
// player cannot walk or jump for that many frames (0 ends it).
//
// Elements, mana, items (first-items.md §1, §6, §7): add_resist(element, n) counts what the
// player wears that resists an element (n -1 takes it off again; a resisted element halves the
// damage after defence); set_affinity(weak, resists) gives the object a weakness (x 1.5) and a
// resistance (x 0.5), Element.None for neither; strike_back(amount), from an attachment's
// hit(): strikes the object that struck the player (false if there is none, as for a hazard);
// add_speed(percent): added to the player's running speed (-6 for heavy armour). mana(),
// max_mana(), restore_mana(n), spend_mana(n) (false, and nothing spent, if there is not enough).
// use_item(record): the item's behaviour attached for a frame in slot 7 (it does what the item
// does and takes it); false if the player has none. return_to_save(): to the last save point,
// at the next frame, the menus closed. quick_slot(i), set_quick_slot(i, record): the two quick
// slots (L and Select use them). set_knockback(percent): how much of a hit's push the object
// takes (100 at its start, 0 for one that does not budge: a boss, a dummy).
//
// say_line(record): the dialogue box with a Line record: its `speaker` (a record whose `name`
// and `portrait` are shown) and what it `says`. say_as(speaker, text): the same with a line
// written in the script. The portrait is a sprite of the pack, at the box's left; the
// speaker's `voice` (a sound effect) is heard with every second letter as the line appears.
//
// save_game(): writes the save game as a SavePoint does, with the player's place now as the
// place to start again from (a bench to rest on).
//
// The playable character (an option, game.h): character() is the name of the figure the player
// is drawn with (#player, #player_mitsuki), set_character(#figure) makes another of the pack's
// playable figures ("player" or "player_<name>") the player (false if it is none),
// has_character(#figure) tells whether the pack has it (with all its sprites),
// character_count() how many such the pack has.
//
// owner_turn(side): from the player's or the room's attachments, the player faces left (side
// below 0) or right (above 0); 0 changes nothing (a scene that draws the player in a place:
// Mitsuki in her bed).
//
// set_light(colour, intensity, ambient): the lit room's light from now on (until the room is
// entered again): its colour 0xRRGGBB, its intensity in percent (100: as `intensity` 1.0) and
// the room's ambient in percent (100: as drawn). A light switch: the moon through the window,
// then a warm lamp. Nothing in a room without light.
//
// set_glow(percent): a lit room's switched glowing colours (its property glow_switch: a
// window lit from inside) at that much of as drawn (100 when the room is entered; 0 black).
//
// set_snow(percent): snow falling over the room, that much of the most (0: none; 24 flakes at
// 100), in front of everything but the UI; until the room is left. Nothing in the multiboot images.
//
// draw_in_front(on): the object drawn in front of the player (on) or behind it, as objects are
// (off; off when it is made): one held in the player's arms. Nothing in the multiboot images.
//
// flag(#name), set_flag(#name, on): story flags (docs/plans/dialogue.md §5, a first part): a
// name that is on or off, the same in every room and kept in the save game (in the inventory,
// under the name's key changed so no record has it: no menu lists them).
//
// fade_music(percent, frames): the music's volume goes to `percent` (0-100) in that many
// frames; at 0 it stops. music_intensity(level): dynamic music: a layer of the song is heard while
// level lies in its range (from to until), fading in and out over the song's fade time;
// layers whose range ends below 15 make modes of a song that give way to each other (set it
// before play_music for a song to begin that way; a room starts its song at the song's own
// intensity).
//
// teleport(#room_id, x, y): the player to (x, y) of that room (#here or the room's own id:
// within it), with the wave (effects.h): false while one is under way or if there is no such
// room. screen_effect(#name, a, b): the room's effect from now on (#wave, #spotlight, #pulse,
// #gradient, #none; a and b 0 for the defaults).
#define BM_SCRIPT_RECORD_FIELD(offset, type) ((int32_t)(offset) | ((int32_t)(type) << 16))

typedef enum BmScriptApi {
#define X(name, result, argc, a0, a1, a2, a3, latent) BM_SCRIPT_API_##name,
  BM_SCRIPT_API(X)
#undef X
  BM_SCRIPT_API_COUNT
} BmScriptApi;

// Variables every actor has, read and written like fields. health: struck only while above
// 0; team: hits strike only figures of another team (0, the default: enemies; the player is
// 1); weight: 1 by default; the grapple pulls one of 2 or less to the player and pulls the
// player to a heavier one (grapple.h); angle: a direction (0 to 255, see the angles above),
// that of the point spawn_at_point made it at (it faces left if that points left), else 0. An attachment reads its owner's (the player's) and may write `health`.
// X(name, type, writable)
#define BM_SCRIPT_ACTOR_VARS(X) \
  X(x,      FIX, 1) \
  X(y,      FIX, 1) \
  X(home_x, FIX, 0) \
  X(home_y, FIX, 0) \
  X(facing, INT, 1) \
  X(health, INT, 1) \
  X(team,   INT, 1) \
  X(weight, INT, 1) \
  X(angle,  INT, 1)

typedef enum BmScriptActorVar {
#define X(name, type, writable) BM_SCRIPT_VAR_##name,
  BM_SCRIPT_ACTOR_VARS(X)
#undef X
  BM_SCRIPT_VAR_COUNT
} BmScriptActorVar;

// Kinds of class (attachable-scripts.md §2), by the base class a script extends.
typedef enum BmScriptKind {
  BM_SCRIPT_KIND_ACTOR = 0,        // `extends Actor`: an object of a room
  BM_SCRIPT_KIND_ATTACHMENT = 1,   // `extends Attachment`: attached to the player at run time
  BM_SCRIPT_KIND_SCREEN = 2,       // `extends Screen`: a menu or other screen (ui.md §4)
  BM_SCRIPT_KIND_COUNT
} BmScriptKind;

// Events the runtime calls. hit(amount): the figure was struck (its health already lowered;
// an attachment gets it when the player was); struck(amount): a hit of this figure or
// attachment struck another; damage_in(amount), damage_out(amount): filters, see set_damage;
// blocked(amount): a guard box of the figure stopped a hit; guarded(amount): a guard box stopped
// a hit of this figure or attachment; wall(): a moving object (fly) entered a solid cell.
// X(name, type of the parameter (VOID: none), kinds that get it:
// 1 Actor, 2 Attachment)
#define BM_SCRIPT_EVENTS(X) \
  X(spawn,    VOID, 1) \
  X(tick,     VOID, 7) \
  X(touch,    VOID, 1) \
  X(attached, VOID, 2) \
  X(detached, VOID, 2) \
  X(press,    NAME, 6) \
  X(release,  NAME, 6) \
  X(jumped,   VOID, 2) \
  X(landed,   VOID, 2) \
  X(hit,      INT,  3) \
  X(struck,   INT,  3) \
  X(opened,   VOID, 4) \
  X(closed,   VOID, 4) \
  X(damage_in,  INT, 2) \
  X(damage_out, INT, 2) \
  X(blocked,  INT,  3) \
  X(guarded,  INT,  3) \
  X(wall,     VOID, 1)

typedef enum BmScriptEvent {
#define X(name, arg, kinds) BM_SCRIPT_EVENT_##name,
  BM_SCRIPT_EVENTS(X)
#undef X
  BM_SCRIPT_EVENT_COUNT
} BmScriptEvent;

// Runtime functions only an Actor may call: they act on the object itself. X(name)
// (owner_velocity, like owner_lock, does nothing from anything but the player's attachments.)
#define BM_SCRIPT_ACTOR_ONLY(X) \
  X(finish_anim) X(play_anim) X(anim_done) X(player_distance) X(move_toward_player) X(move_toward) \
  X(enter_door) X(hide) X(show) X(face_player) X(touching_player) X(destroy) X(spawn_with) X(floor_y) \
  X(set_affinity) X(set_knockback) X(fly) X(fall) X(lifetime) X(effect) X(set_palette) X(flash) X(fly_angle)

// Field flags in the class table: the storage flags of docs/spec/state.md.
#define BM_SCRIPT_FIELD_SAVE 1
#define BM_SCRIPT_FIELD_LINK 2
#define BM_SCRIPT_FIELD_TRANSIENT 4
#define BM_SCRIPT_FIELD_EDITABLE 8   // var(Category): settable from object properties

// Limits of this version.
#define BM_SCRIPT_MAX_FIELDS 8        // per class
#define BM_SCRIPT_MAX_REGISTERS 32    // per function
#define BM_SCRIPT_MAX_ARGS 3         // parameters of a script's own function
#define BM_SCRIPT_MAX_API_ARGS 4     // arguments of a runtime function
