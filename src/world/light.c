/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "light.h"

#include <string.h>

#include "game.h"
#include "hal.h"
#include "ui.h"
#include "video.h"

#define LITE_HEAD 20

// The kernels below run from ROM: IWRAM has no room for them in every image (the native
// script slot takes it), and from ROM a lit room costs about a sixth of a GBA frame.
#define LIGHT_KERNEL

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
    else
    {
      r >>= 1;
    }
    bit >>= 2;
  }
  return r;
}

// -256..256 from a step of the noise.
static int32_t noiseAt(uint32_t k)
{
  uint32_t h = k * 2654435761u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return (int32_t)((h >> 8) & 511) - 256;
}

// Smoothed value noise: steps of `speed` frames, eased between them.
static int32_t smoothNoise(uint32_t frame, uint32_t speed, uint32_t seed)
{
  if (speed == 0) speed = 1;
  uint32_t k = frame / speed, t = frame % speed;
  int32_t a = noiseAt(k + seed), b = noiseAt(k + 1 + seed);
  int32_t s = (int32_t)(t * 256 / speed);
  s = s * s * (768 - 2 * s) / 65536;   // smoothstep, 0..256
  return a + (b - a) * s / 256;
}

int32_t bmLightIntensity(const BmLight *l, uint32_t frame)
{
  int32_t base = (int32_t)l->intensity * 4;   // /64 to /256
  if (l->flicker == 0) return base;
  int32_t n = (smoothNoise(frame, l->flickerSpeed, 0) * 4 + smoothNoise(frame, (l->flickerSpeed + 1) / 2, 7919)) / 5;
  return base + base * (int32_t)l->flicker / 255 * n / 256;
}

// Layer mode: per region bank and normal, how much it faces the light (4096 = 1.0).
static int16_t s_faces[15][15] BM_EWRAM_BSS;

bool bmLightOn(const BmGame *g)
{
  return g->light.mode != BM_LIGHT_NONE;
}

// The sprite banks' own colours again (after a lit room).
static void restoreSprites(BmGame *g)
{
  for (uint8_t b = 0; b < BM_GAME_BANKS; b++)
    if (g->bankSheet[b] != 0xFF) bmSpriteSheetLoadPalette(&g->sheets[g->bankSheet[b]], g->bankPalette[b], b);
}

void bmLightEnterRoom(BmGame *g)
{
  BmLight *l = &g->light;
  bool wasLit = l->wasLit;
  memset(l, 0, sizeof(*l));
  l->layerBg = 0xFF;
  l->wasLit = wasLit;
  bmUiSetLightLayer(-1);
  uint32_t size = 0;
  const uint8_t *p = g->room.liteChunk != 0 ? bmPackChunkData(g->pPack, g->room.liteChunk, &size) : NULL;
  if (p == NULL || size < LITE_HEAD || (p[0] != BM_LIGHT_PALETTE && p[0] != BM_LIGHT_LAYER))
  {
    if (l->wasLit) restoreSprites(g);
    l->wasLit = false;
    return;
  }
  l->mode = p[0];
  l->pLite = p;
  l->liteSize = size;
  l->levels = p[1];
  l->firstBank = p[2];
  l->bankCount = p[3];
  l->x = (int16_t)bmRead16(p + 4);
  l->y = (int16_t)bmRead16(p + 6);
  l->radius = p[8];
  l->height = p[9];
  uint16_t c = bmRead16(p + 10);
  l->red = c & 31;
  l->green = (c >> 5) & 31;
  l->blue = (c >> 10) & 31;
  l->ambient = p[12];
  l->flicker = p[13];
  l->flickerSpeed = p[14];
  l->intensity = p[15];
  l->gain = p[16];
  l->shadows = p[17];
  if (l->mode == BM_LIGHT_PALETTE)
  {
    if (l->levels < 2 || l->levels > 16 || l->bankCount > 16 || size < LITE_HEAD + (uint32_t)l->bankCount * 16) l->mode = BM_LIGHT_NONE;
    // The banks with lit colours (the others are set once); a level out of range: not lit.
    for (uint32_t i = 0; l->mode != BM_LIGHT_NONE && i < (uint32_t)l->bankCount * 16; i++)
    {
      uint8_t k = p[LITE_HEAD + i];
      if (k >= l->levels) l->mode = BM_LIGHT_NONE;
      else if (k != 0) l->litBanks |= (uint16_t)(1u << (i / 16));
    }
  }
  else
  {
    l->normalCount = l->levels;
    if (l->normalCount > 15 || l->bankCount == 0 || l->firstBank + l->bankCount > 15 || size < LITE_HEAD + 60 + (uint32_t)l->bankCount * 8)
      l->mode = BM_LIGHT_NONE;
    for (uint8_t i = 0; i < g->room.layerCount; i++)
      if (g->room.layers[i].role == BM_ROOM_ROLE_LIGHT) l->layerBg = g->room.layers[i].bg;
    if (l->layerBg == 0xFF) l->mode = BM_LIGHT_NONE;
  }
  if (l->mode == BM_LIGHT_NONE)
  {
    if (wasLit) restoreSprites(g);
    l->wasLit = false;
    return;
  }
  {
    uint32_t palSize = 0;
    const uint8_t *pal = bmPackChunkData(g->pPack, g->room.paletteChunk, &palSize);
    if (pal == NULL || palSize < 4 || palSize < 4u + pal[2] * 32u) l->mode = BM_LIGHT_NONE;
    else
    {
      l->pPalette = pal + 4;
      l->paletteFirst = pal[1];
      l->paletteBanks = pal[2];
    }
  }
  if (l->mode == BM_LIGHT_LAYER)
  {
    // Per region and normal how much it faces the light (constant: only the strength changes).
    const int8_t *normals = (const int8_t *)(p + LITE_HEAD);
    const uint8_t *regions = p + LITE_HEAD + 60;
    for (uint8_t r = 0; r < l->bankCount; r++)
    {
      int32_t fx = (int16_t)bmRead16(regions + r * 8), fy = (int16_t)bmRead16(regions + r * 8 + 2), fz = (int16_t)bmRead16(regions + r * 8 + 4);
      for (uint8_t n = 0; n < 15; n++)
      {
        const int8_t *v = normals + n * 4;
        int32_t d = n < l->normalCount ? (v[0] * fx + v[1] * fy + v[2] * fz) >> 7 : 0;   // 4096 = 1.0 (127 taken as 128)
        s_faces[r][n] = (int16_t)(d < 0 ? 0 : d > 32767 ? 32767 : d);
      }
    }
  }
  if (l->mode == BM_LIGHT_NONE)
  {
    if (wasLit) restoreSprites(g);
    l->wasLit = false;
    return;
  }
  l->wasLit = true;
  l->fresh = true;
  bmLightPresent(g);
}

static int32_t clamp256(int32_t v)
{
  return v < 0 ? 0 : v > 256 ? 256 : v;
}

// --- Kernels: one bank of 16 colours each, run every frame (on the GBA from IWRAM, in ARM code;
// they divide nothing).

// Palette mode: each colour (little endian in `src`) × the factors of its level (256 = 1.0, at
// most 256, so no channel passes 31).
static void LIGHT_KERNEL shadeLevels(uint16_t *out, const uint8_t *src, const uint8_t *levels, const int16_t *fac)
{
  for (int32_t i = 0; i < 16; i++)
  {
    uint32_t c = (uint32_t)src[i * 2] | (uint32_t)src[i * 2 + 1] << 8;
    const int16_t *f = fac + levels[i] * 3;
    uint32_t r = ((c & 31) * (uint32_t)f[0] + 128) >> 8;
    uint32_t gr = (((c >> 5) & 31) * (uint32_t)f[1] + 128) >> 8;
    uint32_t b = (((c >> 10) & 31) * (uint32_t)f[2] + 128) >> 8;
    out[i] = (uint16_t)(r | gr << 5 | b << 10);
  }
}

// Layer mode: per normal the light added in a region, `faces` how much each normal faces it
// (4096 = 1.0); `cr`, `cg`, `cb` the light's colour × its strength (31 × 256 = full).
static void LIGHT_KERNEL regionBank(uint16_t *out, const int16_t *faces, int32_t cr, int32_t cg, int32_t cb)
{
  out[0] = 0;
  for (int32_t n = 0; n < 15; n++)
  {
    int32_t d = faces[n];
    int32_t r = ((d * cr >> 12) + 128) >> 8, gr = ((d * cg >> 12) + 128) >> 8, b = ((d * cb >> 12) + 128) >> 8;
    out[n + 1] = (uint16_t)((r > 31 ? 31 : r) | (gr > 31 ? 31 : gr) << 5 | (b > 31 ? 31 : b) << 10);
  }
}

typedef struct LitArgs
{
  int32_t dx, dy, dz, inv;       // toward the light; 65536 / its length
  int32_t strength;              // the light reaching the figure, 256 = 1.0
  int32_t lr, lg, lb;            // the light's colour, 256 = full
  int32_t ambient;
  int32_t hx, hy, hz, invH;      // the half vector for shiny slots; 65536 / its length
  int32_t red, green, blue;      // the light's colour, 0..31
} LitArgs;

// A sprite bank: per slot albedo × (ambient + light × max(0, N·L)), shiny slots a highlight,
// emissive ones as drawn. `normals` NULL: every slot faces the viewer.
static void LIGHT_KERNEL litSlots(uint16_t *out, const uint8_t *pal, const uint8_t *normals, const LitArgs *a)
{
  out[0] = 0;
  for (int32_t i = 1; i < 16; i++)
  {
    uint32_t c = (uint32_t)pal[i * 2] | (uint32_t)pal[i * 2 + 1] << 8;
    int32_t nx = 0, ny = 0, nz = 127, flags = 0;
    if (normals != NULL)
    {
      nx = (int8_t)normals[i * 4];
      ny = (int8_t)normals[i * 4 + 1];
      nz = (int8_t)normals[i * 4 + 2];
      flags = normals[i * 4 + 3];
    }
    if (flags & BM_SPRITE_SLOT_EMISSIVE)
    {
      out[i] = (uint16_t)c;
      continue;
    }
    int32_t nl = (nx * a->dx + ny * a->dy + nz * a->dz) * a->inv >> 15;   // 256 = 1.0
    if (nl < 0) nl = 0;
    int32_t t = nl * a->strength >> 8;
    int32_t fr = a->ambient + (t * a->lr >> 8), fg = a->ambient + (t * a->lg >> 8), fb = a->ambient + (t * a->lb >> 8);
    fr = fr > 256 ? 256 : fr;
    fg = fg > 256 ? 256 : fg;
    fb = fb > 256 ? 256 : fb;
    int32_t r = (int32_t)(((c & 31) * (uint32_t)fr + 128) >> 8), gr = (int32_t)((((c >> 5) & 31) * (uint32_t)fg + 128) >> 8),
            b = (int32_t)((((c >> 10) & 31) * (uint32_t)fb + 128) >> 8);
    if ((flags & BM_SPRITE_SLOT_SHINY) && a->strength > 0)
    {
      int32_t nh = (nx * a->hx + ny * a->hy + nz * a->hz) * a->invH >> 15;   // 256 = 1.0
      if (nh > 0)
      {
        int32_t sp = nh;
        for (int32_t k = 0; k < 3; k++) sp = sp * sp >> 8;     // ^8
        sp = sp * a->strength >> 8;
        r += a->red * sp >> 8;
        gr += a->green * sp >> 8;
        b += a->blue * sp >> 8;
      }
    }
    out[i] = (uint16_t)((r > 31 ? 31 : r) | (gr > 31 ? 31 : gr) << 5 | (b > 31 ? 31 : b) << 10);
  }
}

// --- The room ----------------------------------------------------------------------------------

// Palette mode: the banks that have lit colours every frame; with `all` (entering the room) the
// others too, which only the ambient darkens.
static void presentPalette(BmGame *g, bool all)
{
  const BmLight *l = &g->light;
  uint32_t first = l->paletteFirst, banks = l->paletteBanks;
  const uint8_t *src = l->pPalette;
  // Per level the factor of each channel: ambient + light × level (two divisions in all).
  int16_t fac[16 * 3];
  int32_t step = (l->now << 8) / (l->levels - 1);   // a level's light, 65536 = 1.0
  int32_t lr = l->red * 256 / 31, lg = l->green * 256 / 31, lb = l->blue * 256 / 31;
  for (uint8_t k = 0; k < l->levels; k++)
  {
    int32_t t = step * k >> 8;   // 256 = 1.0
    fac[k * 3] = (int16_t)clamp256(l->ambient + (t * lr >> 8));
    fac[k * 3 + 1] = (int16_t)clamp256(l->ambient + (t * lg >> 8));
    fac[k * 3 + 2] = (int16_t)clamp256(l->ambient + (t * lb >> 8));
  }
  const uint8_t *levelOf = l->pLite + LITE_HEAD;
  uint16_t out[16];
  for (uint32_t b = 0; b < banks && b < l->bankCount; b++)
  {
    if (!all && !(l->litBanks & (1u << b))) continue;
    shadeLevels(out, src + b * 32, levelOf + b * 16, fac);
    bmHalPaletteWriteBlock((uint16_t)((first + b) * 16), out, 16);
  }
}

// Layer mode: the light layer's banks every frame; with `all` the room's own, darkened.
static void presentLayer(BmGame *g, bool all)
{
  const BmLight *l = &g->light;
  uint16_t out[16];
  if (all)
  {
    uint32_t first = l->paletteFirst, banks = l->paletteBanks;
    const uint8_t *src = l->pPalette;
    int16_t fac[3] = {(int16_t)l->ambient, (int16_t)l->ambient, (int16_t)l->ambient};
    static const uint8_t level0[16] = {0};
    for (uint32_t b = 0; b < banks; b++)
    {
      shadeLevels(out, src + b * 32, level0, fac);
      bmHalPaletteWriteBlock((uint16_t)((first + b) * 16), out, 16);
    }
  }
  int32_t strength = l->now * l->gain >> 6;   // 256 = 1.0
  int32_t cr = l->red * strength, cg = l->green * strength, cb = l->blue * strength;
  for (uint8_t r = 0; r < l->bankCount; r++)
  {
    regionBank(out, s_faces[r], cr, cg, cb);
    bmHalPaletteWriteBlock((uint16_t)((l->firstBank + r) * 16), out, 16);
  }
}

// --- Sprites -----------------------------------------------------------------------------------

// Whether a straight line from (x, y) to the light crosses a solid cell (other than the ones
// it starts and ends in). Stepped in 16.16 fixed point, 8 pixels a step.
static bool shadowed(const BmGame *g, int32_t x, int32_t y)
{
  const BmLight *l = &g->light;
  if (!l->shadows) return false;
  int32_t shift = g->room.collisionCell == 8 ? 3 : 4;
  int32_t dx = l->x - x, dy = l->y - y;
  int32_t steps = ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy)) / 8 + 1;
  int32_t stepX = (dx << 16) / steps, stepY = (dy << 16) / steps;
  int32_t sx = x >> shift, sy = y >> shift, lx = l->x >> shift, ly = l->y >> shift;
  int32_t px = x << 16, py = y << 16;
  for (int32_t k = 1; k < steps; k++)
  {
    px += stepX;
    py += stepY;
    if (px < 0 || py < 0) continue;
    int32_t cx = (px >> 16) >> shift, cy = (py >> 16) >> shift;
    if ((cx == sx && cy == sy) || (cx == lx && cy == ly)) continue;
    uint8_t shape = 0, material = 0;
    bmCollisionCell(&g->collision, cx, cy, &shape, &material);
    if ((shape & 0x0F) == 1 && (shape & 0xC0) == 0xC0) return true;
  }
  return false;
}

// How much of the light reaches a figure whose middle is at (x, y): the falloff, 0 in shadow
// (256 = 1.0).
static int32_t reach(const BmGame *g, int32_t x, int32_t y)
{
  const BmLight *l = &g->light;
  int32_t dx = l->x - x, dy = l->y - y;
  int32_t d2 = dx * dx + dy * dy, r2 = (int32_t)l->radius * l->radius;
  if (d2 >= r2 || shadowed(g, x, y)) return 0;
  int32_t f = 256 - d2 * 256 / r2;
  return f * f >> 8;
}

// One sprite bank lit for a figure whose middle is at (x, y), `fall` of the light reaching it.
static void litBank(BmGame *g, uint8_t bank, int32_t x, int32_t y, int32_t fall)
{
  const BmLight *l = &g->light;
  const BmSpriteSheet *s = &g->sheets[g->bankSheet[bank]];
  uint8_t palette = g->bankPalette[bank] < s->paletteCount ? g->bankPalette[bank] : 0;
  LitArgs a;
  a.dx = l->x - x;
  a.dy = l->y - y;
  a.dz = l->height;
  int32_t len = (int32_t)isqrt((uint32_t)(a.dx * a.dx + a.dy * a.dy + a.dz * a.dz));
  a.inv = 65536 / (len == 0 ? 1 : len);
  a.strength = fall * l->now >> 8;
  a.lr = l->red * 256 / 31;
  a.lg = l->green * 256 / 31;
  a.lb = l->blue * 256 / 31;
  a.ambient = l->ambient;
  a.hx = a.dx * a.inv >> 8;
  a.hy = a.dy * a.inv >> 8;
  a.hz = (a.dz * a.inv >> 8) + 256;
  int32_t hl = (int32_t)isqrt((uint32_t)(a.hx * a.hx + a.hy * a.hy + a.hz * a.hz));
  a.invH = 65536 / (hl == 0 ? 1 : hl);
  a.red = l->red;
  a.green = l->green;
  a.blue = l->blue;
  uint16_t out[16];
  litSlots(out, s->pPalette + palette * 32, s->pNormals, &a);
  bmHalPaletteWriteBlock((uint16_t)(BM_HAL_PALETTE_OBJ_OFFSET + bank * 16), out, 16);
}

static void presentSprites(BmGame *g)
{
  BmLight *l = &g->light;
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y) - 14;
  int32_t playerFall = reach(g, px, py);   // once for all the player's banks
  for (uint8_t b = 0; b < BM_GAME_BANKS; b++)
  {
    if (g->bankSheet[b] == 0xFF) continue;
    int8_t sheet = (int8_t)g->bankSheet[b];
    bool player = false;
    for (uint8_t i = 0; i < g->playerSheetCount; i++) player |= g->playerSheets[i] == sheet;
    int32_t x = px, y = py, fall = playerFall;
    if (!player)
    {
      // The nearest actor drawn with this bank's sheet (figures sharing a bank share its light).
      int32_t best = -1;
      const BmActors *a = &g->actors;
      for (uint8_t i = 0; i < a->count; i++)
      {
        if (a->sheet[i] != sheet || a->hidden[i]) continue;
        int32_t ax = BM_FIX_INT(a->x[i]), ay = BM_FIX_INT(a->y[i]) - 8;
        int32_t d = (ax - l->x) * (ax - l->x) + (ay - l->y) * (ay - l->y);
        if (best < 0 || d < best) best = d, x = ax, y = ay;
      }
      fall = best < 0 ? 0 : reach(g, x, y);   // nothing shows it: ambient only
    }
    // Out of the light a bank's colours are only darkened: written once while that lasts.
    bool same = l->objSheet[b] == (uint8_t)sheet && l->objPalette[b] == g->bankPalette[b];
    if (fall == 0 && same && (l->objDark & (1u << b))) continue;
    litBank(g, b, x, y, fall);
    l->objSheet[b] = (uint8_t)sheet;
    l->objPalette[b] = g->bankPalette[b];
    if (fall == 0) l->objDark |= (uint16_t)(1u << b);
    else l->objDark &= (uint16_t)~(1u << b);
  }
}

// Processor cycles the last frame's light took, per part (tests read them: tools/bmlink check).
uint32_t bm_light_cycles[3];

void bmLightPresent(BmGame *g)
{
  BmLight *l = &g->light;
  if (l->mode == BM_LIGHT_NONE) return;
  uint32_t t0 = bmHalCycles();
  l->now = bmLightIntensity(l, g->frame);
  if (l->mode == BM_LIGHT_PALETTE) presentPalette(g, l->fresh);
  else presentLayer(g, l->fresh);
  l->fresh = false;
  uint32_t t1 = bmHalCycles();
  presentSprites(g);
  uint32_t t2 = bmHalCycles();
  bm_light_cycles[0] = t1 - t0;
  bm_light_cycles[1] = t2 - t1;
  if (l->mode == BM_LIGHT_LAYER)
  {
    // The light layer is added over the room while nothing else uses the colour effect; with a
    // screen open or a room effect it is hidden (its colours alone are not a picture).
    bool free = g->screens.count == 0 && g->fxKind == 0 && g->teleTimer == 0;
    bmUiSetLightLayer(free ? (int8_t)l->layerBg : -1);
    if (free) bmUiBlend();
    bmVideoShowBg(l->layerBg, free);
  }
  bm_light_cycles[2] = bmHalCycles() - t2;
}
