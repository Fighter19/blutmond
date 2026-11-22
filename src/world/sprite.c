/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "sprite.h"

#include "hal.h"
#include "video.h"

#define HEADER_SIZE 36
#define HEADER_SIZE_V2 52   // version 2: the boxes' tables follow the version 1 header
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
      at[4] + 32 > size || at[5] + (uint32_t)s->tileCount * 32 > size)
  {
    return BM_ERROR_INVALID_ARGUMENT;
  }
  s->pFrames = p + at[0];
  s->pPieces = p + at[1];
  s->pAnims = p + at[2];
  s->pSteps = p + at[3];
  s->pPalette = p + at[4];
  s->pTiles = p + at[5];
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

BmResult bmSpriteSheetLoad(BmSpriteSheet *s, uint16_t tileBase, uint8_t bank)
{
  if ((uint32_t)tileBase + s->tileCount > 1024 || bank > 15) return BM_ERROR_INVALID_ARGUMENT;
  s->tileBase = tileBase;
  s->bank = bank;
  BmResult r = bmTilesLoadObj(tileBase, s->pTiles, (uint32_t)s->tileCount * 32);
  if (r != BM_SUCCESS) return r;
  BmColor colours[16];
  for (int i = 0; i < 16; i++) colours[i].color = bmRead16(s->pPalette + i * 2);
  bmPaletteLoadObj((uint16_t)(bank * 16), colours, 16);
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
  if (count == 0) return;
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
    int32_t sx = hflip ? x - dx - w : x + dx, sy = y + dy;
    if (sx <= -w || sx >= BM_HAL_SCREEN_WIDTH || sy <= -h || sy >= BM_HAL_SCREEN_HEIGHT) continue;
    BmSprite sprite = {
      .x = (int16_t)sx, .y = (int16_t)sy, .shape = (BmSpriteShape)shape, .size = size,
      .tile = (uint16_t)(s->tileBase + bmRead16(pc + 4)), .paletteBank = s->bank, .priority = priority,
      .hflip = hflip, .visible = true,
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
