/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "overlay.h"

#include "hal.h"
#include "ui.h"

static bool s_shown;
static uint16_t s_keys;

static void drawFrame(BmUi *ui)
{
  bmUiFrame(ui, 0, 0, 30, 5);
  bmUiLabel(ui, 1, 1, "ROOM");
  bmUiLabel(ui, 1, 2, "FRAME");
  bmUiLabel(ui, 16, 2, "HP");
  bmUiLabel(ui, 1, 3, "X");
  bmUiLabel(ui, 8, 3, "Y");
  bmUiLabel(ui, 16, 3, "LOAD");
  bmUiLabel(ui, 27, 3, "%");
}

void bmOverlayInit(BmGame *g, bool shown)
{
  s_shown = shown && g->ui.ready;
  if (!s_shown) return;
  drawFrame(&g->ui);
  bmUiShow(&g->ui, true);
  bmUiPresent(&g->ui);
}

void bmOverlayFrame(BmGame *g, uint16_t keys, uint32_t lines)
{
  BmUi *ui = &g->ui;
  if (!ui->ready) return;
  uint16_t pressed = (uint16_t)(keys & ~s_keys);
  s_keys = keys;
  if ((pressed & BM_KEY_SELECT) && (keys & (BM_KEY_L | BM_KEY_R)) == (BM_KEY_L | BM_KEY_R))
  {
    s_shown = !s_shown;
    if (s_shown) drawFrame(ui);
    else bmUiClearRect(ui, 0, 0, 30, 5);
    bmUiShow(ui, s_shown || ui->shown);
  }
  if (!s_shown) return;
  bmUiShow(ui, true);
  // The room's id, padded with spaces over what a longer one left.
  const char *room = bmGameRoomId(g);
  char line[24];
  int n = 0;
  for (; room != NULL && room[n] != 0 && n < 23; n++) line[n] = room[n];
  for (; n < 23; n++) line[n] = ' ';
  line[23] = 0;
  bmUiLabel(ui, 6, 1, line);
  bmUiNumber(ui, 7, 2, (int32_t)g->frame, 7);
  bmUiNumber(ui, 19, 2, g->health, 3);
  bmUiNumber(ui, 3, 3, BM_FIX_INT(g->player.x), 4);
  bmUiNumber(ui, 10, 3, BM_FIX_INT(g->player.y), 4);
#ifdef BM_PLATFORM_GBA
  bmUiNumber(ui, 23, 3, (int32_t)(lines * 100 / 228), 3);
#else
  (void)lines;
  bmUiLabel(ui, 23, 3, " --");
#endif
  bmUiPresent(ui);
}
