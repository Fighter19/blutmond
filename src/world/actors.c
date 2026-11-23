/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "actors.h"

#include <string.h>

#include "attachments.h"
#include "screens.h"
#include "body.h"
#include "data.h"
#include "effects.h"
#include "game.h"
#include "hal.h"
#include "hits.h"
#include "native.h"
#include "text.h"
#include "ui.h"
#include "video.h"

#ifdef BM_PLATFORM_HOST
#  include <stdio.h>
#endif

uint32_t bm_script_errors;
uint32_t bm_script_cycles;

// --- Helpers --------------------------------------------------------------------------------

// The module an actor's class is in; its instance names the class by its index there.
static const BmVmModule *moduleOf(const BmActors *a, uint8_t i)
{
  return &a->modules[a->classModule[a->classIndex[i]]];
}

const BmVmModule *bmActorsModule(const BmActors *a, uint8_t i, uint16_t *pClassInModule)
{
  if (i >= a->count || a->classIndex[i] >= a->classCount) return NULL;
  if (pClassInModule != NULL) *pClassInModule = a->classInModule[a->classIndex[i]];
  return moduleOf(a, i);
}

// The room object of actor i; one made at run time (spawn) has none and gets a point without
// a name.
static const BmObjectView *objectOf(const BmGame *g, uint8_t i)
{
  static const BmObjectView s_none = { "", "", 0xFFFF, 0, 0, 0, 0, 0, NULL, BM_OBJECT_OTHER };
  uint8_t o = g->actors.object[i];
  return o < g->objectCount ? &g->objects[o] : &s_none;
}

static void loadInstance(const BmActors *a, uint8_t i, BmVmInstance *inst)
{
  inst->classIndex = a->classInModule[a->classIndex[i]];
  inst->state = a->state[i];
  inst->wait = a->wait[i];
  inst->pc = a->pc[i];
  inst->timer = a->timer[i];
  memcpy(inst->fields, &a->fields[i * BM_SCRIPT_MAX_FIELDS], sizeof(inst->fields));
}

static void storeInstance(BmActors *a, uint8_t i, const BmVmInstance *inst)
{
  a->state[i] = inst->state;
  a->wait[i] = inst->wait;
  a->pc[i] = inst->pc;
  a->timer[i] = inst->timer;
  memcpy(&a->fields[i * BM_SCRIPT_MAX_FIELDS], inst->fields, sizeof(inst->fields));
}

static uint32_t hashName(const char *s)
{
  uint32_t h = 2166136261u;
  for (; s != NULL && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

static int32_t absolute(int32_t v)
{
  return v < 0 ? -v : v;
}

// Distance without a square root: the longer side plus half the shorter (at most 12% off).
static int32_t distance(int32_t dx, int32_t dy)
{
  dx = absolute(dx);
  dy = absolute(dy);
  return dx > dy ? dx + dy / 2 : dy + dx / 2;
}

static BmAnimState animOf(const BmActors *a, uint8_t i)
{
  return (BmAnimState){ a->anim[i], a->animFinished[i], a->animStep[i], a->animTimer[i] };
}

static void setAnim(BmActors *a, uint8_t i, const BmAnimState *st)
{
  a->anim[i] = st->anim;
  a->animFinished[i] = st->finished;
  a->animStep[i] = st->step;
  a->animTimer[i] = st->timer;
}

// The object's box in room pixels: its rectangle, its sprite's hitbox, or 16x16 on the anchor.
static void boxOf(const BmGame *g, uint8_t i, int32_t *px, int32_t *py, int32_t *pw, int32_t *ph)
{
  const BmActors *a = &g->actors;
  const BmObjectView *o = objectOf(g, i);
  int32_t x = BM_FIX_INT(a->x[i]), y = BM_FIX_INT(a->y[i]);
  if (o->w != 0 && o->h != 0)
  {
    *px = x - o->w / 2;
    *py = y - o->h;
    *pw = o->w;
    *ph = o->h;
  }
  else if (a->sheet[i] >= 0)
  {
    const BmSpriteSheet *s = &g->sheets[a->sheet[i]];
    BmAnimState st = animOf(a, i);
    BmSpriteBox b = bmSpriteSheetHitbox(s, bmAnimFrame(s, &st), a->facing[i] < 0);
    *px = x + b.x;
    *py = y + b.y;
    *pw = b.w;
    *ph = b.h;
  }
  else
  {
    *px = x - 8;
    *py = y - 16;
    *pw = 16;
    *ph = 16;
  }
}

void bmActorsBox(const BmGame *g, uint8_t i, int32_t *px, int32_t *py, int32_t *pw, int32_t *ph)
{
  boxOf(g, i, px, py, pw, ph);
}

static bool touchesPlayer(const BmGame *g, uint8_t i)
{
  int32_t x, y, w, h;
  boxOf(g, i, &x, &y, &w, &h);
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  int32_t hw = g->playerParams.halfWidth, ph = g->playerParams.height;
  return px - hw < x + w && x < px + hw && py - ph < y + h && y < py;
}

// The key of a `save` variable of the room's attachment in `slot`: like an object's, with
// 0xFFF0 + the room's slot number in place of the object's id.
static uint32_t roomSlotKey(const BmGame *g, uint8_t slot, uint16_t field)
{
  return ((g->roomIndex & 0xFF) << 24) | ((uint32_t)(0xFFF0u + slot - BM_ATTACH_ROOM_FIRST) << 8) | (field & 0xFF);
}

static uint32_t storeKey(const BmGame *g, uint8_t i, uint16_t field)
{
  return ((g->roomIndex & 0xFF) << 24) | ((uint32_t)objectOf(g, i)->persistentId << 8) | (field & 0xFF);
}

// --- What scripts call ------------------------------------------------------------------------

static int32_t classNamed(const BmGame *g, const char *name);
static void resetMotion(BmActors *a, uint8_t i);

static bool hostCall(void *pContext, BmVmInstance *inst, uint16_t api, const int32_t *args, int32_t *result)
{
  BmGame *g = pContext;
  BmActors *a = &g->actors;
  uint8_t i = a->current;
  // An attachment (current: BM_ACTORS_OWNER) has no object of its own. The compiler does not
  // let it call these; a module made by hand that does gets nothing done.
  if (i >= BM_ACTORS_MAX)
  {
    switch (api)
    {
#define X(name) case BM_SCRIPT_API_##name:
      BM_SCRIPT_ACTOR_ONLY(X)
#undef X
      return false;
    default:
      break;
    }
  }
  switch (api)
  {
  case BM_SCRIPT_API_sleep:
    inst->wait = BM_VM_SLEEPING;
    inst->timer = (uint16_t)(args[0] < 1 ? 1 : args[0] > 0xFFFF ? 0xFFFF : args[0]);
    return true;
  case BM_SCRIPT_API_finish_anim:
    inst->wait = BM_VM_FINISH_ANIM;
    return true;
  case BM_SCRIPT_API_play_anim:
    if (a->sheet[i] >= 0)
    {
      const BmSpriteSheet *s = &g->sheets[a->sheet[i]];
      int32_t anim = bmSpriteSheetAnim(s, g->pPack, bmPackString(g->pPack, (uint16_t)args[0]));
      if (anim >= 0)
      {
        BmAnimState st = animOf(a, i);
        bmAnimPlay(&st, (uint8_t)anim, false);
        setAnim(a, i, &st);
      }
    }
    break;
  case BM_SCRIPT_API_anim_done:
    *result = a->animFinished[i];
    break;
  case BM_SCRIPT_API_player_x:
    *result = g->player.x;
    break;
  case BM_SCRIPT_API_player_y:
    *result = g->player.y;
    break;
  case BM_SCRIPT_API_player_distance:
    // To the middle of the player.
    *result = distance(BM_FIX_INT(g->player.x - a->x[i]), BM_FIX_INT(g->player.y - a->y[i]) - g->playerParams.height / 2);
    break;
  case BM_SCRIPT_API_move_toward_player:
  case BM_SCRIPT_API_move_toward:
  {
    int32_t tx, ty, speed;
    if (api == BM_SCRIPT_API_move_toward_player)
    {
      tx = g->player.x;
      ty = g->player.y - BM_FIX(g->playerParams.height / 2);
      speed = args[0];
    }
    else
    {
      tx = args[0];
      ty = args[1];
      speed = args[2];
    }
    int32_t dx = tx - a->x[i], dy = ty - a->y[i];
    int32_t d = distance(dx, dy);
    if (speed <= 0) break;
    if (d <= speed)
    {
      a->x[i] = tx;
      a->y[i] = ty;
      *result = 1;
    }
    else
    {
      a->x[i] += (int32_t)(((int64_t)dx * speed) / d);
      a->y[i] += (int32_t)(((int64_t)dy * speed) / d);
      *result = 0;
    }
    break;
  }
  case BM_SCRIPT_API_hurt_player:
    // As a hazard does (pushed the way the player looks), but known as the striker.
    bmGameStrikePlayerWith(g, args[0], g->player.x + g->player.facing, 0, (uint8_t)(i + 1));
    break;
  case BM_SCRIPT_API_enter_door:
    a->pendingDoor = (uint8_t)(a->object[i] + 1);
    break;
  case BM_SCRIPT_API_hide:
    a->hidden[i] = 1;
    break;
  case BM_SCRIPT_API_show:
    a->hidden[i] = 0;
    break;
  case BM_SCRIPT_API_face_player:
    a->facing[i] = g->player.x < a->x[i] ? -1 : 1;
    break;
  case BM_SCRIPT_API_log:
#ifdef BM_PLATFORM_HOST
    printf("script: %s logs %d\n", objectOf(g, i)->className, (int)args[0]);
#endif
    break;
  case BM_SCRIPT_API_random:
    a->seed = a->seed * 1664525u + 1013904223u;
    *result = args[0] > 0 ? (int32_t)((a->seed >> 8) % (uint32_t)args[0]) : 0;
    break;
  case BM_SCRIPT_API_touching_player:
    *result = touchesPlayer(g, i);
    break;
  case BM_SCRIPT_API_give_item:
  case BM_SCRIPT_API_has_item:
  {
    // An item named #Key is the record Key's key: the same hash.
    uint32_t h = hashName(bmPackString(g->pPack, (uint16_t)args[0]));
    bool has = bmInventoryCount(&g->inventory, h) != 0;
    if (api == BM_SCRIPT_API_has_item) *result = has;
    else if (!has && bmInventoryGive(&g->inventory, h, 1) > 0)
    {
      bmGameNotice(g, h, (uint16_t)args[0], 1);
      g->pickupSound = 1;   // the pickup sound, next frame (game.c)
    }
    break;
  }
  case BM_SCRIPT_API_add_stat:
    if (i == BM_ACTORS_OWNER)
    {
      // From one of the player's attachments: a bonus while it is attached (it takes it away
      // again in detached()), not a change of the player's own value.
      if (args[0] >= 0 && args[0] < BM_INVENTORY_STATS)
      {
        int32_t v = g->inventory.statBonus[args[0]] + args[1];
        int16_t now = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        // Kept per slot, to be taken away again when the attachment goes.
        BmAttachments *t = &g->attachments;
        uint8_t *of = &t->statOf[a->currentSlot * BM_ATTACH_STATS];
        int16_t *added = &t->statAdded[a->currentSlot * BM_ATTACH_STATS];
        for (int k = 0; k < BM_ATTACH_STATS && a->currentSlot < BM_ATTACH_SLOTS; k++)
          if (of[k] == args[0] + 1 || of[k] == 0)
          {
            of[k] = (uint8_t)(args[0] + 1);
            added[k] = (int16_t)(added[k] + (now - g->inventory.statBonus[args[0]]));
            break;
          }
        g->inventory.statBonus[args[0]] = now;
      }
      break;
    }
    bmInventoryScriptCall(&g->inventory, api, args, result);
    break;
  case BM_SCRIPT_API_item_count:
  case BM_SCRIPT_API_give:
  case BM_SCRIPT_API_take:
  case BM_SCRIPT_API_stat:
    bmInventoryScriptCall(&g->inventory, api, args, result);
    // An ability obtained or lost: its stats and its script.
    if ((api == BM_SCRIPT_API_give || api == BM_SCRIPT_API_take) && *result > 0) bmGameInventoryChanged(g);
    // What the player obtains is shown at the bottom left.
    if (api == BM_SCRIPT_API_give && *result > 0)
    {
      bmGameNotice(g, (uint32_t)args[0], 0xFFFF, *result);
      g->pickupSound = 1;
    }
    break;
  case BM_SCRIPT_API_attach:
  case BM_SCRIPT_API_attach_for:
  {
    uint16_t lifetime = api == BM_SCRIPT_API_attach_for ? (uint16_t)(args[2] < 1 ? 1 : args[2] > 0xFFFF ? 0xFFFF : args[2]) : 0;
    uint8_t current = a->current, slot = a->currentSlot;
    *result = args[0] >= 0 && args[0] < BM_ATTACH_PLAYER_SLOTS && bmAttachmentsAttach(g, (uint8_t)args[0], (uint16_t)args[1], lifetime);
    a->current = current;
    a->currentSlot = slot;
    break;
  }
  case BM_SCRIPT_API_detach:
  case BM_SCRIPT_API_room_detach:
  {
    int32_t first = api == BM_SCRIPT_API_detach ? 0 : BM_ATTACH_ROOM_FIRST;
    int32_t count = api == BM_SCRIPT_API_detach ? BM_ATTACH_PLAYER_SLOTS : BM_ATTACH_ROOM_SLOTS;
    if (args[0] >= 0 && args[0] < count)
    {
      uint8_t current = a->current, slot = a->currentSlot;
      bmAttachmentsDetach(g, (uint8_t)(first + args[0]));
      a->current = current;
      a->currentSlot = slot;
    }
    break;
  }
  case BM_SCRIPT_API_room_attach:
  {
    uint8_t current = a->current, slot = a->currentSlot;
    *result = args[0] >= 0 && args[0] < BM_ATTACH_ROOM_SLOTS && bmAttachmentsAttach(g, (uint8_t)(BM_ATTACH_ROOM_FIRST + args[0]), (uint16_t)args[1], 0);
    a->current = current;
    a->currentSlot = slot;
    break;
  }
  case BM_SCRIPT_API_arm:
  case BM_SCRIPT_API_disarm:
  case BM_SCRIPT_API_hit_box:
  {
    // The figure: the object itself, or for the player's attachments the player. The room has
    // no figure to hit from.
    if (i == BM_ACTORS_ROOM) break;
    uint8_t source = i < BM_ACTORS_MAX ? (uint8_t)(1 + i) : BM_HITS_PLAYER;
    uint8_t slot = i < BM_ACTORS_MAX ? BM_HITS_NO_SLOT : a->currentSlot;
    if (api == BM_SCRIPT_API_arm) bmHitsArm(g, source, slot, (uint16_t)args[0], args[1], (uint8_t)args[2], 0, 0);
    else if (api == BM_SCRIPT_API_disarm) bmHitsDisarm(g, source, (uint16_t)args[0]);
    else bmHitsArm(g, source, slot, 0xFFFF, args[2], (uint8_t)(args[3] > 0 && args[3] < BM_GAME_ELEMENTS ? args[3] : 0), args[0], args[1]);
    break;
  }
  case BM_SCRIPT_API_equip:
  {
    uint8_t current = a->current, slot = a->currentSlot;
    *result = args[0] >= 0 && args[0] < BM_INVENTORY_EQUIP && bmAttachmentsEquip(g, (uint8_t)args[0], (uint32_t)args[1]);
    a->current = current;
    a->currentSlot = slot;
    break;
  }
  case BM_SCRIPT_API_owner_lock:
  case BM_SCRIPT_API_owner_play_anim:
  case BM_SCRIPT_API_owner_busy:
    // Only the player's attachments act on their owner.
    if (i != BM_ACTORS_OWNER) break;
    if (api == BM_SCRIPT_API_owner_busy) *result = g->lockTimer != 0;
    else if (api == BM_SCRIPT_API_owner_lock)
    {
      g->lockTimer = (uint16_t)(args[0] < 0 ? 0 : args[0] > 0xFFFF ? 0xFFFF : args[0]);
      // An attack: no walking, the speed fades; steering in the air (lock_move changes it).
      g->lockGround = BM_LOCK_SLIDE;
      g->lockAir = BM_LOCK_STEER;
    }
    else bmGamePlayPlayerAnim(g, (uint16_t)args[0]);
    break;
  case BM_SCRIPT_API_ui_frame:
    bmUiFrame(&g->ui, args[0], args[1], args[2], args[3]);
    break;
  case BM_SCRIPT_API_ui_label:
    bmUiLabel(&g->ui, args[0], args[1], bmText(g->pPack, g->language, (uint16_t)args[2]));
    break;
  case BM_SCRIPT_API_ui_number:
    bmUiNumber(&g->ui, args[0], args[1], args[2], (uint8_t)(args[3] < 1 ? 1 : args[3] > 11 ? 11 : args[3]));
    break;
  case BM_SCRIPT_API_ui_text:
    bmUiText(&g->ui, args[0], args[1], args[2], BM_UI_ROWS - args[1], (uint16_t)args[3], NULL, 0);
    break;
  case BM_SCRIPT_API_ui_gauge:
    bmUiGauge(&g->ui, args[0], args[1], args[2], args[3], 100);
    break;
  case BM_SCRIPT_API_ui_clear:
    bmUiClearRect(&g->ui, args[0], args[1], args[2], args[3]);
    break;
  case BM_SCRIPT_API_hit_option:
  case BM_SCRIPT_API_hit_knock:
  {
    // For the figure calling: the object, or the player for its attachments.
    if (i == BM_ACTORS_ROOM) break;
    uint8_t f = i < BM_ACTORS_MAX ? (uint8_t)(1 + i) : BM_HITS_PLAYER;
    if (api == BM_SCRIPT_API_hit_option) bmHitsOption(g, f, bmPackString(g->pPack, (uint16_t)args[0]), args[1]);
    else bmHitsKnock(g, f, args[0], args[1]);
    break;
  }
  case BM_SCRIPT_API_fly:
    a->vx[i] = args[0];
    a->vy[i] = args[1];
    break;
  case BM_SCRIPT_API_fall:
    a->fall[i] = args[0];
    break;
  case BM_SCRIPT_API_lifetime:
    a->life[i] = (uint16_t)(args[0] < 0 ? 0 : args[0] > 0xFFFF ? 0xFFFF : args[0]);
    break;
  case BM_SCRIPT_API_alive:
  {
    const char *name = bmPackString(g->pPack, (uint16_t)args[0]);
    int32_t cls = name != NULL ? classNamed(g, name) : -1;
    int32_t n = 0;
    for (uint8_t k = 0; cls >= 0 && k < a->count; k++) n += !a->gone[k] && a->classIndex[k] == (uint16_t)cls;
    *result = n;
    break;
  }
  case BM_SCRIPT_API_set_damage:
    g->filterAmount = args[0];
    break;
  case BM_SCRIPT_API_damage_is:
    *result = g->filterElement == (uint8_t)args[0];
    break;
  case BM_SCRIPT_API_owner_velocity:
    // Only the player's attachments move their owner.
    if (i != BM_ACTORS_OWNER) break;
    g->player.vx = args[0];
    g->player.vy = args[1];
    break;
  case BM_SCRIPT_API_ui_map:
    bmMapDraw(g, args[0], args[1], args[2], args[3]);
    break;
  case BM_SCRIPT_API_map_percent:
    *result = bmMapPercent(g);
    break;
  case BM_SCRIPT_API_map_reveal:
    *result = bmMapReveal(g, (uint16_t)args[0]);
    break;
  case BM_SCRIPT_API_ui_show:
    bmUiShow(&g->ui, args[0] != 0);
    break;
  case BM_SCRIPT_API_open_screen:
  {
    uint8_t current = a->current, slot = a->currentSlot;
    *result = bmScreensOpen(g, (uint16_t)args[0]);
    a->current = current;
    a->currentSlot = slot;
    break;
  }
  case BM_SCRIPT_API_close_screen:
  {
    uint8_t current = a->current, slot = a->currentSlot;
    bmScreensClose(g);
    a->current = current;
    a->currentSlot = slot;
    break;
  }
  case BM_SCRIPT_API_language:
    *result = g->language;
    break;
  case BM_SCRIPT_API_set_language:
    if (args[0] >= 0 && args[0] < bmTextLanguageCount(g->pPack) && args[0] != g->language)
    {
      uint8_t current = a->current, slot = a->currentSlot;
      bmGameSetLanguage(g, (uint8_t)args[0]);
      bmScreensRedraw(g);   // every open screen in the new language
      a->current = current;
      a->currentSlot = slot;
    }
    break;
  case BM_SCRIPT_API_language_count:
    *result = bmTextLanguageCount(g->pPack);
    break;
  case BM_SCRIPT_API_language_name:
    *result = args[0] >= 0 && args[0] < 256 ? bmTextLanguageName(g->pPack, (uint8_t)args[0]) : 0xFFFF;
    break;
  case BM_SCRIPT_API_equipped:
    *result = args[0] >= 0 && args[0] < BM_INVENTORY_EQUIP ? (int32_t)g->inventory.equipped[args[0]] : 0;
    break;
  case BM_SCRIPT_API_record_text:
  case BM_SCRIPT_API_record_int:
  {
    // A field by name in the record's own table, whatever structure that is.
    BmDataTable table;
    const uint8_t *record;
    *result = api == BM_SCRIPT_API_record_text ? 0xFFFF : 0;
    if (!bmDataFindRecord(g->pPack, (uint32_t)args[0], &table, &record)) break;
    for (uint16_t f = 0; f < table.fieldCount; f++)
    {
      BmDataField field = bmDataTableField(&table, f);
      if (field.name != (uint16_t)args[1]) continue;
      *result = bmDataRead(&table, record, field.offset, (BmDataType)field.type);
      break;
    }
    break;
  }
  case BM_SCRIPT_API_heal:
  {
    int32_t health = g->health + (args[0] > 0 ? args[0] : 0), max = bmGameMaxHealth(g);
    g->health = (int16_t)(health > max ? max : health);
    break;
  }
  case BM_SCRIPT_API_max_health:
    *result = bmGameMaxHealth(g);
    break;
  case BM_SCRIPT_API_player_health:
    *result = g->health;
    break;
  case BM_SCRIPT_API_add_resist:
    if (args[0] > 0 && args[0] < BM_GAME_ELEMENTS)
    {
      int32_t n = g->resists[args[0]] + args[1];
      g->resists[args[0]] = (uint8_t)(n < 0 ? 0 : n > 255 ? 255 : n);
    }
    break;
  case BM_SCRIPT_API_set_knockback:
    a->steady[i] = (uint8_t)(100 - (args[0] < 0 ? 0 : args[0] > 100 ? 100 : args[0]));
    break;
  case BM_SCRIPT_API_set_affinity:
    a->affinity[i] = (uint8_t)((args[0] > 0 && args[0] < 4 ? 1u << args[0] : 0) | (args[1] > 0 && args[1] < 4 ? 16u << args[1] : 0));
    break;
  case BM_SCRIPT_API_strike_back:
  {
    uint8_t current = a->current, slot = a->currentSlot;
    *result = g->striker != 0xFF && g->striker != 0 && bmHitsStrikeObject(g, (uint8_t)(g->striker - 1), args[0], 0, g->player.x);
    a->current = current;
    a->currentSlot = slot;
    break;
  }
  case BM_SCRIPT_API_add_speed:
  {
    int32_t n = g->speedPercent + args[0];
    g->speedPercent = (int16_t)(n < -90 ? -90 : n > 300 ? 300 : n);
    break;
  }
  case BM_SCRIPT_API_mana:
    *result = g->mana;
    break;
  case BM_SCRIPT_API_max_mana:
    *result = bmGameMaxMana(g);
    break;
  case BM_SCRIPT_API_restore_mana:
  {
    int32_t mana = g->mana + (args[0] > 0 ? args[0] : 0), max = bmGameMaxMana(g);
    g->mana = (int16_t)(mana > max ? max : mana);
    break;
  }
  case BM_SCRIPT_API_spend_mana:
    *result = args[0] >= 0 && g->mana >= args[0];
    if (*result) g->mana = (int16_t)(g->mana - args[0]);
    break;
  case BM_SCRIPT_API_use_item:
    *result = bmAttachmentsUse(g, (uint32_t)args[0]);
    break;
  case BM_SCRIPT_API_return_to_save:
    bmGameReturnToSave(g);
    break;
  case BM_SCRIPT_API_lock_move:
    if (i != BM_ACTORS_OWNER) break;
    g->lockGround = (uint8_t)(args[0] >= 0 && args[0] <= BM_LOCK_WALK ? args[0] : BM_LOCK_SLIDE);
    g->lockAir = (uint8_t)(args[1] >= 0 && args[1] <= BM_LOCK_DRIFT ? args[1] : BM_LOCK_STEER);
    break;
  case BM_SCRIPT_API_aim:
  {
    if (i == BM_ACTORS_ROOM) break;
    int32_t aim = bmHitAimNamed(bmPackString(g->pPack, (uint16_t)args[0]));
    if (aim >= 0) bmHitsAim(g, i < BM_ACTORS_MAX ? (uint8_t)(1 + i) : BM_HITS_PLAYER, (uint8_t)aim);
    break;
  }
  case BM_SCRIPT_API_held:
  {
    const char *n = bmPackString(g->pPack, (uint16_t)args[0]);
    uint16_t keys = g->keysPrevious, key = 0;
    if (n == NULL) break;
    if (strcmp(n, "Up") == 0) key = BM_KEY_UP;
    else if (strcmp(n, "Down") == 0) key = BM_KEY_DOWN;
    else if (strcmp(n, "Left") == 0) key = BM_KEY_LEFT;
    else if (strcmp(n, "Right") == 0) key = BM_KEY_RIGHT;
    else if (strcmp(n, "Fwd") == 0) key = g->player.facing < 0 ? BM_KEY_LEFT : BM_KEY_RIGHT;
    else if (strcmp(n, "Back") == 0) key = g->player.facing < 0 ? BM_KEY_RIGHT : BM_KEY_LEFT;
#define X(name, k) else if (strcmp(n, #name) == 0) key = k;
    BM_ATTACH_BUTTONS(X)
#undef X
    *result = (keys & key) != 0;
    break;
  }
  case BM_SCRIPT_API_on_ground:
    *result = g->player.onGround;
    break;
  case BM_SCRIPT_API_bounce:
    // Up the way the player's gravity is (down when it is flipped).
    g->player.vy = g->player.flipped ? args[0] : -args[0];
    g->player.onGround = false;
    break;
  case BM_SCRIPT_API_flip_gravity:
    g->player.flipped = !g->player.flipped;
    g->player.vy = 0;
    g->player.onGround = false;
    g->player.coyote = 0;
    *result = g->player.flipped;
    break;
  case BM_SCRIPT_API_quick_slot:
    *result = args[0] >= 0 && args[0] < BM_INVENTORY_QUICK ? (int32_t)g->inventory.quick[args[0]] : 0;
    break;
  case BM_SCRIPT_API_set_quick_slot:
    if (args[0] >= 0 && args[0] < BM_INVENTORY_QUICK) g->inventory.quick[args[0]] = (uint32_t)args[1];
    break;
  case BM_SCRIPT_API_say:
    bmGameSay(g, (uint16_t)args[0], (uint16_t)args[1]);
    break;
  case BM_SCRIPT_API_teleport:
  {
    const char *id = bmPackString(g->pPack, (uint16_t)args[0]);
    int32_t index = id != NULL && strcmp(id, "here") == 0 ? (int32_t)g->roomIndex : bmGameFindRoom(g->pPack, id);
    *result = index >= 0 && bmEffectsTeleport(g, (uint32_t)index, args[1], args[2]);
    break;
  }
  case BM_SCRIPT_API_screen_effect:
  {
    int32_t kind = bmEffectNamed(bmPackString(g->pPack, (uint16_t)args[0]));
    bmEffectsSet(g, kind > 0 ? (BmEffect)kind : BM_EFFECT_NONE, args[1], args[2]);
    break;
  }
  case BM_SCRIPT_API_fade_music:
  {
    int32_t percent = args[0] < 0 ? 0 : args[0] > 100 ? 100 : args[0];
    bmSoundFadeMusic(&g->sound, (uint16_t)(percent * 256 / 100), (uint16_t)(args[1] < 0 ? 0 : args[1] > 0xFFFF ? 0xFFFF : args[1]), percent == 0);
    break;
  }
  case BM_SCRIPT_API_music_intensity:
    bmSoundSetIntensity(&g->sound, (uint8_t)(args[0] < 0 ? 0 : args[0] > 15 ? 15 : args[0]));
    break;
  case BM_SCRIPT_API_say_line:
    bmGameSayLine(g, (uint32_t)args[0]);
    break;
  case BM_SCRIPT_API_say_as:
  {
    int32_t name = 0xFFFF, portrait = 0xFFFF;
    bmGameRecordField(g, (uint32_t)args[0], "name", &name);
    bmGameRecordField(g, (uint32_t)args[0], "portrait", &portrait);
    int32_t voice = 0xFFFF;
    bmGameRecordField(g, (uint32_t)args[0], "voice", &voice);
    bmGameSpeak(g, (uint16_t)name, (uint16_t)args[1], (uint16_t)portrait, (uint16_t)voice);
    break;
  }
  case BM_SCRIPT_API_dialogue_done:
    *result = g->dialogue == 0;
    break;
  case BM_SCRIPT_API_player_lock:
    g->lockTimer = (uint16_t)(args[0] < 0 ? 0 : args[0] > 0xFFFF ? 0xFFFF : args[0]);
    // A scene: the player comes to a stand, no steering.
    g->lockGround = BM_LOCK_SLIDE;
    g->lockAir = BM_LOCK_DRIFT;
    break;
  case BM_SCRIPT_API_spawn:
  case BM_SCRIPT_API_spawn_with:
  {
    uint8_t current = a->current, slot = a->currentSlot;
    if (api == BM_SCRIPT_API_spawn) *result = bmActorsSpawn(g, (uint16_t)args[0], args[1], args[2], 0);
    else *result = bmActorsSpawn(g, (uint16_t)args[0], a->x[i], a->y[i], (uint32_t)args[1]);
    a->current = current;
    a->currentSlot = slot;
    break;
  }
  case BM_SCRIPT_API_destroy:
    bmActorsDestroy(g, i);
    break;
  case BM_SCRIPT_API_floor_y:
  {
    int32_t floor = bmCollisionFloor(&g->collision, BM_FIX_INT(a->x[i]), BM_FIX_INT(a->y[i]) - 1, g->room.heightPx, true);
    *result = BM_FIX(floor == INT32_MAX ? g->room.heightPx : floor);
    break;
  }
  case BM_SCRIPT_API_impulse:
    g->player.vx += args[0];
    g->player.vy += args[1];
    break;
  case BM_SCRIPT_API_abs:
    *result = absolute(args[0]);
    break;
  case BM_SCRIPT_API_set_palette:
  {
    // A palette of its sprite by name; one it does not have: its own.
    int32_t palette = a->sheet[i] >= 0 ? bmSpriteSheetPalette(&g->sheets[a->sheet[i]], g->pPack, bmPackString(g->pPack, (uint16_t)args[0])) : -1;
    a->palette[i] = (uint8_t)(palette < 0 ? 0 : palette);
    break;
  }
  case BM_SCRIPT_API_flash:
    a->flash[i] = (uint8_t)(args[0] < 0 ? 0 : args[0] > 255 ? 255 : args[0]);
    break;
  case BM_SCRIPT_API_effect:
    bmGameEffect(g, (uint16_t)args[0], BM_FIX_INT(a->x[i]), BM_FIX_INT(a->y[i]));
    break;
  case BM_SCRIPT_API_play_sound:
    bmSoundPlayEffect(&g->sound, bmPackString(g->pPack, (uint16_t)args[0]));
    break;
  case BM_SCRIPT_API_play_music:
    bmSoundPlayMusic(&g->sound, bmPackString(g->pPack, (uint16_t)args[0]));
    break;
  default:
    bmDataScriptCall(g->pPack, api, args, result);   // record_get, record_count, record_at
    break;
  }
  return false;
}

static int32_t hostGetVar(void *pContext, BmVmInstance *inst, uint16_t var)
{
  (void)inst;
  BmGame *g = pContext;
  BmActors *a = &g->actors;
  uint8_t i = a->current;
  if (i == BM_ACTORS_ROOM)
  {
    // The room's attachments read the camera's position.
    if (var == BM_SCRIPT_VAR_x) return BM_FIX(g->room.cameraX);
    if (var == BM_SCRIPT_VAR_y) return BM_FIX(g->room.cameraY);
    if (var == BM_SCRIPT_VAR_facing) return 1;
    if (var == BM_SCRIPT_VAR_team) return BM_HITS_PLAYER_TEAM;
    return 0;
  }
  if (i >= BM_ACTORS_MAX)
  {
    // An attachment reads its owner's: the player's; so does a screen.
    if (var == BM_SCRIPT_VAR_x) return g->player.x;
    if (var == BM_SCRIPT_VAR_y) return g->player.y;
    if (var == BM_SCRIPT_VAR_facing) return g->player.facing;
    if (var == BM_SCRIPT_VAR_health) return g->health;
    if (var == BM_SCRIPT_VAR_team) return BM_HITS_PLAYER_TEAM;
    return 0;
  }
  switch (var)
  {
  case BM_SCRIPT_VAR_x: return a->x[i];
  case BM_SCRIPT_VAR_y: return a->y[i];
  case BM_SCRIPT_VAR_home_x: return a->homeX[i];
  case BM_SCRIPT_VAR_home_y: return a->homeY[i];
  case BM_SCRIPT_VAR_facing: return a->facing[i];
  case BM_SCRIPT_VAR_health: return a->health[i];
  case BM_SCRIPT_VAR_team: return a->team[i];
  default: return 0;
  }
}

static void hostSetVar(void *pContext, BmVmInstance *inst, uint16_t var, int32_t value)
{
  (void)inst;
  BmGame *g = pContext;
  BmActors *a = &g->actors;
  uint8_t i = a->current;
  if (i >= BM_ACTORS_MAX)
  {
    // An attachment cannot move its owner this way, but it can heal the player (to at least 1:
    // only a hit takes the last of it).
    if (i == BM_ACTORS_OWNER && var == BM_SCRIPT_VAR_health)
      g->health = (int16_t)(value < 1 ? 1 : value > BM_GAME_PLAYER_HEALTH ? BM_GAME_PLAYER_HEALTH : value);
    return;
  }
  switch (var)
  {
  case BM_SCRIPT_VAR_x: a->x[i] = value; break;
  case BM_SCRIPT_VAR_y: a->y[i] = value; break;
  case BM_SCRIPT_VAR_facing: a->facing[i] = (int8_t)(value < 0 ? -1 : 1); break;
  case BM_SCRIPT_VAR_health: a->health[i] = value; break;
  case BM_SCRIPT_VAR_team: a->team[i] = (uint8_t)value; break;
  default: break;
  }
}

// `save` variables are kept by room, object and field, so they survive leaving the room.
static void hostFieldChanged(void *pContext, BmVmInstance *inst, uint16_t field)
{
  BmGame *g = pContext;
  BmActors *a = &g->actors;
  uint8_t flags = 0;
  uint32_t key;
  if (a->current == BM_ACTORS_ROOM)
  {
    // The room's attachments keep `save` variables by room, slot and field.
    int8_t c = g->attachments.cls[a->currentSlot];
    if (c < 0) return;
    bmVmClassField(&a->modules[a->classModule[(uint8_t)c]], inst->classIndex, (uint8_t)field, NULL, NULL, &flags, NULL);
    if (!(flags & BM_SCRIPT_FIELD_SAVE)) return;
    key = roomSlotKey(g, a->currentSlot, field);
  }
  else
  {
    if (a->current >= BM_ACTORS_MAX) return;   // the player's attachments and screens keep no `save` variables yet
    if (a->spawned[a->current]) return;        // an object made at run time is gone with the room
    bmVmClassField(moduleOf(a, a->current), inst->classIndex, (uint8_t)field, NULL, NULL, &flags, NULL);
    if (!(flags & BM_SCRIPT_FIELD_SAVE)) return;
    key = storeKey(g, a->current, field);
  }
  for (uint8_t k = 0; k < a->storeCount; k++)
  {
    if (a->storeKey[k] == key)
    {
      a->storeValue[k] = inst->fields[field];
      return;
    }
  }
  if (a->storeCount < BM_ACTORS_STORE)
  {
    a->storeKey[a->storeCount] = key;
    a->storeValue[a->storeCount++] = inst->fields[field];
  }
  else
  {
    bm_script_errors++;  // more saved values than this version keeps
  }
}

static bool hostWaitOver(void *pContext, BmVmInstance *inst)
{
  (void)inst;
  BmGame *g = pContext;
  BmActors *a = &g->actors;
  uint8_t i = a->current;
  if (i >= BM_ACTORS_MAX || a->sheet[i] < 0) return true;
  // A looping animation never finishes: do not wait for it.
  const BmSpriteSheet *s = &g->sheets[a->sheet[i]];
  if (a->anim[i] >= s->animCount || (s->pAnims[a->anim[i] * 8 + 6] & 1)) return true;
  return a->animFinished[i] != 0;
}

static void hostError(void *pContext, BmVmInstance *inst, const char *pWhat, uint16_t function, uint16_t line)
{
  (void)inst;
  (void)function;
  BmGame *g = pContext;
  bm_script_errors++;
#ifdef BM_PLATFORM_HOST
  const char *who = g->actors.current == BM_ACTORS_SCREEN ? "a screen"
                  : g->actors.current == BM_ACTORS_ROOM  ? "a room attachment"
                  : g->actors.current >= BM_ACTORS_MAX   ? "an attachment"
                                                         : objectOf(g, g->actors.current)->className;
  printf("script error: %s, line %u: %s\n", who, (unsigned)line, pWhat);
#else
  (void)g;
  (void)pWhat;
  (void)line;
#endif
}

static BmVmHost hostOf(BmGame *g)
{
  return (BmVmHost){ g, hostCall, hostGetVar, hostSetVar, hostFieldChanged, hostWaitOver, hostError };
}

// --- Rooms ----------------------------------------------------------------------------------

static int32_t classNamed(const BmGame *g, const char *name)
{
  const BmActors *a = &g->actors;
  for (uint16_t c = 0; c < a->classCount; c++)
  {
    const BmVmModule *m = &a->modules[a->classModule[c]];
    if (bmVmClassKind(m, a->classInModule[c]) != BM_SCRIPT_KIND_ACTOR) continue;
    const char *n = bmPackString(g->pPack, bmVmClassName(m, a->classInModule[c]));
    if (n != NULL && strcmp(n, name) == 0) return c;
  }
  return -1;
}

static int8_t sheetNamed(const BmGame *g, const char *name)
{
  for (uint8_t k = 0; k < g->sheetCount; k++)
  {
    const char *n = bmPackString(g->pPack, g->sheets[k].nameId);
    if (n == NULL || name == NULL) continue;
    const char *x = n, *y = name;
    for (; *x && *y; x++, y++)
    {
      char p = *x, q = *y;
      if (p >= 'A' && p <= 'Z') p = (char)(p + 32);
      if (q >= 'A' && q <= 'Z') q = (char)(q + 32);
      if (p != q) break;
    }
    if (*x == 0 && *y == 0) return (int8_t)k;
  }
  return -1;
}

// A decimal number ("3", "-1.5") as int or fix.
static int32_t parseNumber(const char *s, bool fix)
{
  bool negative = *s == '-';
  if (negative) s++;
  int32_t whole = 0, frac = 0, scale = 1;
  for (; *s >= '0' && *s <= '9'; s++) whole = whole * 10 + (*s - '0');
  if (*s == '.')
  {
    for (s++; *s >= '0' && *s <= '9' && scale < 10000; s++)
    {
      frac = frac * 10 + (*s - '0');
      scale *= 10;
    }
  }
  int32_t v = fix ? whole * 256 + (frac * 256 + scale / 2) / scale : whole;
  return negative ? -v : v;
}

// Object properties named like a var(...) of the class set its value.
static void applyProperties(BmGame *g, uint8_t i, BmVmInstance *inst)
{
  const BmActors *a = &g->actors;
  const BmObjectView *o = objectOf(g, i);
  const BmVmModule *m = moduleOf(a, i);
  for (uint8_t f = 0; f < bmVmClassFieldCount(m, inst->classIndex); f++)
  {
    uint16_t nameId;
    uint8_t type, flags;
    bmVmClassField(m, inst->classIndex, f, &nameId, &type, &flags, NULL);
    if (!(flags & BM_SCRIPT_FIELD_EDITABLE)) continue;
    const char *name = bmPackString(g->pPack, nameId);
    for (uint16_t p = 0; name != NULL && p < o->propertyCount; p++)
    {
      const char *key = bmPackString(g->pPack, bmRead16(o->pProperties + p * 4));
      if (key == NULL || strcmp(key, name) != 0) continue;
      uint16_t valueId = bmRead16(o->pProperties + p * 4 + 2);
      const char *value = bmPackString(g->pPack, valueId);
      if (value == NULL) break;
      if (type == BM_SCRIPT_NAME || type == BM_SCRIPT_TEXT) inst->fields[f] = valueId;
      else if (type == BM_SCRIPT_RECORD) inst->fields[f] = (int32_t)hashName(value);   // the record's key
      else if (type == BM_SCRIPT_BOOL) inst->fields[f] = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
      else inst->fields[f] = parseNumber(value, type == BM_SCRIPT_FIX);
      break;
    }
  }
}

static void findButtonNames(BmGame *g);

// The pack's native code for the scripts, if it has some for this machine: the functions of
// every module it has code for then run as native code instead of being interpreted (native.h).
static void loadNative(BmGame *g)
{
  BmActors *a = &g->actors;
  for (uint8_t m = 0; m < a->moduleCount; m++) bmVmModuleSetNative(&a->modules[m], NULL);
  int32_t chunk = bmPackFindChunk(g->pPack, BM_CHUNK_MODN, 0);
  if (chunk < 0) return;
  uint32_t size = 0, slotSize = 0;
  const uint8_t *p = bmPackChunkData(g->pPack, (uint32_t)chunk, &size);
  void *pSlot = bmVmNativeSlot(&slotSize);
  const char *why = NULL;
  bmHalDebugClearAll();   // breakpoints in the code that is about to be replaced
  const BmVmNativeImage *pImage = bmVmNativeLoad(p, size, pSlot, slotSize, &why);
  if (pImage == NULL)
  {
#if BM_NATIVE_TARGET != BM_NATIVE_TARGET_NONE
    // On a machine that could run it, an image that does not load is a fault of the pack.
    bm_script_errors++;
#endif
    return;
  }
  // A module the image has no code for (a script pushed since) is interpreted.
  for (uint8_t m = 0; m < a->moduleCount; m++)
  {
    const BmVmNativeModule *pNative = bmVmNativeFind(pImage, a->verifiedHash[m]);
    if (pNative != NULL && !bmVmModuleSetNative(&a->modules[m], pNative)) bm_script_errors++;
  }
}

// Opens the pack's script modules (only those that changed since the last time are verified
// again) and lists their classes. Returns a hash over all of them.
static uint32_t loadModules(BmGame *g)
{
  BmActors *a = &g->actors;
  uint32_t hash = 0;
  bool changed = false;
  uint8_t n = 0;
  for (int32_t chunk = bmPackFindChunk(g->pPack, BM_CHUNK_MODB, 0); chunk >= 0 && n < BM_ACTORS_MODULES;
       chunk = bmPackFindChunk(g->pPack, BM_CHUNK_MODB, (uint32_t)chunk + 1))
  {
    BmChunkEntry entry;
    if (!bmPackGetChunk(g->pPack, (uint32_t)chunk, &entry) || entry.compression != BM_PACK_COMPRESSION_NONE) continue;
    uint32_t size = entry.size;
    const uint8_t *p = g->pPack->pData + entry.offset;   // as bmPackChunkData
    // The module that was verified when the pack was first used: no need to do it again.
    if (n < a->moduleCount && a->pVerified[n] == p && a->verifiedHash[n] == entry.hash)
    {
      hash = hash * 31u + entry.hash;
      n++;
      continue;
    }
    const char *why = NULL;
    changed = true;
    // Its code is verified when it first runs (vm.h): a pack's many scripts start quickly.
    if (!bmVmModuleOpenLazy(&a->modules[n], p, size, &why))
    {
      bm_script_errors++;
#ifdef BM_PLATFORM_HOST
      printf("script module refused: %s\n", why ? why : "unreadable");
#endif
      continue;
    }
    a->pVerified[n] = p;
    a->verifiedHash[n] = entry.hash;
    hash = hash * 31u + entry.hash;
    n++;
  }
  if (n != a->moduleCount) changed = true;
  a->moduleCount = n;
  if (changed) loadNative(g);
  a->classCount = 0;
  for (uint8_t m = 0; m < n; m++)
    for (uint16_t c = 0; c < a->modules[m].classCount && a->classCount < BM_ACTORS_CLASSES; c++)
    {
      a->classModule[a->classCount] = m;
      a->classInModule[a->classCount] = (uint8_t)c;
      a->classCount++;
    }
  a->hasModule = a->classCount != 0;
  if (changed) findButtonNames(g);
  bmAttachmentsBind(g);   // the player's attachments find their classes in the table
  bmScreensBind(g);
  return hash;
}

// FNV-1a over the class's variables: names, types, flags. Equal means the values still fit.
static uint32_t layoutHash(const BmGame *g, const BmVmModule *m, uint16_t cls)
{
  uint32_t h = 2166136261u;
  for (uint8_t f = 0; f < bmVmClassFieldCount(m, cls); f++)
  {
    uint16_t nameId;
    uint8_t type, flags;
    bmVmClassField(m, cls, f, &nameId, &type, &flags, NULL);
    h = (h ^ hashName(bmPackString(g->pPack, nameId))) * 16777619u;
    h = (h ^ type) * 16777619u;
    h = (h ^ flags) * 16777619u;
  }
  return h;
}

static uint32_t stateHash(const BmGame *g, const BmVmModule *m, uint16_t cls, uint8_t state)
{
  return state == 0xFF || state >= bmVmClassStateCount(m, cls) ? 0 : hashName(bmPackString(g->pPack, bmVmStateName(m, cls, state)));
}

void bmActorsPrepareReload(BmGame *g)
{
  BmActors *a = &g->actors;
  a->keep = a->hasModule;
  a->keepCount = a->count;
  for (uint8_t i = 0; a->keep && i < a->count; i++)
  {
    uint16_t cls = a->classInModule[a->classIndex[i]];
    const BmVmModule *m = moduleOf(a, i);
    a->keepObject[i] = objectOf(g, i)->persistentId;
    a->keepClass[i] = hashName(bmPackString(g->pPack, bmVmClassName(m, cls)));
    a->keepModule[i] = a->verifiedHash[a->classModule[a->classIndex[i]]];
    a->keepLayout[i] = layoutHash(g, m, cls);
    a->keepState[i] = stateHash(g, m, cls, a->state[i]);
  }
}

// After a module reload: what of object i (already of its new class) can be kept.
// Returns false if it has to start anew.
static bool keepObject(BmGame *g, uint8_t i)
{
  BmActors *a = &g->actors;
  if (i >= a->keepCount) return false;
  uint16_t cls = a->classInModule[a->classIndex[i]];
  const BmVmModule *m = moduleOf(a, i);
  if (objectOf(g, i)->persistentId != a->keepObject[i] || hashName(bmPackString(g->pPack, bmVmClassName(m, cls))) != a->keepClass[i])
    return false;
  if (a->verifiedHash[a->classModule[a->classIndex[i]]] == a->keepModule[i]) return true;   // the same script
  if (layoutHash(g, m, cls) != a->keepLayout[i]) return false;
  // The variables fit; the code changed: the same state (by name) from its start, or the start
  // state if there is no such state any more.
  BmVmInstance fresh;
  bmVmInstanceInit(m, &fresh, cls);
  uint8_t state = fresh.state;
  for (uint8_t s = 0; s < bmVmClassStateCount(m, cls); s++)
    if (a->keepState[i] != 0 && stateHash(g, m, cls, s) == a->keepState[i]) state = s;
  a->state[i] = state;
  a->wait[i] = BM_VM_RUNNING;
  a->pc[i] = 0;
  a->timer[i] = 0;
  return true;
}

bool bmActorsVerifySome(BmGame *g, uint32_t words)
{
  BmActors *a = &g->actors;
  bool more = false;
  for (uint8_t m = 0; m < a->moduleCount && words > 0; m++)
  {
    if (a->modules[m].verified != 0) continue;
    if (!bmVmModuleVerifySome(&a->modules[m], &words, NULL)) bm_script_errors++;
    more |= a->modules[m].verified == 0;
  }
  return more;
}

// The code of what runs now is verified while the room loads (a load may take its time; a
// first use in the middle of play would cost a frame): the room's objects, the player's and
// the room's attachments, open screens.
void bmActorsVerifyInUse(BmGame *g)
{
  BmActors *a = &g->actors;
  for (uint8_t i = 0; i < a->count; i++)
    if (a->classIndex[i] < a->classCount && !bmVmModuleVerify(&a->modules[a->classModule[a->classIndex[i]]], NULL)) bm_script_errors++;
  for (uint8_t s = 0; s < BM_ATTACH_SLOTS; s++)
  {
    int8_t c = g->attachments.cls[s];
    if (g->attachments.classKey[s] != 0 && c >= 0 && !bmVmModuleVerify(&a->modules[a->classModule[(uint8_t)c]], NULL)) bm_script_errors++;
  }
}

void bmActorsEnterRoom(BmGame *g, bool fresh)
{
  BmActors *a = &g->actors;
  a->pendingDoor = 0;
  uint32_t hash = loadModules(g);
  // State made with other scripts does not fit these: start the objects anew - unless single
  // modules are being reloaded (bmActorsPrepareReload): then each object is looked at.
  bool perObject = a->keep && !fresh && hash != a->moduleHash;
  a->keep = false;
  if (hash != a->moduleHash && !perObject) fresh = true;
  a->moduleHash = hash;
  if (!a->hasModule)
  {
    a->count = 0;
    return;
  }

  // The room's objects whose class is scripted, in the room's order.
  uint8_t n = 0;
  for (uint16_t o = 0; o < g->objectCount && n < BM_ACTORS_MAX; o++)
  {
    int32_t cls = classNamed(g, g->objects[o].className);
    if (cls < 0) continue;
    const BmObjectView *v = &g->objects[o];
    a->object[n] = (uint8_t)o;
    // The anchor: a point as it is, a rectangle at its bottom centre.
    a->homeX[n] = BM_FIX(v->x + v->w / 2);
    a->homeY[n] = BM_FIX(v->y + v->h);
    uint16_t spriteId = bmVmClassSprite(&a->modules[a->classModule[cls]], a->classInModule[cls]);
    a->sheet[n] = sheetNamed(g, spriteId != BM_VM_NONE ? bmPackString(g->pPack, spriteId) : v->className);
    if (perObject) a->classIndex[n] = (uint16_t)cls;   // the class table was made anew
    if (!fresh && !perObject && (n >= a->count || a->classIndex[n] != (uint16_t)cls)) fresh = true;  // not the room the state was made in
    if (fresh) a->classIndex[n] = (uint16_t)cls;
    n++;
  }
  a->roomCount = n;
  // Objects made at run time come after the room's: a state loaded in the room brings them
  // back (their classes by the state); a reload of single modules or a fresh start drops them.
  uint8_t total = n;
  while (!fresh && !perObject && total < a->count && a->spawned[total] && a->classIndex[total] < a->classCount)
  {
    a->object[total] = 0xFF;
    a->homeX[total] = a->x[total];
    a->homeY[total] = a->y[total];
    uint16_t cls = a->classIndex[total];
    uint16_t spriteId = bmVmClassSprite(&a->modules[a->classModule[cls]], a->classInModule[cls]);
    a->sheet[total] = sheetNamed(g, spriteId != BM_VM_NONE ? bmPackString(g->pPack, spriteId)
                                                          : bmPackString(g->pPack, bmVmClassName(&a->modules[a->classModule[cls]], a->classInModule[cls])));
    total++;
  }
  if (!fresh && !perObject && total != a->count) fresh = true;
  if (fresh || perObject)
  {
    a->count = n;
    for (uint8_t i = 0; i < BM_ACTORS_MAX; i++) a->spawned[i] = a->gone[i] = 0;
  }
  else a->count = total;
  if (!fresh && !perObject) return;

  BmVmHost host = hostOf(g);
  for (uint8_t i = 0; i < n; i++)
  {
    if (!fresh && keepObject(g, i)) continue;
    BmVmInstance inst;
    bmVmInstanceInit(moduleOf(a, i), &inst, a->classInModule[a->classIndex[i]]);
    a->x[i] = a->homeX[i];
    a->y[i] = a->homeY[i];
    a->facing[i] = 1;
    a->hidden[i] = 0;
    a->touching[i] = 0;
    a->health[i] = 0;
  a->affinity[i] = 0;
  a->knock[i] = 0;
  a->steady[i] = 0;
    a->affinity[i] = 0;
  a->knock[i] = 0;
  a->steady[i] = 0;
    a->knock[i] = 0;
    a->steady[i] = 0;
    a->team[i] = 0;
    a->invulnerable[i] = 0;
    // Its palette: the room object's property `palette` (a palette of its sprite), else its own.
    int32_t palette = a->sheet[i] >= 0 ? bmSpriteSheetPalette(&g->sheets[a->sheet[i]], g->pPack, bmGameObjectProperty(g, objectOf(g, i), "palette")) : -1;
    a->palette[i] = (uint8_t)(palette < 0 ? 0 : palette);
    a->flash[i] = 0;
    resetMotion(a, i);
    BmAnimState none = { 0, 0, 0, 0 };
    setAnim(a, i, &none);
    applyProperties(g, i, &inst);
    for (uint8_t k = 0; k < a->storeCount; k++)
    {
      uint8_t field = (uint8_t)a->storeKey[k];
      if ((a->storeKey[k] & ~0xFFu) == (storeKey(g, i, 0) & ~0xFFu) && field < BM_SCRIPT_MAX_FIELDS) inst.fields[field] = a->storeValue[k];
    }
    a->current = i;
    bmVmEvent(moduleOf(a, i), &host, &inst, BM_SCRIPT_EVENT_spawn);
    storeInstance(a, i, &inst);
  }
}

void bmActorsFrame(BmGame *g)
{
  BmActors *a = &g->actors;
  if (!a->hasModule) return;
  BmVmHost host = hostOf(g);
  uint32_t started = bmHalCycles();
  for (uint8_t i = 0; i < a->count; i++)
  {
    if (a->gone[i]) continue;
    BmVmInstance inst;
    loadInstance(a, i, &inst);
    a->current = i;
    const BmVmModule *m = moduleOf(a, i);
    bmVmEvent(m, &host, &inst, BM_SCRIPT_EVENT_tick);
    bool touching = touchesPlayer(g, i);
    if (touching && !a->touching[i]) bmVmEvent(m, &host, &inst, BM_SCRIPT_EVENT_touch);
    a->touching[i] = touching;
    bmVmStep(m, &host, &inst);
    storeInstance(a, i, &inst);
  }
  bm_script_cycles += bmHalCycles() - started;
  for (uint8_t i = 0; i < a->count; i++)
  {
    if (a->sheet[i] >= 0 && !a->gone[i])
    {
      BmAnimState st = animOf(a, i);
      bmAnimTick(&g->sheets[a->sheet[i]], &st);
      setAnim(a, i, &st);
    }
  }
}

uint8_t bmActorsDraw(BmGame *g, uint8_t firstOam, uint8_t maxOam)
{
  BmActors *a = &g->actors;
  uint8_t used = 0;
  for (uint8_t i = 0; a->hasModule && i < a->count && used < maxOam; i++)
  {
    if (a->hidden[i]) continue;
    if (a->sheet[i] < 0)
    {
      // No sprite: the placeholder frame, for objects placed as points.
      const BmObjectView *o = objectOf(g, i);
      int32_t sx = BM_FIX_INT(a->x[i]) - 8 - g->room.cameraX, sy = BM_FIX_INT(a->y[i]) - 16 - g->room.cameraY;
      if (o->w != 0 || o->h != 0 || sx < -16 || sx > 240 || sy < -16 || sy > 160) continue;
      BmSprite b = { .x = (int16_t)sx, .y = (int16_t)sy, .shape = BM_SPRITE_SQUARE, .size = 1, .tile = (uint16_t)(g->builtinTile + 8),
                     .paletteBank = 15, .priority = 1, .visible = true };
      bmSpriteSet((uint8_t)(firstOam + used), &b);
      used++;
      continue;
    }
    // Its palette's bank (given out the first time in the room); one that does not fit: not drawn.
    int8_t bank = bmGameSheetBank(g, a->sheet[i], a->palette[i]);
    if (bank < 0) continue;
    const BmSpriteSheet *s = &g->sheets[a->sheet[i]];
    BmAnimState st = animOf(a, i);
    // Just struck, or flash(frames): drawn in the flash bank (hits.h).
    bool flash = a->invulnerable[i] > BM_HITS_INVULNERABLE - BM_HITS_FLASH_FRAMES || a->flash[i] != 0;
    used = (uint8_t)(used + bmSpriteSheetDrawBank(s, bmAnimFrame(s, &st), BM_FIX_INT(a->x[i]) - g->room.cameraX, BM_FIX_INT(a->y[i]) - g->room.cameraY,
                                                  a->facing[i] < 0, 1, flash ? BM_HITS_FLASH_BANK : (uint8_t)bank, (uint8_t)(firstOam + used),
                                                  (uint8_t)(maxOam - used)));
  }
  return used;
}

static void resetMotion(BmActors *a, uint8_t i)
{
  a->vx[i] = a->vy[i] = a->fall[i] = 0;
  a->life[i] = 0;
  a->madeBy[i] = BM_HITS_FREE;
  a->madeBySlot[i] = BM_HITS_NO_SLOT;
}

int32_t bmActorsStoreGet(const BmGame *g, uint32_t key, bool *pFound)
{
  const BmActors *a = &g->actors;
  for (uint8_t k = 0; k < a->storeCount; k++)
    if (a->storeKey[k] == key)
    {
      if (pFound) *pFound = true;
      return a->storeValue[k];
    }
  if (pFound) *pFound = false;
  return 0;
}

bool bmActorsStorePut(BmGame *g, uint32_t key, int32_t value)
{
  BmActors *a = &g->actors;
  for (uint8_t k = 0; k < a->storeCount; k++)
    if (a->storeKey[k] == key)
    {
      a->storeValue[k] = value;
      return true;
    }
  if (a->storeCount >= BM_ACTORS_STORE) return false;
  a->storeKey[a->storeCount] = key;
  a->storeValue[a->storeCount++] = value;
  return true;
}

static bool spawnNamed(BmGame *g, const char *name, int32_t x, int32_t y, uint32_t record);

bool bmActorsSpawn(BmGame *g, uint16_t className, int32_t x, int32_t y, uint32_t record)
{
  return spawnNamed(g, bmPackString(g->pPack, className), x, y, record);
}

bool bmActorsSpawnNamed(BmGame *g, const char *name, int32_t x, int32_t y, uint32_t record)
{
  // Made by the room (the engine): on no side, no maker to tell.
  uint8_t current = g->actors.current;
  g->actors.current = BM_ACTORS_ROOM;
  bool made = spawnNamed(g, name, x, y, record);
  g->actors.current = current;
  return made;
}

static bool spawnNamed(BmGame *g, const char *name, int32_t x, int32_t y, uint32_t record)
{
  BmActors *a = &g->actors;
  // Who makes it: an object, the player (one of its attachments), or the room.
  uint8_t maker = a->current < a->count ? (uint8_t)(1 + a->current) : a->current == BM_ACTORS_OWNER ? BM_HITS_PLAYER : BM_HITS_FREE;
  uint8_t makerSlot = a->current == BM_ACTORS_OWNER ? a->currentSlot : BM_HITS_NO_SLOT;
  uint8_t makerTeam = maker == BM_HITS_PLAYER ? BM_HITS_PLAYER_TEAM : maker != BM_HITS_FREE ? a->team[maker - 1] : 0;
  int32_t cls = name != NULL ? classNamed(g, name) : -1;
  if (!a->hasModule || cls < 0) return false;
  // A place after the room's objects: one destroyed (not the one running now: it is stored
  // again when its event ends), else a new one at the end.
  uint8_t i = BM_ACTORS_MAX;
  for (uint8_t k = a->roomCount; k < a->count && i == BM_ACTORS_MAX; k++)
    if (a->gone[k] && k != a->current) i = k;
  if (i == BM_ACTORS_MAX)
  {
    if (a->count >= BM_ACTORS_MAX) return false;
    i = a->count++;
  }
  a->classIndex[i] = (uint16_t)cls;
  a->object[i] = 0xFF;
  a->spawned[i] = 1;
  a->gone[i] = 0;
  a->homeX[i] = a->x[i] = x;
  a->homeY[i] = a->y[i] = y;
  a->facing[i] = 1;
  a->hidden[i] = a->touching[i] = 0;
  a->health[i] = 0;
  a->affinity[i] = 0;
  a->knock[i] = 0;
  a->steady[i] = 0;
  a->team[i] = makerTeam;   // its hits are on the side of who made it
  a->invulnerable[i] = 0;
  a->palette[i] = 0;
  a->flash[i] = 0;
  resetMotion(a, i);
  a->madeBy[i] = maker;
  a->madeBySlot[i] = makerSlot;
  BmAnimState none = { 0, 0, 0, 0 };
  setAnim(a, i, &none);
  const BmVmModule *m = moduleOf(a, i);
  uint16_t spriteId = bmVmClassSprite(m, a->classInModule[cls]);
  a->sheet[i] = sheetNamed(g, spriteId != BM_VM_NONE ? bmPackString(g->pPack, spriteId) : name);
  BmVmInstance inst;
  bmVmInstanceInit(m, &inst, a->classInModule[cls]);
  if (record != 0)
  {
    for (uint8_t f = 0; f < bmVmClassFieldCount(m, a->classInModule[cls]); f++)
    {
      uint8_t type;
      bmVmClassField(m, a->classInModule[cls], f, NULL, &type, NULL, NULL);
      if (type != BM_SCRIPT_RECORD) continue;
      inst.fields[f] = (int32_t)record;
      break;
    }
  }
  storeInstance(a, i, &inst);
  bmActorsEvent(g, i, BM_SCRIPT_EVENT_spawn, 0);
  return true;
}

int32_t bmActorsClassNamed(const BmGame *g, const char *name)
{
  return classNamed(g, name);
}

void bmActorsMove(BmGame *g)
{
  BmActors *a = &g->actors;
  const bm_fix most = BM_FIX(16);   // faster ones could not be tested along their way
  for (uint8_t i = 0; a->hasModule && i < a->count; i++)
  {
    if (a->gone[i]) continue;
    if (a->life[i] != 0 && --a->life[i] == 0)
    {
      if (a->spawned[i]) bmActorsDestroy(g, i);
      else a->hidden[i] = 1;
      continue;
    }
    if (a->vx[i] == 0 && a->vy[i] == 0 && a->fall[i] == 0) continue;
    a->vy[i] += a->fall[i];
    if (a->vx[i] > most) a->vx[i] = most;
    if (a->vx[i] < -most) a->vx[i] = -most;
    if (a->vy[i] > most) a->vy[i] = most;
    if (a->vy[i] < -most) a->vy[i] = -most;
    a->x[i] += a->vx[i];
    a->y[i] += a->vy[i];
    int32_t bx, by, bw, bh;
    bmActorsBox(g, i, &bx, &by, &bw, &bh);
    if (bmCollisionSolidSides(&g->collision, bx + bw / 2, by + bh / 2)) bmActorsEvent(g, i, BM_SCRIPT_EVENT_wall, 0);
  }
}

void bmActorsDestroy(BmGame *g, uint8_t i)
{
  BmActors *a = &g->actors;
  if (i >= a->count) return;
  a->gone[i] = 1;
  a->hidden[i] = 1;
  a->touching[i] = 0;
  a->health[i] = 0;
  a->affinity[i] = 0;
  a->knock[i] = 0;
  a->steady[i] = 0;
  for (uint8_t k = 0; k < BM_HITS_MAX; k++)
    if (g->hits.source[k] == 1 + i) bmHitsDisarm(g, (uint8_t)(1 + i), g->hits.name[k]);
}

void bmActorsEvent(BmGame *g, uint8_t i, BmScriptEvent event, int32_t arg)
{
  BmActors *a = &g->actors;
  if (!a->hasModule || i >= a->count || a->gone[i]) return;
  BmVmHost host = hostOf(g);
  BmVmInstance inst;
  loadInstance(a, i, &inst);
  uint8_t current = a->current, currentSlot = a->currentSlot;
  a->current = i;
  bmVmEventWith(moduleOf(a, i), &host, &inst, event, arg);
  storeInstance(a, i, &inst);
  a->current = current;
  a->currentSlot = currentSlot;
}

bool bmActorsOwnObject(const BmGame *g, uint16_t objectIndex)
{
  const BmActors *a = &g->actors;
  for (uint8_t i = 0; a->hasModule && i < a->count; i++)
  {
    if (a->object[i] == objectIndex) return true;
  }
  return false;
}

// --- Attachments (attachments.h) ------------------------------------------------------------

static int8_t attachmentClass(const BmGame *g, uint32_t key)
{
  const BmActors *a = &g->actors;
  for (uint16_t c = 0; c < a->classCount && key != 0; c++)
  {
    const BmVmModule *m = &a->modules[a->classModule[c]];
    if (bmVmClassKind(m, a->classInModule[c]) != BM_SCRIPT_KIND_ATTACHMENT) continue;
    if (hashName(bmPackString(g->pPack, bmVmClassName(m, a->classInModule[c]))) == key) return (int8_t)c;
  }
  return -1;
}

static uint16_t findString(const BmPack *pPack, const char *text)
{
  for (uint32_t id = 0; id < 0xFFFF; id++)
  {
    const char *s = bmPackString(pPack, (uint16_t)id);
    if (s == NULL) break;
    if (strcmp(s, text) == 0) return (uint16_t)id;
  }
  return BM_VM_NONE;
}

static void loadSlot(const BmAttachments *t, uint8_t slot, uint16_t classInModule, BmVmInstance *inst)
{
  inst->classIndex = classInModule;
  inst->state = t->state[slot];
  inst->wait = t->wait[slot];
  inst->pc = t->pc[slot];
  inst->timer = t->timer[slot];
  memcpy(inst->fields, &t->fields[slot * BM_SCRIPT_MAX_FIELDS], sizeof(inst->fields));
}

static void storeSlot(BmAttachments *t, uint8_t slot, const BmVmInstance *inst)
{
  t->state[slot] = inst->state;
  t->wait[slot] = inst->wait;
  t->pc[slot] = inst->pc;
  t->timer[slot] = inst->timer;
  memcpy(&t->fields[slot * BM_SCRIPT_MAX_FIELDS], inst->fields, sizeof(inst->fields));
}

static uint8_t ownerOf(uint8_t slot)
{
  return slot < BM_ATTACH_ROOM_FIRST ? BM_ACTORS_OWNER : BM_ACTORS_ROOM;
}

// Runs an event of the attachment in `slot`, if there is one with a class.
static void slotEvent(BmGame *g, uint8_t slot, BmScriptEvent event, int32_t arg)
{
  BmActors *a = &g->actors;
  BmAttachments *t = &g->attachments;
  if (t->classKey[slot] == 0 || t->cls[slot] < 0) return;
  uint8_t c = (uint8_t)t->cls[slot];
  const BmVmModule *m = &a->modules[a->classModule[c]];
  BmVmHost host = hostOf(g);
  BmVmInstance inst;
  loadSlot(t, slot, a->classInModule[c], &inst);
  uint8_t current = a->current, currentSlot = a->currentSlot;
  a->current = ownerOf(slot);
  a->currentSlot = slot;
  bmVmEventWith(m, &host, &inst, event, arg);
  a->current = current;
  a->currentSlot = currentSlot;
  // The event may have detached or replaced this very attachment.
  if (t->classKey[slot] != 0 && t->cls[slot] == (int8_t)c) storeSlot(t, slot, &inst);
}

// tick(), state code and the lifetime of the attachment in `slot`.
static void slotFrame(BmGame *g, uint8_t slot)
{
  BmActors *a = &g->actors;
  BmAttachments *t = &g->attachments;
  slotEvent(g, slot, BM_SCRIPT_EVENT_tick, 0);
  if (t->classKey[slot] == 0 || t->cls[slot] < 0) return;
  uint8_t c = (uint8_t)t->cls[slot];
  BmVmHost host = hostOf(g);
  BmVmInstance inst;
  loadSlot(t, slot, a->classInModule[c], &inst);
  a->current = ownerOf(slot);
  a->currentSlot = slot;
  bmVmStep(&a->modules[a->classModule[c]], &host, &inst);
  if (t->classKey[slot] != 0 && t->cls[slot] == (int8_t)c) storeSlot(t, slot, &inst);
  // A lifetime ends after the frame's work.
  if (t->lifetime[slot] != 0 && --t->lifetime[slot] == 0)
  {
    t->lifetime[slot] = 1;   // detached() still sees it attached
    bmAttachmentsDetach(g, slot);
  }
}

void bmAttachmentsEvent(BmGame *g, uint8_t slot, BmScriptEvent event, int32_t arg)
{
  if (slot < BM_ATTACH_SLOTS) slotEvent(g, slot, event, arg);
}

int32_t bmAttachmentsFilter(BmGame *g, uint8_t event, int32_t amount, uint8_t element)
{
  BmAttachments *t = &g->attachments;
  if ((t->occupied & ((1u << BM_ATTACH_PLAYER_SLOTS) - 1)) == 0) return amount;
  int32_t outer = g->filterAmount;
  uint8_t outerElement = g->filterElement;
  g->filterAmount = amount;
  g->filterElement = element;
  for (uint8_t s = 0; s < BM_ATTACH_PLAYER_SLOTS; s++) slotEvent(g, s, event, g->filterAmount);
  amount = g->filterAmount;
  g->filterAmount = outer;
  g->filterElement = outerElement;
  return amount;
}

void bmAttachmentsEach(BmGame *g, uint8_t event, int32_t arg)
{
  BmAttachments *t = &g->attachments;
  if ((t->occupied & ((1u << BM_ATTACH_PLAYER_SLOTS) - 1)) == 0) return;
  for (uint8_t s = 0; s < BM_ATTACH_PLAYER_SLOTS; s++) slotEvent(g, s, (BmScriptEvent)event, arg);
}

void bmAttachmentsPlayerHit(BmGame *g, int32_t amount)
{
  BmAttachments *t = &g->attachments;
  if ((t->occupied & ((1u << BM_ATTACH_PLAYER_SLOTS) - 1)) == 0) return;
  for (uint8_t s = 0; s < BM_ATTACH_PLAYER_SLOTS; s++) slotEvent(g, s, BM_SCRIPT_EVENT_hit, amount);
}

void bmAttachmentsBind(BmGame *g)
{
  BmAttachments *t = &g->attachments;
  t->occupied = 0;
  for (uint8_t s = 0; s < BM_ATTACH_SLOTS; s++)
  {
    t->cls[s] = t->classKey[s] != 0 ? attachmentClass(g, t->classKey[s]) : -1;
    if (t->classKey[s] != 0) t->occupied |= (uint16_t)(1u << s);
  }
}

// The string ids of the game buttons' names, when the scripts changed (they may name others).
static void findButtonNames(BmGame *g)
{
  BmAttachments *t = &g->attachments;
  static const char *const names[] = {
#define X(name, key) #name,
    BM_ATTACH_BUTTONS(X)
#undef X
  };
  for (uint8_t b = 0; b < sizeof(names) / sizeof(names[0]); b++) t->buttonName[b] = findString(g->pPack, names[b]);
  // And the names a screen's buttons have (screens.h).
  static const char *const screenNames[10] = { "Up", "Down", "Left", "Right", "A", "B", "Start", "Select", "L", "R" };
  for (uint8_t b = 0; b < 10; b++) g->screens.buttonName[b] = findString(g->pPack, screenNames[b]);
}

// The room's own object (class "@room", written by the cooker from the map's properties).
static const BmObjectView *roomObject(const BmGame *g)
{
  for (uint16_t o = 0; o < g->objectCount; o++)
    if (strcmp(g->objects[o].className, "@room") == 0) return &g->objects[o];
  return NULL;
}

// A room attachment's variables: the room's `Class.variable` properties for its var(Category)
// variables, then what its `save` variables kept.
static void applyRoomValues(BmGame *g, uint8_t slot, uint8_t c, BmVmInstance *inst)
{
  BmActors *a = &g->actors;
  const BmVmModule *m = &a->modules[a->classModule[c]];
  const char *cls = bmPackString(g->pPack, bmVmClassName(m, a->classInModule[c]));
  const BmObjectView *o = roomObject(g);
  size_t clsLen = cls != NULL ? strlen(cls) : 0;
  for (uint8_t f = 0; f < bmVmClassFieldCount(m, a->classInModule[c]); f++)
  {
    uint16_t nameId;
    uint8_t type, flags;
    bmVmClassField(m, a->classInModule[c], f, &nameId, &type, &flags, NULL);
    const char *name = bmPackString(g->pPack, nameId);
    for (uint16_t p = 0; o != NULL && cls != NULL && name != NULL && (flags & BM_SCRIPT_FIELD_EDITABLE) && p < o->propertyCount; p++)
    {
      const char *key = bmPackString(g->pPack, bmRead16(o->pProperties + p * 4));
      if (key == NULL || strncmp(key, cls, clsLen) != 0 || key[clsLen] != '.' || strcmp(key + clsLen + 1, name) != 0) continue;
      uint16_t valueId = bmRead16(o->pProperties + p * 4 + 2);
      const char *value = bmPackString(g->pPack, valueId);
      if (value == NULL) break;
      if (type == BM_SCRIPT_NAME || type == BM_SCRIPT_TEXT) inst->fields[f] = valueId;
      else if (type == BM_SCRIPT_RECORD) inst->fields[f] = (int32_t)hashName(value);
      else if (type == BM_SCRIPT_BOOL) inst->fields[f] = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
      else inst->fields[f] = parseNumber(value, type == BM_SCRIPT_FIX);
      break;
    }
  }
  for (uint8_t k = 0; k < a->storeCount; k++)
  {
    uint8_t field = (uint8_t)a->storeKey[k];
    if ((a->storeKey[k] & ~0xFFu) == (roomSlotKey(g, slot, 0) & ~0xFFu) && field < BM_SCRIPT_MAX_FIELDS) inst->fields[field] = a->storeValue[k];
  }
}

static bool attachKey(BmGame *g, uint8_t slot, uint32_t key, uint16_t lifetime, uint32_t record)
{
  BmActors *a = &g->actors;
  BmAttachments *t = &g->attachments;
  if (slot >= BM_ATTACH_SLOTS) return false;
  int8_t c = attachmentClass(g, key);
  if (c < 0) return false;
  bmAttachmentsDetach(g, slot);
  BmVmInstance inst;
  bmVmInstanceInit(&a->modules[a->classModule[(uint8_t)c]], &inst, a->classInModule[(uint8_t)c]);
  if (slot >= BM_ATTACH_ROOM_FIRST) applyRoomValues(g, slot, (uint8_t)c, &inst);
  if (record != 0)
  {
    // Equipped: its first record<S> variable is the record it came with.
    const BmVmModule *m = &a->modules[a->classModule[(uint8_t)c]];
    for (uint8_t f = 0; f < bmVmClassFieldCount(m, a->classInModule[(uint8_t)c]); f++)
    {
      uint8_t type;
      bmVmClassField(m, a->classInModule[(uint8_t)c], f, NULL, &type, NULL, NULL);
      if (type != BM_SCRIPT_RECORD) continue;
      inst.fields[f] = (int32_t)record;
      break;
    }
  }
  t->classKey[slot] = key;
  t->cls[slot] = c;
  t->occupied |= (uint16_t)(1u << slot);
  t->lifetime[slot] = lifetime;
  storeSlot(t, slot, &inst);
  slotEvent(g, slot, BM_SCRIPT_EVENT_attached, 0);
  return true;
}

bool bmAttachmentsAttachRecord(BmGame *g, uint8_t slot, uint16_t className, uint16_t lifetime, uint32_t record)
{
  const char *cls = className != BM_DATA_NONE_ID ? bmPackString(g->pPack, className) : NULL;
  if (cls == NULL) return false;
  BmActors *a = &g->actors;
  uint8_t current = a->current, currentSlot = a->currentSlot;
  bool done = attachKey(g, slot, hashName(cls), lifetime, record);
  a->current = current;
  a->currentSlot = currentSlot;
  return done;
}

bool bmAttachmentsAttach(BmGame *g, uint8_t slot, uint16_t className, uint16_t lifetime)
{
  return attachKey(g, slot, hashName(bmPackString(g->pPack, className)), lifetime, 0);
}

bool bmAttachmentsEquip(BmGame *g, uint8_t slot, uint32_t key)
{
  if (slot >= BM_INVENTORY_EQUIP || slot >= BM_ATTACH_PLAYER_SLOTS) return false;
  if (key != 0 && bmInventoryCount(&g->inventory, key) == 0) return false;
  BmDataTable table;
  const uint8_t *record;
  bool found = key != 0 && bmDataFindRecord(g->pPack, key, &table, &record);
  // A structure with roles says where its records go: only equipment, and only into its slot
  // (data-format.md §6). Without roles anything goes anywhere.
  if (found && bmDataTableRoles(&table) != 0 && bmDataRecordSlot(&table, record) != slot) return false;
  g->inventory.equipped[slot] = key;
  bmGameEquipmentChanged(g);
  uint16_t behaviour = found ? bmDataRecordBehaviour(&table, record) : BM_DATA_NONE_ID;
  const char *cls = behaviour != BM_DATA_NONE_ID ? bmPackString(g->pPack, behaviour) : NULL;
  if (cls == NULL || !attachKey(g, slot, hashName(cls), 0, key)) bmAttachmentsDetach(g, slot);
  return true;
}

bool bmAttachmentsUse(BmGame *g, uint32_t key)
{
  if (key == 0 || bmInventoryCount(&g->inventory, key) == 0) return false;
  BmDataTable table;
  const uint8_t *record;
  uint16_t behaviour = bmDataFindRecord(g->pPack, key, &table, &record) ? bmDataRecordBehaviour(&table, record) : BM_DATA_NONE_ID;
  const char *cls = behaviour != BM_DATA_NONE_ID ? bmPackString(g->pPack, behaviour) : NULL;
  BmActors *a = &g->actors;
  uint8_t current = a->current, slot = a->currentSlot;
  bool used = cls != NULL && attachKey(g, BM_GAME_ITEM_SLOT, hashName(cls), 1, key);
  a->current = current;
  a->currentSlot = slot;
  return used;
}

void bmAttachmentsAbilities(BmGame *g)
{
  BmAttachments *t = &g->attachments;
  BmActors *a = &g->actors;
  uint32_t want[BM_ATTACH_ABILITY_SLOTS] = { 0 };
  uint16_t cls[BM_ATTACH_ABILITY_SLOTS] = { 0 };
  uint8_t n = 0;
  for (uint8_t i = 0; i < g->inventory.count && n < BM_ATTACH_ABILITY_SLOTS; i++)
  {
    BmDataTable table;
    const uint8_t *record;
    uint32_t key = g->inventory.key[i];
    if (!bmDataFindRecord(g->pPack, key, &table, &record) || !(bmDataTableRoles(&table) & BM_DATA_ROLE_ABILITY)) continue;
    uint16_t behaviour = bmDataRecordBehaviour(&table, record);
    if (behaviour == BM_DATA_NONE_ID) continue;
    want[n] = key;
    cls[n++] = behaviour;
  }
  uint8_t current = a->current, currentSlot = a->currentSlot;
  for (uint8_t k = 0; k < BM_ATTACH_ABILITY_SLOTS; k++)
  {
    uint8_t slot = (uint8_t)(BM_ATTACH_ABILITY_FIRST + k);
    if (t->abilityKey[k] == want[k] && (want[k] == 0 || t->classKey[slot] != 0)) continue;
    bmAttachmentsDetach(g, slot);
    t->abilityKey[k] = want[k];
    const char *name = want[k] != 0 ? bmPackString(g->pPack, cls[k]) : NULL;
    if (name != NULL && !attachKey(g, slot, hashName(name), 0, want[k])) t->abilityKey[k] = 0;
  }
  a->current = current;
  a->currentSlot = currentSlot;
}

void bmAttachmentsEquipAll(BmGame *g)
{
  for (uint8_t s = 0; s < BM_INVENTORY_EQUIP && s < BM_ATTACH_PLAYER_SLOTS; s++)
    if (g->inventory.equipped[s] != 0) bmAttachmentsEquip(g, s, g->inventory.equipped[s]);
}

void bmAttachmentsDetach(BmGame *g, uint8_t slot)
{
  BmAttachments *t = &g->attachments;
  if (slot >= BM_ATTACH_SLOTS || t->classKey[slot] == 0) return;
  uint32_t key = t->classKey[slot];
  slotEvent(g, slot, BM_SCRIPT_EVENT_detached, 0);
  if (t->classKey[slot] != key) return;   // detached() attached something else here
  // What its add_stat added goes with it.
  for (int k = 0; k < BM_ATTACH_STATS; k++)
  {
    uint8_t stat = t->statOf[slot * BM_ATTACH_STATS + k];
    if (stat != 0 && stat <= BM_INVENTORY_STATS)
    {
      int32_t v = g->inventory.statBonus[stat - 1] - t->statAdded[slot * BM_ATTACH_STATS + k];
      g->inventory.statBonus[stat - 1] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
    t->statOf[slot * BM_ATTACH_STATS + k] = 0;
    t->statAdded[slot * BM_ATTACH_STATS + k] = 0;
  }
  t->classKey[slot] = 0;
  t->cls[slot] = -1;
  t->occupied &= (uint16_t)~(1u << slot);
  t->state[slot] = t->wait[slot] = 0;
  t->pc[slot] = t->timer[slot] = t->lifetime[slot] = 0;
  memset(&t->fields[slot * BM_SCRIPT_MAX_FIELDS], 0, sizeof(int32_t) * BM_SCRIPT_MAX_FIELDS);
}

void bmAttachmentsFrame(BmGame *g, uint16_t keys, uint16_t previousKeys)
{
  BmAttachments *t = &g->attachments;
  if ((t->occupied & ((1u << BM_ATTACH_PLAYER_SLOTS) - 1)) == 0) return;
  static const uint16_t buttonKeys[] = {
#define X(name, key) key,
    BM_ATTACH_BUTTONS(X)
#undef X
  };
  uint16_t changed = (uint16_t)(keys ^ previousKeys);
  uint32_t started = bmHalCycles();
  for (uint8_t s = 0; s < BM_ATTACH_PLAYER_SLOTS; s++)
  {
    if (t->classKey[s] == 0 || t->cls[s] < 0) continue;
    for (uint8_t b = 0; b < sizeof(buttonKeys) / sizeof(buttonKeys[0]); b++)
      if ((changed & buttonKeys[b]) && t->buttonName[b] != BM_VM_NONE)
        slotEvent(g, s, (keys & buttonKeys[b]) ? BM_SCRIPT_EVENT_press : BM_SCRIPT_EVENT_release, t->buttonName[b]);
    slotFrame(g, s);
  }
  bm_script_cycles += bmHalCycles() - started;
}

void bmAttachmentsBodyEvents(BmGame *g, uint32_t bodyEvents)
{
  BmAttachments *t = &g->attachments;
  if ((t->occupied & ((1u << BM_ATTACH_PLAYER_SLOTS) - 1)) == 0 || (bodyEvents & (BM_BODY_JUMPED | BM_BODY_LANDED)) == 0) return;
  for (uint8_t s = 0; s < BM_ATTACH_PLAYER_SLOTS; s++)
  {
    if (t->classKey[s] == 0) continue;
    if (bodyEvents & BM_BODY_JUMPED) slotEvent(g, s, BM_SCRIPT_EVENT_jumped, 0);
    if (bodyEvents & BM_BODY_LANDED) slotEvent(g, s, BM_SCRIPT_EVENT_landed, 0);
  }
}

void bmAttachmentsRoomFrame(BmGame *g)
{
  BmAttachments *t = &g->attachments;
  if ((t->occupied >> BM_ATTACH_ROOM_FIRST) == 0) return;
  uint32_t started = bmHalCycles();
  for (uint8_t s = BM_ATTACH_ROOM_FIRST; s < BM_ATTACH_SLOTS; s++)
    if (t->classKey[s] != 0 && t->cls[s] >= 0) slotFrame(g, s);
  bm_script_cycles += bmHalCycles() - started;
}

void bmAttachmentsLeaveRoom(BmGame *g)
{
  for (uint8_t s = BM_ATTACH_ROOM_FIRST; s < BM_ATTACH_SLOTS; s++) bmAttachmentsDetach(g, s);
}

void bmAttachmentsEnterRoom(BmGame *g)
{
  const BmObjectView *o = roomObject(g);
  if (o == NULL) return;
  const char *list = bmGameObjectProperty(g, o, "attach");
  // "Wind, Rain": names separated by commas, in the room's slots in this order.
  uint8_t slot = BM_ATTACH_ROOM_FIRST;
  for (const char *p = list; p != NULL && *p != 0 && slot < BM_ATTACH_SLOTS;)
  {
    while (*p == ' ' || *p == ',') p++;
    const char *end = p;
    while (*end != 0 && *end != ',') end++;
    const char *last = end;
    while (last > p && last[-1] == ' ') last--;
    if (last > p)
    {
      uint32_t h = 2166136261u;
      for (const char *q = p; q < last; q++) h = (h ^ (uint8_t)*q) * 16777619u;
      if (attachKey(g, slot, h, 0, 0)) slot++;
      else bm_script_errors++;   // not an Attachment class of this pack
    }
    p = end;
  }
}

// --- Screens (screens.h) --------------------------------------------------------------------

static int8_t screenClass(const BmGame *g, uint32_t key)
{
  const BmActors *a = &g->actors;
  for (uint16_t c = 0; c < a->classCount && key != 0; c++)
  {
    const BmVmModule *m = &a->modules[a->classModule[c]];
    if (bmVmClassKind(m, a->classInModule[c]) != BM_SCRIPT_KIND_SCREEN) continue;
    if (hashName(bmPackString(g->pPack, bmVmClassName(m, a->classInModule[c]))) == key) return (int8_t)c;
  }
  return -1;
}

static void loadScreen(const BmScreens *t, uint8_t s, uint16_t classInModule, BmVmInstance *inst)
{
  inst->classIndex = classInModule;
  inst->state = t->state[s];
  inst->wait = t->wait[s];
  inst->pc = t->pc[s];
  inst->timer = t->timer[s];
  memcpy(inst->fields, &t->fields[s * BM_SCRIPT_MAX_FIELDS], sizeof(inst->fields));
}

static void storeScreen(BmScreens *t, uint8_t s, const BmVmInstance *inst)
{
  t->state[s] = inst->state;
  t->wait[s] = inst->wait;
  t->pc[s] = inst->pc;
  t->timer[s] = inst->timer;
  memcpy(&t->fields[s * BM_SCRIPT_MAX_FIELDS], inst->fields, sizeof(inst->fields));
}

// An event (or, with `step`, the state code) of the screen at place s, if it is still there.
static void screenRun(BmGame *g, uint8_t s, BmScriptEvent event, int32_t arg, bool step)
{
  BmActors *a = &g->actors;
  BmScreens *t = &g->screens;
  if (s >= t->count || t->cls[s] < 0) return;
  uint8_t c = (uint8_t)t->cls[s];
  uint32_t key = t->classKey[s];
  const BmVmModule *m = &a->modules[a->classModule[c]];
  BmVmHost host = hostOf(g);
  BmVmInstance inst;
  loadScreen(t, s, a->classInModule[c], &inst);
  uint8_t current = a->current, currentSlot = a->currentSlot;
  a->current = BM_ACTORS_SCREEN;
  a->currentSlot = s;
  if (step) bmVmStep(m, &host, &inst);
  else bmVmEventWith(m, &host, &inst, event, arg);
  a->current = current;
  a->currentSlot = currentSlot;
  // The screen may have closed itself (or opened another one in its place).
  if (s < t->count && t->classKey[s] == key && t->cls[s] == (int8_t)c) storeScreen(t, s, &inst);
}

// The room behind open screens is darkened; the UI layer (background 3) is not.
static void darkenRoom(bool on)
{
  if (on) bmBlendSet(BM_BLEND_DARKEN, BM_LAYER_BG(0) | BM_LAYER_BG(1) | BM_LAYER_BG(2) | BM_LAYER_OBJ | BM_LAYER_BACKDROP, 0, 0, 0, 9);
  else bmBlendSet(BM_BLEND_NONE, 0, 0, 0, 0, 0);
}

bool bmScreensOpen(BmGame *g, uint16_t className)
{
  BmActors *a = &g->actors;
  BmScreens *t = &g->screens;
  uint32_t key = hashName(bmPackString(g->pPack, className));
  int8_t c = screenClass(g, key);
  if (c < 0 || t->count >= BM_SCREENS_MAX) return false;
  uint8_t s = t->count++;
  BmVmInstance inst;
  bmVmInstanceInit(&a->modules[a->classModule[(uint8_t)c]], &inst, a->classInModule[(uint8_t)c]);
  t->classKey[s] = key;
  t->cls[s] = c;
  storeScreen(t, s, &inst);
  t->repeat = 0;
  if (s == 0) darkenRoom(true);
  bmUiShow(&g->ui, true);
  screenRun(g, s, BM_SCRIPT_EVENT_opened, 0, false);
  return true;
}

void bmScreensClose(BmGame *g)
{
  BmScreens *t = &g->screens;
  if (t->count == 0) return;
  uint8_t s = (uint8_t)(t->count - 1);
  uint32_t key = t->classKey[s];
  screenRun(g, s, BM_SCRIPT_EVENT_closed, 0, false);
  if (t->count == 0 || t->classKey[t->count - 1] != key) return;   // closed() closed it already
  t->count--;
  t->classKey[t->count] = 0;
  t->cls[t->count] = -1;
  if (t->count == 0)
  {
    bmUiClear(&g->ui);
    g->hudDirty = true;   // the HUD and a notice come back
    bmUiShow(&g->ui, g->dialogue != 0);
    darkenRoom(false);
    bmGameDrawDialogue(g);   // the dialogue box comes back, as far as its line had appeared
  }
  else bmScreensRedraw(g);   // the one below shows again
}

void bmScreensFrame(BmGame *g, uint16_t keys, uint16_t previousKeys)
{
  BmScreens *t = &g->screens;
  static const uint16_t keyOf[10] = { BM_KEY_UP, BM_KEY_DOWN, BM_KEY_LEFT, BM_KEY_RIGHT, BM_KEY_A, BM_KEY_B, BM_KEY_START, BM_KEY_SELECT, BM_KEY_L, BM_KEY_R };
  uint32_t started = bmHalCycles();
  // Buttons to the top screen; a direction held repeats after 15 frames, every 5.
  uint16_t pressed = (uint16_t)(keys & ~previousKeys), released = (uint16_t)(~keys & previousKeys);
  uint16_t directions = keys & (BM_KEY_UP | BM_KEY_DOWN | BM_KEY_LEFT | BM_KEY_RIGHT);
  if (directions != 0 && (pressed & directions) == 0)
  {
    t->repeat = (uint8_t)(t->repeat < 255 ? t->repeat + 1 : 255);
    if (t->repeat >= 15 && (t->repeat - 15) % 5 == 0) pressed |= directions;
  }
  else t->repeat = 0;
  for (uint8_t b = 0; b < 10 && t->count > 0; b++)
  {
    if (t->buttonName[b] == BM_VM_NONE) continue;
    if (pressed & keyOf[b]) screenRun(g, (uint8_t)(t->count - 1), BM_SCRIPT_EVENT_press, t->buttonName[b], false);
    else if (released & keyOf[b]) screenRun(g, (uint8_t)(t->count - 1), BM_SCRIPT_EVENT_release, t->buttonName[b], false);
  }
  for (uint8_t s = 0; s < t->count; s++)
  {
    screenRun(g, s, BM_SCRIPT_EVENT_tick, 0, false);
    screenRun(g, s, BM_SCRIPT_EVENT_tick, 0, true);
  }
  bm_script_cycles += bmHalCycles() - started;
}

void bmScreensRedraw(BmGame *g)
{
  BmScreens *t = &g->screens;
  if (t->count == 0) return;
  bmUiClear(&g->ui);
  bmUiShow(&g->ui, true);
  darkenRoom(true);
  for (uint8_t s = 0; s < t->count; s++) screenRun(g, s, BM_SCRIPT_EVENT_opened, 0, false);
}

void bmScreensBind(BmGame *g)
{
  BmScreens *t = &g->screens;
  for (uint8_t s = 0; s < BM_SCREENS_MAX; s++) t->cls[s] = s < t->count ? screenClass(g, t->classKey[s]) : -1;
}
