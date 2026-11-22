/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "overlay.h"

#include "hal.h"
#include "ui.h"

static BmUi s_ui BM_EWRAM_BSS;
static uint16_t s_keys;

void bmOverlayInit(const BmPack *pPack, bool shown)
{
  if (bmUiInit(&s_ui, pPack, "font") != BM_SUCCESS) return;
  bmUiFrame(&s_ui, 0, 0, 30, 5);
  bmUiLabel(&s_ui, 1, 1, "ROOM");
  bmUiLabel(&s_ui, 1, 2, "FRAME");
  bmUiLabel(&s_ui, 16, 2, "HP");
  bmUiLabel(&s_ui, 1, 3, "X");
  bmUiLabel(&s_ui, 8, 3, "Y");
  bmUiLabel(&s_ui, 16, 3, "LOAD");
  bmUiLabel(&s_ui, 27, 3, "%");
  bmUiShow(&s_ui, shown);
  bmUiPresent(&s_ui);
}

void bmOverlayFrame(const BmGame *g, uint16_t keys, uint32_t lines)
{
  if (!s_ui.ready) return;
  uint16_t pressed = (uint16_t)(keys & ~s_keys);
  s_keys = keys;
  if ((pressed & BM_KEY_SELECT) && (keys & (BM_KEY_L | BM_KEY_R)) == (BM_KEY_L | BM_KEY_R)) bmUiShow(&s_ui, !s_ui.shown);
  if (s_ui.shown)
  {
    // The room's id, padded with spaces over what a longer one left.
    const char *room = bmGameRoomId(g);
    char line[24];
    int n = 0;
    for (; room != NULL && room[n] != 0 && n < 23; n++) line[n] = room[n];
    for (; n < 23; n++) line[n] = ' ';
    line[23] = 0;
    bmUiLabel(&s_ui, 6, 1, line);
    bmUiNumber(&s_ui, 7, 2, (int32_t)g->frame, 7);
    bmUiNumber(&s_ui, 19, 2, g->health, 3);
    bmUiNumber(&s_ui, 3, 3, BM_FIX_INT(g->player.x), 4);
    bmUiNumber(&s_ui, 10, 3, BM_FIX_INT(g->player.y), 4);
#ifdef BM_PLATFORM_GBA
    bmUiNumber(&s_ui, 23, 3, (int32_t)(lines * 100 / 228), 3);
#else
    (void)lines;
    bmUiLabel(&s_ui, 23, 3, " --");
#endif
  }
  bmUiPresent(&s_ui);
}
