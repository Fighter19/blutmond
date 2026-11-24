/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "grapple.h"

#include <string.h>

#include "game.h"
#include "hal.h"
#include "sound.h"
#include "sprite.h"

#define HOOK_SPEED 8      // pixels a frame, out
#define BACK_SPEED 10     // and back
#define PULL_SPEED 6      // the player towards what the hook holds
#define DRAG_SPEED 5      // a light object towards the player
#define PULL_FRAMES 60    // the most a pull lasts
#define COOLDOWN 12
#define HAND_Y 20         // the hand: this far above the feet
#define CONE_COS2 67      // cos(35 degrees)^2, in hundredths

static const char *const s_animNames[3] = { "hook", "link", "anchor" };

static uint32_t isqrt(uint32_t v)
{
  uint32_t r = 0, bit = 1u << 30;
  while (bit > v) bit >>= 2;
  while (bit != 0)
  {
    if (v >= r + bit)
    {
      v -= r + bit;
      r = (r >> 1) + bit;
    }
    else r >>= 1;
    bit >>= 2;
  }
  return r;
}

static void hand(const BmGame *g, int32_t *px, int32_t *py)
{
  *px = BM_FIX_INT(g->player.x) + g->player.facing * 4;
  *py = BM_FIX_INT(g->player.y) - HAND_Y;
}

// The middle of object i's box.
static void objectMiddle(BmGame *g, uint8_t i, int32_t *px, int32_t *py)
{
  int32_t x, y, w, h;
  bmActorsBox(g, i, &x, &y, &w, &h);
  *px = x + w / 2;
  *py = y + h / 2;
}

// Whether the line from (x0, y0) to (x1, y1) is clear of what is solid (looked at every 4
// pixels, the last 6 left out: what it ends at may stand in a wall's corner).
static bool clearLine(const BmGame *g, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
  int32_t dx = x1 - x0, dy = y1 - y0;
  int32_t d = (int32_t)isqrt((uint32_t)(dx * dx + dy * dy));
  for (int32_t t = 4; t < d - 6; t += 4)
    if (bmCollisionSolidSides(&g->collision, x0 + dx * t / d, y0 + dy * t / d)) return false;
  return true;
}

// Whether the player's box with its feet at (x, y) touches nothing solid.
static bool playerFits(const BmGame *g, int32_t x, int32_t y)
{
  int32_t hw = g->playerParams.halfWidth, h = g->playerParams.height;
  const int32_t xs[3] = { x - hw, x, x + hw - 1 }, ys[3] = { y - h + 1, y - h / 2, y - 1 };
  for (int i = 0; i < 3; i++)
    for (int k = 0; k < 3; k++)
      if (bmCollisionSolidSides(&g->collision, xs[i], ys[k])) return false;
  return true;
}

void bmGrappleEnterRoom(BmGame *g, bool fresh)
{
  BmGrapple *q = &g->grapple;
  if (fresh)
  {
    q->phase = BM_GRAPPLE_IDLE;
    q->target = BM_GRAPPLE_NOTHING;
    q->cooldown = 0;
  }
  q->anchorCount = 0;
  for (uint16_t i = 0; i < g->objectCount && q->anchorCount < 8; i++)
    if (strcmp(g->objects[i].className, "GrappleAnchor") == 0) q->anchors[q->anchorCount++] = i;
  q->sheet = -1;
  for (uint8_t s = 0; s < g->sheetCount; s++)
  {
    const char *name = bmPackString(g->pPack, g->sheets[s].nameId);
    if (name != NULL && strcmp(name, "grapple") == 0) q->sheet = (int8_t)s;
  }
  for (uint8_t a = 0; a < 3; a++)
    q->anims[a] = (int8_t)(q->sheet >= 0 ? bmSpriteSheetAnim(&g->sheets[q->sheet], g->pPack, s_animNames[a]) : -1);
}

// --- Shooting -------------------------------------------------------------------------------

// How well (dx, dy) lies along the aim (ax, ay): its cos^2 in hundredths if within the cone
// and range, else -1.
static int32_t alongAim(int32_t dx, int32_t dy, int32_t ax, int32_t ay)
{
  int32_t d2 = dx * dx + dy * dy, a2 = ax * ax + ay * ay, dot = dx * ax + dy * ay;
  if (d2 < 64 || d2 > BM_GRAPPLE_RANGE * BM_GRAPPLE_RANGE || dot <= 0) return -1;
  int32_t c2 = dot * dot * 100 / (d2 * a2);
  return c2 >= CONE_COS2 ? c2 : -1;
}

static void shoot(BmGame *g, uint16_t keys)
{
  BmGrapple *q = &g->grapple;
  int8_t fwd = (keys & BM_KEY_RIGHT) ? 1 : (keys & BM_KEY_LEFT) ? -1 : 0;
  if (fwd != 0) g->player.facing = fwd;
  int32_t f = g->player.facing;
  int32_t ax = f, ay = -1;                                   // up and ahead
  if ((keys & BM_KEY_UP) && fwd == 0) ax = 0;                // straight up
  else if (!(keys & BM_KEY_UP) && (fwd != 0 || (keys & BM_KEY_DOWN))) ay = 0;   // level ahead
  int32_t hx, hy;
  hand(g, &hx, &hy);
  // The best thing within the cone: an anchor, or an object not on the player's side.
  int32_t best = -1, bestDist = 0x7FFFFFFF;
  uint8_t kind = BM_GRAPPLE_NOTHING, which = 0;
  int32_t tx = 0, ty = 0;
  for (uint8_t k = 0; k < q->anchorCount; k++)
  {
    const BmObjectView *o = &g->objects[q->anchors[k]];
    int32_t c = alongAim(o->x - hx, o->y - hy, ax, ay), d = (o->x - hx) * (o->x - hx) + (o->y - hy) * (o->y - hy);
    if (c < 0 || !clearLine(g, hx, hy, o->x, o->y) || c < best || (c == best && d >= bestDist)) continue;
    best = c, bestDist = d, kind = BM_GRAPPLE_ANCHOR, tx = o->x, ty = o->y;
  }
  const BmActors *a = &g->actors;
  for (uint8_t i = 0; a->hasModule && i < a->count; i++)
  {
    if (a->gone[i] || a->hidden[i] || a->team[i] == BM_HITS_PLAYER_TEAM || a->sheet[i] < 0) continue;
    int32_t mx, my;
    objectMiddle(g, i, &mx, &my);
    int32_t c = alongAim(mx - hx, my - hy, ax, ay), d = (mx - hx) * (mx - hx) + (my - hy) * (my - hy);
    if (c < 0 || !clearLine(g, hx, hy, mx, my) || c < best || (c == best && d >= bestDist)) continue;
    best = c, bestDist = d, kind = BM_GRAPPLE_OBJECT, which = i, tx = mx, ty = my;
  }
  q->target = kind;
  q->object = which;
  q->targetX = (int16_t)tx;
  q->targetY = (int16_t)ty;
  int32_t dx = kind != BM_GRAPPLE_NOTHING ? tx - hx : ax * 100, dy = kind != BM_GRAPPLE_NOTHING ? ty - hy : ay * 100;
  int32_t d = (int32_t)isqrt((uint32_t)(dx * dx + dy * dy));
  q->speedX = dx * HOOK_SPEED * 256 / d;
  q->speedY = dy * HOOK_SPEED * 256 / d;
  q->hookX = BM_FIX(hx);
  q->hookY = BM_FIX(hy);
  q->travelled = 0;
  q->timer = 0;
  q->phase = BM_GRAPPLE_FLYING;
  bmSoundPlayEffect(&g->sound, "grapple");
}

// The hook bites what it flew at, or (target NOTHING) a ledge.
static void bite(BmGame *g)
{
  BmGrapple *q = &g->grapple;
  q->timer = 0;
  bmSoundPlayEffect(&g->sound, "hook");
  if (q->target == BM_GRAPPLE_OBJECT && g->actors.weight[q->object] <= BM_GRAPPLE_LIGHT)
  {
    q->phase = BM_GRAPPLE_PULL_OBJECT;
    return;
  }
  q->phase = BM_GRAPPLE_PULL_PLAYER;
  g->player.vx = g->player.vy = 0;
  g->player.onGround = false;
  g->player.climbing = false;
}

// Whether the hook at (x, y) has hit a ledge: a solid cell whose top it is within 10 pixels of,
// with room above it for the player. Sets the ledge's top corner (on the player's side).
static bool ledgeAt(BmGame *g, int32_t x, int32_t y, int32_t fromX)
{
  BmGrapple *q = &g->grapple;
  int32_t cs = g->collision.cellSize;
  int32_t cx = x >= 0 ? x / cs : -1, cy = y >= 0 ? y / cs : -1;
  int32_t top = cy * cs;
  if (y - top >= 10 || bmCollisionSolidSides(&g->collision, x, top - 1)) return false;
  int8_t side = fromX < cx * cs + cs / 2 ? 1 : -1;   // the wall is right of the player
  int32_t corner = side > 0 ? cx * cs : cx * cs + cs;
  int32_t stand = corner + side * (g->playerParams.halfWidth + 2);
  if (!playerFits(g, stand, top)) return false;
  q->side = side;
  q->targetX = (int16_t)corner;
  q->targetY = (int16_t)top;
  return true;
}

static void flying(BmGame *g)
{
  BmGrapple *q = &g->grapple;
  // At an object: after it, wherever it goes.
  if (q->target == BM_GRAPPLE_OBJECT)
  {
    const BmActors *a = &g->actors;
    if (a->gone[q->object] || a->hidden[q->object])
    {
      q->phase = BM_GRAPPLE_BACK;
      return;
    }
    int32_t mx, my;
    objectMiddle(g, q->object, &mx, &my);
    int32_t dx = mx - BM_FIX_INT(q->hookX), dy = my - BM_FIX_INT(q->hookY);
    int32_t d = (int32_t)isqrt((uint32_t)(dx * dx + dy * dy));
    if (d <= HOOK_SPEED)
    {
      q->hookX = BM_FIX(mx);
      q->hookY = BM_FIX(my);
      bite(g);
      return;
    }
    q->speedX = dx * HOOK_SPEED * 256 / d;
    q->speedY = dy * HOOK_SPEED * 256 / d;
  }
  // Two half steps, each looked at.
  for (int k = 0; k < 2; k++)
  {
    q->hookX += q->speedX / 2;
    q->hookY += q->speedY / 2;
    int32_t x = BM_FIX_INT(q->hookX), y = BM_FIX_INT(q->hookY);
    if (q->target == BM_GRAPPLE_ANCHOR)
    {
      int32_t dx = q->targetX - x, dy = q->targetY - y;
      if (dx * dx + dy * dy <= (HOOK_SPEED / 2) * (HOOK_SPEED / 2))
      {
        q->hookX = BM_FIX(q->targetX);
        q->hookY = BM_FIX(q->targetY);
        bite(g);
        return;
      }
    }
    if (bmCollisionSolidSides(&g->collision, x, y))
    {
      // A wall: a ledge if near its top (only flying free: what it was aimed at is not that).
      if (q->target == BM_GRAPPLE_NOTHING && ledgeAt(g, x, y, BM_FIX_INT(g->player.x)))
      {
        q->target = BM_GRAPPLE_LEDGE;
        bite(g);
      }
      else
      {
        bmSoundPlayEffect(&g->sound, "clang");
        q->phase = BM_GRAPPLE_BACK;
      }
      return;
    }
  }
  q->travelled = (uint16_t)(q->travelled + HOOK_SPEED);
  if (q->travelled >= BM_GRAPPLE_RANGE) q->phase = BM_GRAPPLE_BACK;
}

// Lets the player go: a little on the way they were pulled, or a jump.
static void letGo(BmGame *g, bool jump)
{
  BmGrapple *q = &g->grapple;
  q->phase = BM_GRAPPLE_BACK;
  q->timer = 0;
  if (jump)
  {
    g->player.vy = -(g->playerParams.jumpSpeed * 3 / 4);
    g->player.jumpCutDone = false;
  }
}

// The player pulled towards what the hook holds.
static void pullPlayer(BmGame *g, uint16_t pressed)
{
  BmGrapple *q = &g->grapple;
  BmBody *b = &g->player;
  if ((pressed & BM_KEY_A) || ++q->timer > PULL_FRAMES)
  {
    letGo(g, (pressed & BM_KEY_A) != 0);
    return;
  }
  int32_t hw = g->playerParams.halfWidth;
  // Where the feet go: under an anchor (hanging from it), beside a ledge's top (then up on it),
  // beside a heavy object (the side the player came from).
  int32_t gx, gy;
  if (q->target == BM_GRAPPLE_LEDGE)
  {
    gx = q->targetX - q->side * hw;
    gy = q->targetY + 2;
  }
  else if (q->target == BM_GRAPPLE_OBJECT)
  {
    if (g->actors.gone[q->object] || g->actors.hidden[q->object])
    {
      letGo(g, false);
      return;
    }
    int32_t mx, my, x, y, w, h;
    objectMiddle(g, q->object, &mx, &my);
    bmActorsBox(g, q->object, &x, &y, &w, &h);
    q->hookX = BM_FIX(mx);
    q->hookY = BM_FIX(my);
    int32_t from = BM_FIX_INT(b->x) < mx ? -1 : 1;
    gx = mx + from * (w / 2 + hw + 2);
    gy = my + HAND_Y;
  }
  else
  {
    gx = q->targetX;
    gy = q->targetY + HAND_Y + 6;
  }
  int32_t px = BM_FIX_INT(b->x), py = BM_FIX_INT(b->y);
  int32_t dx = gx - px, dy = gy - py;
  int32_t d = (int32_t)isqrt((uint32_t)(dx * dx + dy * dy));
  if (d <= PULL_SPEED)
  {
    // There.
    if (q->target == BM_GRAPPLE_LEDGE)
    {
      // Up onto the ledge, standing on it.
      bmBodyPlace(b, q->targetX + q->side * (hw + 2), q->targetY);
      b->facing = q->side;
      q->phase = BM_GRAPPLE_BACK;
      return;
    }
    if (playerFits(g, gx, gy))
    {
      int8_t facing = b->facing;
      bmBodyPlace(b, gx, gy);
      b->facing = facing;
    }
    if (q->target == BM_GRAPPLE_ANCHOR)
    {
      // Swung up past it a little, on the way they came.
      b->vx = dx > 0 ? BM_FIX(1) : dx < 0 ? -BM_FIX(1) : 0;
      b->vy = -BM_FIX(2.5);
    }
    letGo(g, false);
    return;
  }
  int32_t nx = px + dx * PULL_SPEED / d, ny = py + dy * PULL_SPEED / d;
  if (!playerFits(g, nx, ny))
  {
    // Something in the way: as far as it goes along one axis, else let go.
    if (playerFits(g, nx, py)) ny = py;
    else if (playerFits(g, px, ny)) nx = px;
    else
    {
      letGo(g, false);
      return;
    }
  }
  b->x = BM_FIX(nx);
  b->y = BM_FIX(ny);
  b->vx = b->vy = 0;
  b->onGround = false;
  if (dx != 0) b->facing = dx > 0 ? 1 : -1;
}

// A light object pulled to the player.
static void pullObject(BmGame *g)
{
  BmGrapple *q = &g->grapple;
  BmActors *a = &g->actors;
  if (a->gone[q->object] || a->hidden[q->object] || ++q->timer > PULL_FRAMES)
  {
    q->phase = BM_GRAPPLE_BACK;
    return;
  }
  int32_t hx, hy, mx, my;
  hand(g, &hx, &hy);
  objectMiddle(g, q->object, &mx, &my);
  int32_t dx = hx - mx, dy = hy - my;
  int32_t d = (int32_t)isqrt((uint32_t)(dx * dx + dy * dy));
  if (d <= 20)
  {
    q->phase = BM_GRAPPLE_BACK;
    return;
  }
  int32_t step = d - 20 < DRAG_SPEED ? d - 20 : DRAG_SPEED;
  a->x[q->object] += BM_FIX(dx * step) / d;
  a->y[q->object] += BM_FIX(dy * step) / d;
  q->hookX = BM_FIX(mx + dx * step / d);
  q->hookY = BM_FIX(my + dy * step / d);
}

bool bmGrappleFrame(BmGame *g, uint16_t keys, uint16_t pressed)
{
  BmGrapple *q = &g->grapple;
  if (q->cooldown) q->cooldown--;
  switch (q->phase)
  {
  case BM_GRAPPLE_IDLE:
    if ((pressed & BM_KEY_R) && q->cooldown == 0 && bmInventoryStat(&g->inventory, BM_STAT_GRAPPLE) > 0 && !g->player.climbing)
      shoot(g, keys);
    return false;
  case BM_GRAPPLE_FLYING:
    flying(g);
    return q->phase == BM_GRAPPLE_PULL_PLAYER;
  case BM_GRAPPLE_PULL_PLAYER:
    pullPlayer(g, pressed);
    return q->phase == BM_GRAPPLE_PULL_PLAYER;
  case BM_GRAPPLE_PULL_OBJECT:
    pullObject(g);
    return false;
  default:
  {
    // Coming back: a new shot goes at once (from one anchor to the next).
    if ((pressed & BM_KEY_R) && bmInventoryStat(&g->inventory, BM_STAT_GRAPPLE) > 0 && !g->player.climbing)
    {
      shoot(g, keys);
      return false;
    }
    // Back to the hand.
    int32_t hx, hy;
    hand(g, &hx, &hy);
    int32_t dx = hx - BM_FIX_INT(q->hookX), dy = hy - BM_FIX_INT(q->hookY);
    int32_t d = (int32_t)isqrt((uint32_t)(dx * dx + dy * dy));
    if (d <= BACK_SPEED)
    {
      q->phase = BM_GRAPPLE_IDLE;
      q->target = BM_GRAPPLE_NOTHING;
      q->cooldown = COOLDOWN;
    }
    else
    {
      q->hookX += BM_FIX(dx * BACK_SPEED) / d;
      q->hookY += BM_FIX(dy * BACK_SPEED) / d;
    }
    return false;
  }
  }
}

uint8_t bmGrappleDraw(BmGame *g, uint8_t firstOam, uint8_t maxOam)
{
  const BmGrapple *q = &g->grapple;
  // Nothing to draw (no anchors, the hook put away): its sheet is not even loaded.
  if ((q->phase == BM_GRAPPLE_IDLE && q->anchorCount == 0) || bmGameSheetBank(g, q->sheet, 0) < 0) return 0;
  const BmSpriteSheet *s = &g->sheets[q->sheet];
  int32_t camX = g->room.cameraX, camY = g->room.cameraY;
  uint8_t used = 0;
  // The chain first (in front), a link every 10 pixels from the hand, then the hook.
  if (q->phase != BM_GRAPPLE_IDLE)
  {
    int32_t hx, hy;
    hand(g, &hx, &hy);
    int32_t kx = BM_FIX_INT(q->hookX), ky = BM_FIX_INT(q->hookY);
    int32_t dx = kx - hx, dy = ky - hy;
    int32_t d = (int32_t)isqrt((uint32_t)(dx * dx + dy * dy));
    if (q->anims[0] >= 0 && used < maxOam)
      used = (uint8_t)(used + bmSpriteSheetDraw(s, bmAnimFrameAt(s, (uint8_t)q->anims[0], 0), kx - camX, ky - camY, dx < 0, 0,
                                               (uint8_t)(firstOam + used), (uint8_t)(maxOam - used)));
    for (int32_t t = 10; q->anims[1] >= 0 && t < d - 4 && used < maxOam; t += 10)
      used = (uint8_t)(used + bmSpriteSheetDraw(s, bmAnimFrameAt(s, (uint8_t)q->anims[1], 0), hx + dx * t / d - camX, hy + dy * t / d - camY,
                                               false, 0, (uint8_t)(firstOam + used), (uint8_t)(maxOam - used)));
  }
  for (uint8_t k = 0; k < q->anchorCount && q->anims[2] >= 0 && used < maxOam; k++)
  {
    const BmObjectView *o = &g->objects[q->anchors[k]];
    used = (uint8_t)(used + bmSpriteSheetDraw(s, bmAnimFrameAt(s, (uint8_t)q->anims[2], g->frame), o->x - camX, o->y - camY, false, 1,
                                             (uint8_t)(firstOam + used), (uint8_t)(maxOam - used)));
  }
  return used;
}
