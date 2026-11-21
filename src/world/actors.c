/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "actors.h"

#include <string.h>

#include "game.h"
#include "hal.h"
#include "native.h"
#include "video.h"

#ifdef BM_PLATFORM_HOST
#  include <stdio.h>
#endif

uint32_t bm_script_errors;
uint32_t bm_script_cycles;

// --- Helpers --------------------------------------------------------------------------------

static void loadInstance(const BmActors *a, uint8_t i, BmVmInstance *inst)
{
  inst->classIndex = a->classIndex[i];
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
  const BmObjectView *o = &g->objects[a->object[i]];
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

static bool touchesPlayer(const BmGame *g, uint8_t i)
{
  int32_t x, y, w, h;
  boxOf(g, i, &x, &y, &w, &h);
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  int32_t hw = g->playerParams.halfWidth, ph = g->playerParams.height;
  return px - hw < x + w && x < px + hw && py - ph < y + h && y < py;
}

static uint32_t storeKey(const BmGame *g, uint8_t i, uint16_t field)
{
  return ((g->roomIndex & 0xFF) << 24) | ((uint32_t)g->objects[g->actors.object[i]].persistentId << 8) | (field & 0xFF);
}

// --- What scripts call ------------------------------------------------------------------------

static bool hostCall(void *pContext, BmVmInstance *inst, uint16_t api, const int32_t *args, int32_t *result)
{
  BmGame *g = pContext;
  BmActors *a = &g->actors;
  uint8_t i = a->current;
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
    bmGameHurtPlayer(g, args[0]);
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
    printf("script: %s logs %d\n", g->objects[a->object[i]].className, (int)args[0]);
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
    uint32_t h = hashName(bmPackString(g->pPack, (uint16_t)args[0]));
    bool has = false;
    for (uint8_t k = 0; k < a->itemCount; k++) has = has || a->items[k] == h;
    if (api == BM_SCRIPT_API_has_item) *result = has;
    else if (!has && a->itemCount < BM_ACTORS_ITEMS) a->items[a->itemCount++] = h;
    break;
  }
  case BM_SCRIPT_API_abs:
    *result = absolute(args[0]);
    break;
  case BM_SCRIPT_API_play_sound:
    bmSoundPlayEffect(&g->sound, bmPackString(g->pPack, (uint16_t)args[0]));
    break;
  case BM_SCRIPT_API_play_music:
    bmSoundPlayMusic(&g->sound, bmPackString(g->pPack, (uint16_t)args[0]));
    break;
  default:
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
  switch (var)
  {
  case BM_SCRIPT_VAR_x: return a->x[i];
  case BM_SCRIPT_VAR_y: return a->y[i];
  case BM_SCRIPT_VAR_home_x: return a->homeX[i];
  case BM_SCRIPT_VAR_home_y: return a->homeY[i];
  case BM_SCRIPT_VAR_facing: return a->facing[i];
  default: return 0;
  }
}

static void hostSetVar(void *pContext, BmVmInstance *inst, uint16_t var, int32_t value)
{
  (void)inst;
  BmGame *g = pContext;
  BmActors *a = &g->actors;
  uint8_t i = a->current;
  switch (var)
  {
  case BM_SCRIPT_VAR_x: a->x[i] = value; break;
  case BM_SCRIPT_VAR_y: a->y[i] = value; break;
  case BM_SCRIPT_VAR_facing: a->facing[i] = (int8_t)(value < 0 ? -1 : 1); break;
  default: break;
  }
}

// `save` variables are kept by room, object and field, so they survive leaving the room.
static void hostFieldChanged(void *pContext, BmVmInstance *inst, uint16_t field)
{
  BmGame *g = pContext;
  BmActors *a = &g->actors;
  uint8_t flags = 0;
  bmVmClassField(&a->module, inst->classIndex, (uint8_t)field, NULL, NULL, &flags, NULL);
  if (!(flags & BM_SCRIPT_FIELD_SAVE)) return;
  uint32_t key = storeKey(g, a->current, field);
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
  if (a->sheet[i] < 0) return true;
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
  printf("script error: %s, line %u: %s\n", g->objects[g->actors.object[g->actors.current]].className, (unsigned)line, pWhat);
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
  for (uint16_t c = 0; c < a->module.classCount; c++)
  {
    const char *n = bmPackString(g->pPack, bmVmClassName(&a->module, c));
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
  const BmObjectView *o = &g->objects[a->object[i]];
  for (uint8_t f = 0; f < bmVmClassFieldCount(&a->module, inst->classIndex); f++)
  {
    uint16_t nameId;
    uint8_t type, flags;
    bmVmClassField(&a->module, inst->classIndex, f, &nameId, &type, &flags, NULL);
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
      else if (type == BM_SCRIPT_BOOL) inst->fields[f] = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
      else inst->fields[f] = parseNumber(value, type == BM_SCRIPT_FIX);
      break;
    }
  }
}

// The pack's native code for the scripts, if it has some for this machine: the functions then
// run as native code instead of being interpreted (native.h).
static void loadNative(BmGame *g, BmVmModule *pModule, uint32_t bytecodeCrc)
{
  int32_t chunk = bmPackFindChunk(g->pPack, BM_CHUNK_MODN, 0);
  if (chunk < 0) return;
  uint32_t size = 0, slotSize = 0;
  const uint8_t *p = bmPackChunkData(g->pPack, (uint32_t)chunk, &size);
  void *pSlot = bmVmNativeSlot(&slotSize);
  const char *why = NULL;
  bmHalDebugClearAll();   // breakpoints in the code that is about to be replaced
  const BmVmNativeModule *pNative = bmVmNativeLoad(p, size, pSlot, slotSize, bytecodeCrc, &why);
  if (pNative == NULL || !bmVmModuleSetNative(pModule, pNative))
  {
#if BM_NATIVE_TARGET != BM_NATIVE_TARGET_NONE
    // On a machine that could run it, a module that does not load is a fault of the pack.
    bm_script_errors++;
#endif
  }
}

void bmActorsEnterRoom(BmGame *g, bool fresh)
{
  BmActors *a = &g->actors;
  a->pendingDoor = 0;
  uint32_t hash = 0;
  int32_t chunk = bmPackFindChunk(g->pPack, BM_CHUNK_MODB, 0);
  uint32_t size = 0;
  const uint8_t *p = chunk >= 0 ? bmPackChunkData(g->pPack, (uint32_t)chunk, &size) : NULL;
  BmChunkEntry entry;
  if (p != NULL && bmPackGetChunk(g->pPack, (uint32_t)chunk, &entry) && a->hasModule && a->pVerified == p && a->verifiedHash == entry.hash)
  {
    // The module that was verified when the pack was first used: no need to do it again.
    hash = entry.hash;
  }
  else if (chunk >= 0)
  {
    a->hasModule = false;
    const char *why = NULL;
    if (p != NULL && bmPackGetChunk(g->pPack, (uint32_t)chunk, &entry) && bmVmModuleOpen(&a->module, p, size, &why))
    {
      loadNative(g, &a->module, entry.hash);
      a->hasModule = true;
      a->pVerified = p;
      a->verifiedHash = entry.hash;
      hash = entry.hash;
    }
    else
    {
      bm_script_errors++;
#ifdef BM_PLATFORM_HOST
      printf("script module refused: %s\n", why ? why : "unreadable");
#endif
    }
  }
  if (chunk < 0) a->hasModule = false;
  // State made with other scripts does not fit these: start the objects anew.
  if (hash != a->moduleHash) fresh = true;
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
    uint16_t spriteId = bmVmClassSprite(&a->module, (uint16_t)cls);
    a->sheet[n] = sheetNamed(g, spriteId != BM_VM_NONE ? bmPackString(g->pPack, spriteId) : v->className);
    if (!fresh && (n >= a->count || a->classIndex[n] != (uint16_t)cls)) fresh = true;  // not the room the state was made in
    if (fresh) a->classIndex[n] = (uint16_t)cls;
    n++;
  }
  if (!fresh && n != a->count) fresh = true;
  a->count = n;
  if (!fresh) return;

  BmVmHost host = hostOf(g);
  for (uint8_t i = 0; i < n; i++)
  {
    BmVmInstance inst;
    bmVmInstanceInit(&a->module, &inst, a->classIndex[i]);
    a->x[i] = a->homeX[i];
    a->y[i] = a->homeY[i];
    a->facing[i] = 1;
    a->hidden[i] = 0;
    a->touching[i] = 0;
    BmAnimState none = { 0, 0, 0, 0 };
    setAnim(a, i, &none);
    applyProperties(g, i, &inst);
    for (uint8_t k = 0; k < a->storeCount; k++)
    {
      uint8_t field = (uint8_t)a->storeKey[k];
      if ((a->storeKey[k] & ~0xFFu) == (storeKey(g, i, 0) & ~0xFFu) && field < BM_SCRIPT_MAX_FIELDS) inst.fields[field] = a->storeValue[k];
    }
    a->current = i;
    bmVmEvent(&a->module, &host, &inst, BM_SCRIPT_EVENT_spawn);
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
    BmVmInstance inst;
    loadInstance(a, i, &inst);
    a->current = i;
    bmVmEvent(&a->module, &host, &inst, BM_SCRIPT_EVENT_tick);
    bool touching = touchesPlayer(g, i);
    if (touching && !a->touching[i]) bmVmEvent(&a->module, &host, &inst, BM_SCRIPT_EVENT_touch);
    a->touching[i] = touching;
    bmVmStep(&a->module, &host, &inst);
    storeInstance(a, i, &inst);
  }
  bm_script_cycles += bmHalCycles() - started;
  for (uint8_t i = 0; i < a->count; i++)
  {
    if (a->sheet[i] >= 0)
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
      const BmObjectView *o = &g->objects[a->object[i]];
      int32_t sx = BM_FIX_INT(a->x[i]) - 8 - g->room.cameraX, sy = BM_FIX_INT(a->y[i]) - 16 - g->room.cameraY;
      if (o->w != 0 || o->h != 0 || sx < -16 || sx > 240 || sy < -16 || sy > 160) continue;
      BmSprite b = { .x = (int16_t)sx, .y = (int16_t)sy, .shape = BM_SPRITE_SQUARE, .size = 1, .tile = (uint16_t)(g->builtinTile + 8),
                     .paletteBank = 15, .priority = 1, .visible = true };
      bmSpriteSet((uint8_t)(firstOam + used), &b);
      used++;
      continue;
    }
    const BmSpriteSheet *s = &g->sheets[a->sheet[i]];
    BmAnimState st = animOf(a, i);
    used = (uint8_t)(used + bmSpriteSheetDraw(s, bmAnimFrame(s, &st), BM_FIX_INT(a->x[i]) - g->room.cameraX, BM_FIX_INT(a->y[i]) - g->room.cameraY,
                                              a->facing[i] < 0, 1, (uint8_t)(firstOam + used), (uint8_t)(maxOam - used)));
  }
  return used;
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
