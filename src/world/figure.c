/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "figure.h"

#include <string.h>

#define HEADER 4
#define LAYER_SIZE 12

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

BmResult bmFigureFind(BmFigure *f, const BmPack *pPack, const char *pName)
{
  for (int32_t c = bmPackFindChunk(pPack, BM_CHUNK_FIGR, 0); c >= 0; c = bmPackFindChunk(pPack, BM_CHUNK_FIGR, (uint32_t)c + 1))
  {
    uint32_t size = 0;
    const uint8_t *p = bmPackChunkData(pPack, (uint32_t)c, &size);
    if (p == NULL || size < HEADER || !sameName(bmPackString(pPack, bmRead16(p)), pName)) continue;
    uint8_t n = p[2];
    if (n == 0 || n > BM_FIGURE_MAX_LAYERS || HEADER + (uint32_t)n * LAYER_SIZE > size) return BM_ERROR_INVALID_ARGUMENT;
    memset(f, 0, sizeof(*f));
    f->nameId = bmRead16(p);
    f->layerCount = n;
    for (uint8_t i = 0; i < n; i++)
    {
      const uint8_t *l = p + HEADER + i * LAYER_SIZE;
      BmFigureLayer *o = &f->layers[i];
      o->name = bmRead16(l);
      o->sprite = bmRead16(l + 2);
      o->point = bmRead16(l + 4);
      o->parent = l[6];
      o->mode = l[7];
      o->behind = (l[8] & 1) != 0;
      o->palette = bmRead16(l + 10);
      // The root hangs on nothing; every other layer on one before it.
      if (i == 0 ? o->parent != 0xFF : o->parent >= i) return BM_ERROR_INVALID_ARGUMENT;
      if (o->mode > BM_FIGURE_SYNC || (i == 0 && o->mode != BM_FIGURE_OWN)) return BM_ERROR_INVALID_ARGUMENT;
    }
    return BM_SUCCESS;
  }
  return BM_ERROR_NOT_FOUND;
}

int32_t bmFigureLayerAnim(const BmSpriteSheet *pParent, uint8_t parentAnim, const BmSpriteSheet *pSheet)
{
  if (pParent == pSheet) return parentAnim;
  uint16_t name = bmSpriteSheetAnimName(pParent, parentAnim);
  return name == 0xFFFF ? -1 : bmSpriteSheetAnimById(pSheet, name);
}

void bmFigurePose(const BmFigure *f, const BmSpriteSheet *const sheets[BM_FIGURE_MAX_LAYERS], const BmAnimState states[BM_FIGURE_MAX_LAYERS],
                  bool hflip, bool vflip, BmFigurePose *pose)
{
  BmAnimState eff[BM_FIGURE_MAX_LAYERS];
  bool shown[BM_FIGURE_MAX_LAYERS], behind[BM_FIGURE_MAX_LAYERS];
  memset(pose, 0, sizeof(*pose));
  for (uint8_t i = 0; i < f->layerCount; i++)
  {
    const BmFigureLayer *l = &f->layers[i];
    const BmSpriteSheet *s = sheets[i];
    eff[i] = states[i];
    shown[i] = s != NULL;
    behind[i] = l->behind;
    int32_t px = 0, py = 0;
    if (i != 0)
    {
      uint8_t up = l->parent;
      px = pose->x[up];
      py = pose->y[up];
      // In step with the parent: its animation's namesake, at its step.
      if (l->mode == BM_FIGURE_SYNC && s != NULL && sheets[up] != NULL)
      {
        int32_t a = bmFigureLayerAnim(sheets[up], eff[up].anim, s);
        shown[i] = a >= 0;
        eff[i].anim = (uint8_t)(a < 0 ? 0 : a);
        eff[i].step = eff[up].step;
        eff[i].timer = eff[up].timer;
        eff[i].finished = eff[up].finished;
      }
      BmSpritePoint pt;
      if (l->point != 0xFFFF && sheets[up] != NULL && shown[up] &&
          bmSpriteSheetPoint(sheets[up], pose->frame[up], l->point, hflip, vflip, &pt))
      {
        px += pt.x;
        py += pt.y;
        pose->angle[i] = pt.angle;
        behind[i] = behind[i] || pt.behind;
      }
    }
    pose->x[i] = (int16_t)px;
    pose->y[i] = (int16_t)py;
    pose->frame[i] = s != NULL ? bmAnimFrame(s, &eff[i]) : 0;
  }

  // Back to front: each layer in front of everything so far, or right behind its parent.
  uint8_t seq[BM_FIGURE_MAX_LAYERS], n = 0;
  for (uint8_t i = 0; i < f->layerCount; i++)
  {
    uint8_t at = n;
    if (i != 0 && behind[i])
      for (uint8_t k = 0; k < n; k++)
        if (seq[k] == f->layers[i].parent) at = k;
    memmove(seq + at + 1, seq + at, (size_t)(n - at));
    seq[at] = i;
    n++;
  }
  for (uint8_t k = n; k-- > 0;)
    if (shown[seq[k]]) pose->order[pose->count++] = seq[k];
}
