/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "sprite.h"

#include <string.h>

#include "hal.h"
#include "video.h"

#define HEADER_SIZE 36
#define HEADER_SIZE_V2 52   // version 2: the boxes' tables follow the version 1 header
#define HEADER_SIZE_V3 56   // version 3: and the number of palettes
#define HEADER_SIZE_V4 68   // version 4: and the points' tables
#define HEADER_SIZE_V5 72   // version 5: and the slots' normals
#define POINT_SIZE 4
#define BOX_SIZE 6
#define FRAME_SIZE 12
#define PIECE_SIZE 6
#define ANIM_SIZE 8
#define STEP_SIZE 4

int32_t bmSpriteSheetChunk(const BmPack *pPack, uint32_t n)
{
  int32_t chunk = -1;
  for (uint32_t i = 0; i <= n; i++)
  {
    chunk = bmPackFindChunk(pPack, BM_CHUNK_SPRT, (uint32_t)(chunk + 1));
    if (chunk < 0) return -1;
  }
  return chunk;
}

BmResult bmSpriteSheetOpen(BmSpriteSheet *s, const BmPack *pPack, uint32_t chunk)
{
  uint32_t size = 0;
  const uint8_t *p = bmPackChunkData(pPack, chunk, &size);
  if (p == NULL || size < HEADER_SIZE) return BM_ERROR_INVALID_ARGUMENT;
  *s = (BmSpriteSheet){0};
  s->pData = p;
  s->nameId = bmRead16(p + 0);
  s->frameCount = bmRead16(p + 2);
  s->animCount = bmRead16(p + 4);
  s->tileCount = bmRead16(p + 6);
  uint16_t pieces = bmRead16(p + 8), steps = bmRead16(p + 10);
  uint32_t at[6];
  for (int i = 0; i < 6; i++) at[i] = bmRead32(p + 12 + i * 4);
  if (at[0] + (uint32_t)s->frameCount * FRAME_SIZE > size || at[1] + (uint32_t)pieces * PIECE_SIZE > size ||
      at[2] + (uint32_t)s->animCount * ANIM_SIZE > size || at[3] + (uint32_t)steps * STEP_SIZE > size ||
      at[4] + 32 > size || at[5] > size)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  s->pFrames = p + at[0];
  s->pPieces = p + at[1];
  s->pAnims = p + at[2];
  s->pSteps = p + at[3];
  s->pPalette = p + at[4];
  s->pTiles = p + at[5];
  s->pEnd = p + size;
  // Version 2's flags (byte 51): bit 0 the tiles are packed; else they must all be there.
  s->packed = at[0] >= HEADER_SIZE_V2 && size >= HEADER_SIZE_V2 && (p[51] & 1) != 0;
  if (!s->packed && at[5] + (uint32_t)s->tileCount * 32 > size) return BM_ERROR_INVALID_ARGUMENT;
  // Version 2 (sprite-format.md): its frames start behind a longer header with the boxes.
  if (at[0] >= HEADER_SIZE_V2 && size >= HEADER_SIZE_V2)
  {
    uint32_t indexAt = bmRead32(p + 36), boxesAt = bmRead32(p + 40), namesAt = bmRead32(p + 44);
    s->boxCount = bmRead16(p + 48);
    s->boxNameCount = p[50];
    if (indexAt + (uint32_t)s->frameCount * 2 > size || boxesAt + (uint32_t)s->boxCount * BOX_SIZE > size ||
        namesAt + (uint32_t)s->boxNameCount * 2 > size)
      return BM_ERROR_INVALID_ARGUMENT;
    s->pBoxIndex = p + indexAt;
    s->pBoxes = p + boxesAt;
    s->pBoxNames = p + namesAt;
    for (uint16_t f = 0; f < s->frameCount; f++)
      if ((uint32_t)bmRead16(s->pBoxIndex + f * 2) + s->pFrames[f * FRAME_SIZE + 3] > s->boxCount) return BM_ERROR_INVALID_ARGUMENT;
  }
  // Version 3: the number of palettes (byte 52), the default one first, their names after them.
  s->paletteCount = 1;
  if (at[0] >= HEADER_SIZE_V3 && size >= HEADER_SIZE_V3)
  {
    uint16_t n = bmRead16(p + 52);
    if (n == 0 || n > BM_SPRITE_MAX_PALETTES || at[4] + (uint32_t)n * 34 > size) return BM_ERROR_INVALID_ARGUMENT;
    s->paletteCount = (uint8_t)n;
    s->pPaletteNames = s->pPalette + n * 32;
  }
  // Version 5: the normals of the palette slots (16 x 4 bytes; offset 0: none).
  if (at[0] >= HEADER_SIZE_V5 && size >= HEADER_SIZE_V5)
  {
    uint32_t normalsAt = bmRead32(p + 68);
    if (normalsAt != 0 && normalsAt + 64 > size) return BM_ERROR_INVALID_ARGUMENT;
    if (normalsAt != 0) s->pNormals = p + normalsAt;
  }
  // Version 4: the points (sprite-format.md).
  if (at[0] >= HEADER_SIZE_V4 && size >= HEADER_SIZE_V4)
  {
    uint32_t indexAt = bmRead32(p + 56), namesAt = bmRead32(p + 60), count = bmRead16(p + 64);
    uint32_t pointsAt = indexAt + ((uint32_t)s->frameCount + 1) * 2;
    s->pointNameCount = p[66];
    if (pointsAt + count * POINT_SIZE > size || namesAt + (uint32_t)s->pointNameCount * 2 > size) return BM_ERROR_INVALID_ARGUMENT;
    s->pPointIndex = p + indexAt;
    s->pPoints = p + pointsAt;
    s->pPointNames = p + namesAt;
    for (uint16_t f = 0; f < s->frameCount; f++)
    {
      uint32_t from = bmRead16(s->pPointIndex + f * 2), to = bmRead16(s->pPointIndex + f * 2 + 2);
      if (from > to || to > count) return BM_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < count; i++)
      if ((s->pPoints[i * POINT_SIZE + 3] & 0x7F) >= s->pointNameCount) return BM_ERROR_INVALID_ARGUMENT;
  }
  // Every reference must stay inside its table, so drawing never reads outside the chunk.
  for (uint16_t f = 0; f < s->frameCount; f++)
  {
    const uint8_t *fr = s->pFrames + f * FRAME_SIZE;
    if ((uint32_t)bmRead16(fr) + fr[2] > pieces) return BM_ERROR_INVALID_ARGUMENT;
  }
  for (uint16_t a = 0; a < s->animCount; a++)
  {
    const uint8_t *an = s->pAnims + a * ANIM_SIZE;
    if ((uint32_t)bmRead16(an + 2) + bmRead16(an + 4) > steps) return BM_ERROR_INVALID_ARGUMENT;
  }
  for (uint16_t i = 0; i < steps; i++)
  {
    if (bmRead16(s->pSteps + i * STEP_SIZE) >= s->frameCount) return BM_ERROR_INVALID_ARGUMENT;
  }
  return BM_SUCCESS;
}

static bool sameName(const char *a, const char *b)
{
  if (a == NULL || b == NULL) return false;
  for (;; a++, b++)
  {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
    if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
    if (x != y) return false;
    if (x == 0) return true;
  }
}

BmResult bmSpriteSheetFind(BmSpriteSheet *s, const BmPack *pPack, const char *pName)
{
  for (int32_t chunk = bmPackFindChunk(pPack, BM_CHUNK_SPRT, 0); chunk >= 0; chunk = bmPackFindChunk(pPack, BM_CHUNK_SPRT, (uint32_t)chunk + 1))
  {
    BmSpriteSheet sheet;
    if (bmSpriteSheetOpen(&sheet, pPack, (uint32_t)chunk) != BM_SUCCESS) continue;
    if (sameName(bmPackString(pPack, sheet.nameId), pName))
    {
      *s = sheet;
      return BM_SUCCESS;
    }
  }
  return BM_ERROR_NOT_FOUND;
}

// Packed tiles (sprite-format.md): unpacked a tile at a time into sprite memory, with the last
// 512 bytes kept for the copies. Packed data that would read past the chunk, copy from before
// its start or make too many tiles is refused (what was unpacked so far stays).
static BmResult unpackTiles(const BmSpriteSheet *s, uint16_t tileBase)
{
  uint8_t history[512];
  uint32_t total = (uint32_t)s->tileCount * 32, made = 0;
  const uint8_t *p = s->pTiles, *end = s->pEnd;
  uint32_t tile = tileBase;
  while (made < total)
  {
    if (p >= end) return BM_ERROR_INVALID_ARGUMENT;
    uint8_t flags = *p++;
    for (int item = 0; item < 8 && made < total; item++)
    {
      if (flags & (1u << item))
      {
        if (p + 2 > end) return BM_ERROR_INVALID_ARGUMENT;
        uint32_t length = (uint32_t)(p[0] >> 4) + 3, distance = ((uint32_t)(p[0] & 15) << 8 | p[1]) + 1;
        p += 2;
        if (distance > made || distance > sizeof(history) || made + length > total) return BM_ERROR_INVALID_ARGUMENT;
        for (uint32_t k = 0; k < length; k++, made++) history[made & 511] = history[(made - distance) & 511];
      }
      else
      {
        if (p >= end) return BM_ERROR_INVALID_ARGUMENT;
        history[made & 511] = *p++;
        made++;
      }
      // Each tile as soon as it is whole (32 bytes, 32-aligned: it sits within the history).
      while (made >= (tile - tileBase + 1) * 32)
      {
        BmResult r = bmTilesLoadObj((uint16_t)tile, &history[((tile - tileBase) * 32) & 511], 32);
        if (r != BM_SUCCESS) return r;
        tile++;
      }
    }
  }
  return BM_SUCCESS;
}

BmResult bmSpriteSheetLoadTiles(BmSpriteSheet *s, uint16_t tileBase)
{
  if ((uint32_t)tileBase + s->tileCount > 1024) return BM_ERROR_INVALID_ARGUMENT;
  s->tileBase = tileBase;
  return s->packed ? unpackTiles(s, tileBase) : bmTilesLoadObj(tileBase, s->pTiles, (uint32_t)s->tileCount * 32);
}

void bmSpriteSheetLoadPalette(const BmSpriteSheet *s, uint8_t palette, uint8_t bank)
{
  if (palette >= s->paletteCount) palette = 0;
  BmColor colours[16];
  for (int i = 0; i < 16; i++) colours[i].color = bmRead16(s->pPalette + palette * 32 + i * 2);
  bmPaletteLoadObj((uint16_t)(bank * 16), colours, 16);
}

int32_t bmSpriteSheetPalette(const BmSpriteSheet *s, const BmPack *pPack, const char *pName)
{
  if (pName == NULL) return -1;
  if (strcmp(pName, "default") == 0) return 0;
  for (uint8_t k = 1; k < s->paletteCount && s->pPaletteNames != NULL; k++)
  {
    const char *n = bmPackString(pPack, bmRead16(s->pPaletteNames + k * 2));
    if (n != NULL && strcmp(n, pName) == 0) return k;
  }
  return -1;
}

BmResult bmSpriteSheetLoad(BmSpriteSheet *s, uint16_t tileBase, uint8_t bank)
{
  if ((uint32_t)tileBase + s->tileCount > 1024 || bank > 15) return BM_ERROR_INVALID_ARGUMENT;
  s->bank = bank;
  BmResult r = bmSpriteSheetLoadTiles(s, tileBase);
  if (r != BM_SUCCESS) return r;
  bmSpriteSheetLoadPalette(s, 0, bank);
  return BM_SUCCESS;
}

int32_t bmSpriteSheetAnim(const BmSpriteSheet *s, const BmPack *pPack, const char *pName)
{
  for (uint16_t a = 0; a < s->animCount; a++)
  {
    if (sameName(bmPackString(pPack, bmRead16(s->pAnims + a * ANIM_SIZE)), pName)) return a;
  }
  return -1;
}

void bmAnimPlay(BmAnimState *st, uint8_t anim, bool restart)
{
  if (st->anim == anim && !restart) return;
  st->anim = anim;
  st->step = 0;
  st->timer = 0;
  st->finished = 0;
}

void bmAnimTick(const BmSpriteSheet *s, BmAnimState *st)
{
  if (st->anim >= s->animCount) return;
  const uint8_t *an = s->pAnims + st->anim * ANIM_SIZE;
  uint16_t first = bmRead16(an + 2), count = bmRead16(an + 4);
  if (count == 0 || (an[6] & 2) != 0) return;   // a direction set is not played in time
  if (st->step >= count) st->step = 0;
  uint16_t duration = bmRead16(s->pSteps + (first + st->step) * STEP_SIZE + 2);
  if (++st->timer < duration) return;
  st->timer = 0;
  if (st->step + 1 < count) st->step++;
  else if (an[6] & 1) st->step = 0;
  else st->finished = 1;
}

uint16_t bmAnimFrame(const BmSpriteSheet *s, const BmAnimState *st)
{
  if (st->anim >= s->animCount) return 0;
  const uint8_t *an = s->pAnims + st->anim * ANIM_SIZE;
  uint16_t first = bmRead16(an + 2), count = bmRead16(an + 4);
  if (count == 0) return 0;
  uint16_t step = st->step < count ? st->step : (uint16_t)(count - 1);
  return bmRead16(s->pSteps + (first + step) * STEP_SIZE);
}

uint32_t bmAnimLength(const BmSpriteSheet *s, uint8_t anim)
{
  if (anim >= s->animCount) return 0;
  const uint8_t *an = s->pAnims + anim * ANIM_SIZE;
  uint16_t first = bmRead16(an + 2), count = bmRead16(an + 4);
  uint32_t total = 0;
  for (uint16_t i = 0; i < count; i++) total += bmRead16(s->pSteps + (first + i) * STEP_SIZE + 2);
  return total;
}

uint16_t bmAnimFrameAt(const BmSpriteSheet *s, uint8_t anim, uint32_t time)
{
  if (anim >= s->animCount) return 0;
  const uint8_t *an = s->pAnims + anim * ANIM_SIZE;
  uint16_t first = bmRead16(an + 2), count = bmRead16(an + 4);
  if (count == 0) return 0;
  uint32_t total = 0;
  for (uint16_t i = 0; i < count; i++) total += bmRead16(s->pSteps + (first + i) * STEP_SIZE + 2);
  if (total == 0) return bmRead16(s->pSteps + first * STEP_SIZE);
  time %= total;
  for (uint16_t i = 0; i < count; i++)
  {
    uint16_t duration = bmRead16(s->pSteps + (first + i) * STEP_SIZE + 2);
    if (time < duration) return bmRead16(s->pSteps + (first + i) * STEP_SIZE);
    time -= duration;
  }
  return bmRead16(s->pSteps + first * STEP_SIZE);
}

uint8_t bmSpriteSheetDraw(const BmSpriteSheet *s, uint16_t frame, int32_t x, int32_t y, bool hflip, uint8_t priority,
                          uint8_t firstOam, uint8_t maxOam)
{
  return bmSpriteSheetDrawBank(s, frame, x, y, hflip, priority, s->bank, firstOam, maxOam);
}

uint8_t bmSpriteSheetDrawBank(const BmSpriteSheet *s, uint16_t frame, int32_t x, int32_t y, bool hflip, uint8_t priority,
                              uint8_t bank, uint8_t firstOam, uint8_t maxOam)
{
  return bmSpriteSheetDrawFlipped(s, frame, x, y, hflip, false, priority, bank, firstOam, maxOam);
}

uint8_t bmSpriteSheetDrawFlipped(const BmSpriteSheet *s, uint16_t frame, int32_t x, int32_t y, bool hflip, bool vflip,
                                 uint8_t priority, uint8_t bank, uint8_t firstOam, uint8_t maxOam)
{
  if (frame >= s->frameCount) return 0;
  static const uint8_t widths[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
  static const uint8_t heights[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };
  const uint8_t *fr = s->pFrames + frame * FRAME_SIZE;
  uint16_t first = bmRead16(fr);
  uint8_t used = 0;
  for (uint8_t i = 0; i < fr[2] && used < maxOam; i++)
  {
    const uint8_t *pc = s->pPieces + (first + i) * PIECE_SIZE;
    uint8_t shape = pc[2] & 3, size = (pc[2] >> 2) & 3;
    if (shape > 2) continue;
    int32_t w = widths[shape][size], h = heights[shape][size];
    int32_t dx = (int8_t)pc[0], dy = (int8_t)pc[1];
    int32_t sx = hflip ? x - dx - w : x + dx, sy = vflip ? y - dy - h : y + dy;
    if (sx <= -w || sx >= BM_HAL_SCREEN_WIDTH || sy <= -h || sy >= BM_HAL_SCREEN_HEIGHT) continue;
    BmSprite sprite = {
      .x = (int16_t)sx, .y = (int16_t)sy, .shape = (BmSpriteShape)shape, .size = size,
      .tile = (uint16_t)(s->tileBase + bmRead16(pc + 4)), .paletteBank = bank, .priority = priority,
      .hflip = hflip, .vflip = vflip, .visible = true,
    };
    bmSpriteSet((uint8_t)(firstOam + used), &sprite);
    used++;
  }
  return used;
}

// x * f / 256, rounded to the nearest (halves away from zero).
static int32_t mulRound(int32_t x, int32_t f)
{
  int32_t p = x * f;
  return p >= 0 ? (p + 128) >> 8 : -((-p + 128) >> 8);
}

uint8_t bmSpriteSheetDrawSheared(const BmSpriteSheet *s, uint16_t frame, int32_t x, int32_t y, int32_t xs, int32_t ys, int32_t vs,
                                 uint8_t matrix, uint8_t priority, uint8_t firstOam, uint8_t maxOam)
{
  if (frame >= s->frameCount || matrix >= 32) return 0;
  if (xs > -16 && xs < 16) xs = xs < 0 ? -16 : 16;
  vs = vs < 64 ? 64 : vs > 512 ? 512 : vs;
  // Screen to texture: the inverse of [xs 0; ys vs] / 256.
  bmSpriteSetMatrix(matrix, (int16_t)(65536 / xs), 0, (int16_t)(-ys * 65536 / xs / vs), (int16_t)(65536 / vs));
  static const uint8_t widths[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
  static const uint8_t heights[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };
  const uint8_t *fr = s->pFrames + frame * FRAME_SIZE;
  uint16_t first = bmRead16(fr);
  uint8_t used = 0;
  for (uint8_t i = 0; i < fr[2] && used < maxOam; i++)
  {
    const uint8_t *pc = s->pPieces + (first + i) * PIECE_SIZE;
    uint8_t shape = pc[2] & 3, size = (pc[2] >> 2) & 3;
    if (shape > 2) continue;
    int32_t w = widths[shape][size], h = heights[shape][size];
    // The piece's centre, from the anchor, moved as the frame is; the box (2w x 2h) around it.
    int32_t u = (int8_t)pc[0] + w / 2, v = (int8_t)pc[1] + h / 2;
    int32_t cx = x + mulRound(u, xs), cy = y + mulRound(u, ys) + mulRound(v, vs);
    int32_t sx = cx - w, sy = cy - h;
    if (sx <= -2 * w || sx >= BM_HAL_SCREEN_WIDTH || sy <= -2 * h || sy >= BM_HAL_SCREEN_HEIGHT) continue;
    BmSprite sprite = {
      .x = (int16_t)sx, .y = (int16_t)sy, .shape = (BmSpriteShape)shape, .size = size,
      .tile = (uint16_t)(s->tileBase + bmRead16(pc + 4)), .paletteBank = s->bank, .priority = priority,
      .visible = true, .affine = (uint8_t)(matrix + 1),
    };
    bmSpriteSet((uint8_t)(firstOam + used), &sprite);
    used++;
  }
  return used;
}

uint8_t bmSpriteSheetDrawReflected(const BmSpriteSheet *s, uint16_t frame, int32_t x, int32_t y, bool hflip, int32_t surface,
                                   uint8_t squash, uint8_t matrix, uint8_t priority, uint8_t bank, uint8_t firstOam, uint8_t maxOam)
{
  if (frame >= s->frameCount || matrix >= 32 || squash == 0) return 0;
  // Screen to texture: x as it is (or mirrored), y upside down and stretched by the squeeze.
  bmSpriteSetMatrix(matrix, (int16_t)(hflip ? -256 : 256), 0, 0, (int16_t)(-(int32_t)squash * 16));
  static const uint8_t widths[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
  static const uint8_t heights[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };
  const uint8_t *fr = s->pFrames + frame * FRAME_SIZE;
  uint16_t first = bmRead16(fr);
  uint8_t used = 0;
  for (uint8_t i = 0; i < fr[2] && used < maxOam; i++)
  {
    const uint8_t *pc = s->pPieces + (first + i) * PIECE_SIZE;
    uint8_t shape = pc[2] & 3, size = (pc[2] >> 2) & 3;
    if (shape > 2) continue;
    int32_t w = widths[shape][size], h = heights[shape][size];
    int32_t dx = (int8_t)pc[0], dy = (int8_t)pc[1];
    // The piece's middle where it is drawn, then mirrored in the line (rounded: 1/2 pixel).
    int32_t cx = (hflip ? x - dx - w : x + dx) + w / 2, cy2 = 2 * (y + dy) + h;   // twice the middle row
    int32_t below = ((2 * surface - cy2) * 16 / squash + 1) / 2;                 // rows under the line
    int32_t cy = surface + below;
    int32_t sx = cx - w, sy = cy - h;
    if (sx <= -2 * w || sx >= BM_HAL_SCREEN_WIDTH || sy <= -2 * h || sy >= BM_HAL_SCREEN_HEIGHT) continue;
    BmSprite sprite = {
      .x = (int16_t)sx, .y = (int16_t)sy, .shape = (BmSpriteShape)shape, .size = size,
      .tile = (uint16_t)(s->tileBase + bmRead16(pc + 4)), .paletteBank = bank, .priority = priority,
      .visible = true, .affine = (uint8_t)(matrix + 1), .mode = BM_SPRITE_SEMI_TRANSPARENT,
    };
    bmSpriteSet((uint8_t)(firstOam + used), &sprite);
    used++;
  }
  return used;
}

BmSpriteBox bmSpriteSheetHitbox(const BmSpriteSheet *s, uint16_t frame, bool hflip)
{
  BmSpriteBox box = { 0, 0, 0, 0 };
  if (frame >= s->frameCount) return box;
  const uint8_t *fr = s->pFrames + frame * FRAME_SIZE;
  box.x = (int8_t)fr[4];
  box.y = (int8_t)fr[5];
  box.w = fr[6];
  box.h = fr[7];
  if (hflip) box.x = (int8_t)(-box.x - box.w);
  return box;
}

uint8_t bmSpriteSheetBoxes(const BmSpriteSheet *s, uint16_t frame, bool hflip, BmSpriteNamedBox *pBoxes, uint8_t max)
{
  if (s->pBoxIndex == NULL || frame >= s->frameCount) return 0;
  uint16_t first = bmRead16(s->pBoxIndex + frame * 2);
  uint8_t count = s->pFrames[frame * FRAME_SIZE + 3];
  for (uint8_t i = 0; i < count && i < max; i++)
  {
    const uint8_t *b = s->pBoxes + (uint32_t)(first + i) * BOX_SIZE;
    BmSpriteNamedBox *o = &pBoxes[i];
    o->box.x = (int8_t)b[0];
    o->box.y = (int8_t)b[1];
    o->box.w = b[2];
    o->box.h = b[3];
    if (hflip) o->box.x = (int8_t)(-o->box.x - o->box.w);
    o->kind = b[4];
    o->name = b[5] < s->boxNameCount ? bmRead16(s->pBoxNames + b[5] * 2) : 0xFFFF;
  }
  return count;
}

bool bmSpriteSheetPoint(const BmSpriteSheet *s, uint16_t frame, uint16_t name, bool hflip, bool vflip, BmSpritePoint *pOut)
{
  if (s->pPointIndex == NULL || frame >= s->frameCount) return false;
  uint32_t from = bmRead16(s->pPointIndex + frame * 2), to = bmRead16(s->pPointIndex + frame * 2 + 2);
  for (uint32_t i = from; i < to; i++)
  {
    const uint8_t *pt = s->pPoints + i * POINT_SIZE;
    if (bmRead16(s->pPointNames + (pt[3] & 0x7F) * 2) != name) continue;
    int32_t x = (int8_t)pt[0], y = (int8_t)pt[1];
    uint8_t angle = pt[2];
    // The angle counts from forward, so facing the other way leaves it as it is; upside down
    // it turns the other way round.
    if (vflip) angle = (uint8_t)(256 - angle);
    pOut->x = (int8_t)(hflip ? -x : x);
    pOut->y = (int8_t)(vflip ? -y : y);
    pOut->angle = angle;
    pOut->behind = (pt[3] & 0x80) != 0;
    return true;
  }
  return false;
}

int32_t bmSpriteSheetAnimById(const BmSpriteSheet *s, uint16_t name)
{
  for (uint16_t a = 0; a < s->animCount; a++)
    if (bmRead16(s->pAnims + a * ANIM_SIZE) == name) return a;
  return -1;
}

uint16_t bmSpriteSheetAnimName(const BmSpriteSheet *s, uint8_t anim)
{
  return anim < s->animCount ? bmRead16(s->pAnims + anim * ANIM_SIZE) : 0xFFFF;
}

uint16_t bmSpriteSheetAnimSteps(const BmSpriteSheet *s, uint8_t anim)
{
  return anim < s->animCount ? bmRead16(s->pAnims + anim * ANIM_SIZE + 4) : 0;
}

bool bmSpriteSheetAnimIsDirections(const BmSpriteSheet *s, uint8_t anim)
{
  return anim < s->animCount && (s->pAnims[anim * ANIM_SIZE + 6] & 2) != 0;
}

uint16_t bmSpriteSheetDirectionStep(const BmSpriteSheet *s, uint8_t anim, uint8_t angle)
{
  if (!bmSpriteSheetAnimIsDirections(s, anim)) return 0;
  const uint8_t *an = s->pAnims + anim * ANIM_SIZE;
  uint16_t first = bmRead16(an + 2), count = bmRead16(an + 4), best = 0;
  int32_t bestDistance = 256;
  for (uint16_t i = 0; i < count; i++)
  {
    // The step's angle is where another animation has its duration.
    int32_t d = (int32_t)((bmRead16(s->pSteps + (first + i) * STEP_SIZE + 2) - angle) & 255);
    if (d > 128) d = 256 - d;
    if (d < bestDistance)
    {
      bestDistance = d;
      best = i;
    }
  }
  return best;
}

int32_t bmSpriteSheetFirstDirections(const BmSpriteSheet *s)
{
  for (uint16_t a = 0; a < s->animCount; a++)
    if (s->pAnims[a * ANIM_SIZE + 6] & 2) return a;
  return -1;
}
