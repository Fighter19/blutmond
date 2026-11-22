/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "hits.h"

#include <string.h>

#include "game.h"

#define NO_NAME 0xFFFF
#define MAX_RECTS 4

typedef struct Rect
{
  int32_t x, y, w, h;
} Rect;

static bool overlap(const Rect *a, const Rect *b)
{
  return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

// --- Figures --------------------------------------------------------------------------------

static uint8_t figureCount(const BmGame *g)
{
  return (uint8_t)(1 + (g->actors.hasModule ? g->actors.count : 0));
}

static uint8_t teamOf(const BmGame *g, uint8_t f)
{
  return f == BM_HITS_PLAYER ? BM_HITS_PLAYER_TEAM : g->actors.team[f - 1];
}

// Whether a figure can be struck now.
static bool strikable(const BmGame *g, uint8_t f)
{
  if (f == BM_HITS_PLAYER) return g->hurtTimer == 0;
  const BmActors *a = &g->actors;
  uint8_t i = (uint8_t)(f - 1);
  return !a->hidden[i] && a->health[i] > 0 && a->invulnerable[i] == 0;
}

static void anchorOf(const BmGame *g, uint8_t f, int32_t *px, int32_t *py, bool *pFlip)
{
  if (f == BM_HITS_PLAYER)
  {
    *px = BM_FIX_INT(g->player.x);
    *py = BM_FIX_INT(g->player.y);
    *pFlip = g->player.facing < 0;
  }
  else
  {
    *px = BM_FIX_INT(g->actors.x[f - 1]);
    *py = BM_FIX_INT(g->actors.y[f - 1]);
    *pFlip = g->actors.facing[f - 1] < 0;
  }
}

static Rect bodyOf(const BmGame *g, uint8_t f)
{
  Rect r;
  if (f == BM_HITS_PLAYER)
  {
    int32_t hw = g->playerParams.halfWidth;
    r.x = BM_FIX_INT(g->player.x) - hw;
    r.y = BM_FIX_INT(g->player.y) - g->playerParams.height;
    r.w = hw * 2;
    r.h = g->playerParams.height;
  }
  else
  {
    bmActorsBox(g, (uint8_t)(f - 1), &r.x, &r.y, &r.w, &r.h);
  }
  return r;
}

// The sprite sheet and animation of a figure (false: it has none).
static bool animOf(const BmGame *g, uint8_t f, const BmSpriteSheet **ppSheet, BmAnimState *pState)
{
  if (f == BM_HITS_PLAYER)
  {
    if (g->playerSheet < 0) return false;
    *ppSheet = &g->sheets[g->playerSheet];
    *pState = g->playerAnim;
    return true;
  }
  const BmActors *a = &g->actors;
  uint8_t i = (uint8_t)(f - 1);
  if (a->sheet[i] < 0) return false;
  *ppSheet = &g->sheets[a->sheet[i]];
  *pState = (BmAnimState){ a->anim[i], a->animFinished[i], a->animStep[i], a->animTimer[i] };
  return true;
}

// The boxes of `kind` (and, for hit boxes, `name`) on the frame a figure shows, in room pixels.
static uint8_t boxesOf(const BmGame *g, uint8_t f, uint8_t kind, uint16_t name, Rect *pOut)
{
  const BmSpriteSheet *s;
  BmAnimState st;
  if (!animOf(g, f, &s, &st)) return 0;
  int32_t x, y;
  bool flip;
  anchorOf(g, f, &x, &y, &flip);
  BmSpriteNamedBox boxes[12];
  uint8_t n = bmSpriteSheetBoxes(s, bmAnimFrame(s, &st), flip, boxes, 12);
  uint8_t count = 0;
  for (uint8_t b = 0; b < n && count < MAX_RECTS; b++)
  {
    if (boxes[b].kind != kind || (kind == BM_BOX_HIT && boxes[b].name != name)) continue;
    pOut[count++] = (Rect){ x + boxes[b].box.x, y + boxes[b].box.y, boxes[b].box.w, boxes[b].box.h };
  }
  return count;
}

// Where a figure can be struck: its hurt boxes, or its body box if the frame has none.
static uint8_t hurtOf(const BmGame *g, uint8_t f, Rect *pOut)
{
  uint8_t n = boxesOf(g, f, BM_BOX_HURT, NO_NAME, pOut);
  if (n == 0) pOut[n++] = bodyOf(g, f);
  return n;
}

// --- Hits -----------------------------------------------------------------------------------

static void endHit(BmHits *h, uint8_t k)
{
  h->source[k] = BM_HITS_FREE;
  h->slot[k] = BM_HITS_NO_SLOT;
  h->name[k] = NO_NAME;
  h->damage[k] = 0;
  h->element[k] = h->anim[k] = 0;
  h->frames[k] = h->struck[k] = 0;
  h->reach[k] = 0;
}

// The hits of a struck figure end.
static void endHitsOf(BmGame *g, uint8_t f)
{
  for (uint8_t k = 0; k < BM_HITS_MAX; k++)
    if (g->hits.source[k] == f) endHit(&g->hits, k);
}

void bmHitsClear(BmGame *g)
{
  for (uint8_t k = 0; k < BM_HITS_MAX; k++) endHit(&g->hits, k);
}

bool bmHitsArm(BmGame *g, uint8_t source, uint8_t slot, uint16_t name, int32_t damage, uint8_t element, int32_t reach, int32_t frames)
{
  BmHits *h = &g->hits;
  if (source >= figureCount(g)) return false;
  int free = -1;
  for (uint8_t k = 0; k < BM_HITS_MAX; k++)
  {
    // Arming the same boxes again starts a new hit in place of the old one.
    if (name != NO_NAME && h->source[k] == source && h->name[k] == name) { free = k; break; }
    if (free < 0 && h->source[k] == BM_HITS_FREE) free = k;
  }
  if (free < 0) return false;
  const BmSpriteSheet *s;
  BmAnimState st = { 0, 0, 0, 0 };
  animOf(g, source, &s, &st);
  uint8_t k = (uint8_t)free;
  endHit(h, k);
  h->source[k] = source;
  h->slot[k] = slot;
  h->name[k] = name;
  h->damage[k] = (int16_t)(damage < -32768 ? -32768 : damage > 32767 ? 32767 : damage);
  h->element[k] = element;
  h->anim[k] = st.anim;
  h->frames[k] = (uint16_t)(frames < 1 ? 1 : frames > 0xFFFF ? 0xFFFF : frames);
  h->reach[k] = (int16_t)(reach < 0 ? 0 : reach > 255 ? 255 : reach);
  return true;
}

void bmHitsDisarm(BmGame *g, uint8_t source, uint16_t name)
{
  for (uint8_t k = 0; k < BM_HITS_MAX; k++)
    if (g->hits.source[k] == source && g->hits.name[k] == name) endHit(&g->hits, k);
}

uint8_t bmHitsLive(const BmGame *g)
{
  uint8_t n = 0;
  for (uint8_t k = 0; k < BM_HITS_MAX; k++) n = (uint8_t)(n + (g->hits.source[k] != BM_HITS_FREE));
  return n;
}

// Whether an armed hit still holds: its figure is there and shows the animation it was armed
// in, not yet finished.
static bool stillArmed(const BmGame *g, uint8_t k)
{
  const BmHits *h = &g->hits;
  if (h->source[k] >= figureCount(g)) return false;
  if (h->name[k] == NO_NAME) return true;
  const BmSpriteSheet *s;
  BmAnimState st;
  if (!animOf(g, h->source[k], &s, &st)) return true;   // nothing to draw boxes from: it waits for disarm
  if (st.anim != h->anim[k]) return false;
  bool loops = st.anim < s->animCount && (s->pAnims[st.anim * 8 + 6] & 1);
  return loops || !st.finished;
}

// The live rectangles of a hit this frame.
static uint8_t hitRects(const BmGame *g, uint8_t k, Rect *pOut)
{
  const BmHits *h = &g->hits;
  uint8_t f = h->source[k];
  if (h->name[k] != NO_NAME) return boxesOf(g, f, BM_BOX_HIT, h->name[k], pOut);
  int32_t x, y;
  bool flip;
  anchorOf(g, f, &x, &y, &flip);
  Rect body = bodyOf(g, f);
  pOut[0] = (Rect){ flip ? x - h->reach[k] : x, body.y, h->reach[k], body.h };
  return 1;
}

int32_t bmHitsElement(int32_t amount, uint8_t affinity, uint8_t element)
{
  if (element == 0 || element > 3) return amount;
  if (affinity & (1u << element)) amount = amount * 3 / 2;
  else if (affinity & (16u << element)) amount /= 2;
  return amount < 1 ? 1 : amount;
}

// Object i is struck for `amount` (the element applied already) from room x `fromX` (fix): it
// is pushed away from there (as much as it is not steady), blinks and hears of it.
static void strikeObject(BmGame *g, uint8_t i, int32_t amount, int32_t fromX)
{
  BmActors *a = &g->actors;
  a->health[i] -= amount;
  a->invulnerable[i] = BM_HITS_INVULNERABLE;
  int32_t push = (int32_t)BM_HITS_KNOCK * (100 - (a->steady[i] > 100 ? 100 : a->steady[i])) / 100;
  a->knock[i] = a->x[i] < fromX ? -push : push;
  endHitsOf(g, (uint8_t)(i + 1));
  bmActorsEvent(g, i, BM_SCRIPT_EVENT_hit, amount);
}

bool bmHitsStrikeObject(BmGame *g, uint8_t i, int32_t amount, uint8_t element, int32_t fromX)
{
  if (i >= g->actors.count || amount <= 0 || !strikable(g, (uint8_t)(i + 1))) return false;
  strikeObject(g, i, bmHitsElement(amount, g->actors.affinity[i], element), fromX);
  return true;
}

void bmHitsKnockFrame(BmGame *g)
{
  BmActors *a = &g->actors;
  for (uint8_t i = 0; a->hasModule && i < a->count; i++)
  {
    int32_t k = a->knock[i];
    if (k == 0) continue;
    if (a->gone[i] || a->hidden[i])
    {
      a->knock[i] = 0;
      continue;
    }
    // A wall ahead (at the object's side, a little above its anchor) stops the push.
    int32_t x = a->x[i] + k;
    int32_t side = BM_FIX_INT(x) + (k > 0 ? 6 : -6), y = BM_FIX_INT(a->y[i]);
    if (bmCollisionSolidSides(&g->collision, side, y - 4) || bmCollisionSolidSides(&g->collision, side, y - 12))
    {
      a->knock[i] = 0;
      continue;
    }
    a->x[i] = x;
    // Slower every frame, until it stands.
    if (k > 0) a->knock[i] = k > BM_HITS_KNOCK_SLOWING ? k - BM_HITS_KNOCK_SLOWING : 0;
    else a->knock[i] = k < -BM_HITS_KNOCK_SLOWING ? k + BM_HITS_KNOCK_SLOWING : 0;
  }
}

// Figure `target` is struck by hit `k` for `amount`.
static void strike(BmGame *g, uint8_t k, uint8_t target, int32_t amount)
{
  BmHits *h = &g->hits;
  uint8_t source = h->source[k], slot = h->slot[k];
  h->struck[k] = (uint16_t)(h->struck[k] | (1u << target));
  int32_t sx, sy;
  bool flip;
  anchorOf(g, source, &sx, &sy, &flip);
  if (target == BM_HITS_PLAYER)
  {
    bmGameStrikePlayerWith(g, amount, BM_FIX(sx), h->element[k], source);
  }
  else
  {
    uint8_t i = (uint8_t)(target - 1);
    strikeObject(g, i, bmHitsElement(amount, g->actors.affinity[i], h->element[k]), BM_FIX(sx));
  }
  // The source hears of it: the attachment that armed the hit, or the object.
  if (slot != BM_HITS_NO_SLOT) bmAttachmentsEvent(g, slot, BM_SCRIPT_EVENT_struck, amount);
  else if (source != BM_HITS_PLAYER && source < figureCount(g)) bmActorsEvent(g, (uint8_t)(source - 1), BM_SCRIPT_EVENT_struck, amount);
}

void bmHitsFrame(BmGame *g)
{
  BmHits *h = &g->hits;
  for (uint8_t k = 0; k < BM_HITS_MAX; k++)
  {
    if (h->source[k] == BM_HITS_FREE) continue;
    if (!stillArmed(g, k))
    {
      endHit(h, k);
      continue;
    }
    Rect hit[MAX_RECTS];
    uint8_t n = hitRects(g, k, hit);
    uint8_t source = h->source[k];
    uint16_t name = h->name[k];
    // Figures in a fixed order: the player, then the objects by index.
    for (uint8_t f = 0; n != 0 && f < figureCount(g); f++)
    {
      if (f == source || teamOf(g, f) == teamOf(g, source) || (h->struck[k] & (1u << f)) || !strikable(g, f)) continue;
      Rect hurt[MAX_RECTS];
      uint8_t m = hurtOf(g, f, hurt);
      bool touches = false;
      for (uint8_t a = 0; a < n && !touches; a++)
        for (uint8_t b = 0; b < m && !touches; b++) touches = overlap(&hit[a], &hurt[b]);
      if (!touches) continue;
      // The hit's damage, at least 1; the target's defence (the player's) and its weakness
      // or resistance to the hit's element follow in strike().
      int32_t amount = h->damage[k] < 1 ? 1 : h->damage[k];
      strike(g, k, f, amount);
      // The events may have ended or replaced this hit.
      if (h->source[k] != source || h->name[k] != name) break;
    }
    // A rectangle lasts its frames.
    if (h->source[k] == source && h->name[k] == NO_NAME && name == NO_NAME && --h->frames[k] == 0) endHit(h, k);
  }
  BmActors *a = &g->actors;
  for (uint8_t i = 0; a->hasModule && i < a->count; i++)
    if (a->invulnerable[i]) a->invulnerable[i]--;
}
