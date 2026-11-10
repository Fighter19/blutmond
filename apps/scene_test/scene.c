/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "scene.h"

#include "video.h"

#include <string.h>

// 4 bpp tile from 8 rows of 8 colour indices given as hex digits.
static void makeTile4(uint8_t out[32], const char *rows[8])
{
  for (int y = 0; y < 8; y++)
  {
    for (int x = 0; x < 8; x += 2)
    {
      char a = rows[y][x], b = rows[y][x + 1];
      uint8_t lo = (uint8_t)(a <= '9' ? a - '0' : a - 'A' + 10);
      uint8_t hi = (uint8_t)(b <= '9' ? b - '0' : b - 'A' + 10);
      out[y * 4 + x / 2] = (uint8_t)(lo | (hi << 4));
    }
  }
}

// Asymmetric shapes, so flips are visible.
static const char *s_tileFlag[8] = {
  "11111111",
  "12222221",
  "12333221",
  "12300000",
  "12300000",
  "12333300",
  "12000000",
  "11000000",
};
static const char *s_tileChecker[8] = {
  "44445555",
  "44445555",
  "44445555",
  "44445555",
  "55554444",
  "55554444",
  "55554444",
  "55554444",
};
static const char *s_tileStripes[8] = {
  "60000006",
  "06000060",
  "00600600",
  "00066000",
  "00066000",
  "00600700",
  "06000070",
  "60000007",
};
static const char *s_tileSolid[8] = {
  "88888888", "88888888", "99999999", "99999999",
  "AAAAAAAA", "AAAAAAAA", "BBBBBBBB", "BBBBBBBB",
};

static void loadPalettes(void)
{
  BmColor bg[48];
  memset(bg, 0, sizeof(bg));
  bg[0] = bmColorRGB(2, 2, 6);  // backdrop
  // bank 0
  bg[1] = bmColorRGB(31, 31, 31);
  bg[2] = bmColorRGB(31, 8, 8);
  bg[3] = bmColorRGB(8, 8, 31);
  bg[4] = bmColorRGB(6, 20, 6);
  bg[5] = bmColorRGB(12, 28, 12);
  bg[6] = bmColorRGB(31, 31, 0);
  bg[7] = bmColorRGB(0, 31, 31);
  bg[8] = bmColorRGB(20, 4, 4);
  bg[9] = bmColorRGB(24, 8, 4);
  bg[10] = bmColorRGB(28, 14, 4);
  bg[11] = bmColorRGB(31, 22, 6);
  // bank 1: the same indices in other colours
  for (int i = 1; i < 16; i++)
  {
    BmColor c = bg[i];
    bg[16 + i] = (BmColor){ (uint16_t)(((c.color & 0x1F) << 10) | (c.color & 0x3E0) | (c.color >> 10)) };
  }
  // bank 2: greys
  for (int i = 1; i < 16; i++)
  {
    bg[32 + i] = bmColorRGB((uint8_t)(i * 2), (uint8_t)(i * 2), (uint8_t)(i * 2));
  }
  bmPaletteLoadBg(0, bg, 48);

  // Entries 64..127 for the 8 bpp background: a gradient.
  BmColor grad[64];
  for (int i = 0; i < 64; i++)
  {
    grad[i] = bmColorRGB((uint8_t)(i / 2), (uint8_t)(10 + i / 4), (uint8_t)(31 - i / 2));
  }
  bmPaletteLoadBg(64, grad, 64);

  BmColor obj[48];
  memset(obj, 0, sizeof(obj));
  obj[1] = bmColorRGB(31, 0, 0);
  obj[2] = bmColorRGB(31, 31, 31);
  obj[3] = bmColorRGB(0, 0, 0);
  obj[4] = bmColorRGB(31, 16, 0);
  for (int i = 1; i < 16; i++)
  {
    obj[16 + i] = bmColorRGB(0, (uint8_t)(i * 2), 31);
  }
  bmPaletteLoadObj(0, obj, 32);

  BmColor obj8[32];
  for (int i = 0; i < 32; i++)
  {
    obj8[i] = bmColorRGB((uint8_t)i, 31, (uint8_t)(31 - i));
  }
  bmPaletteLoadObj(128, obj8, 32);
}

static void loadBackgrounds(bool lineTable)
{
  // Char block 0: 4 bpp tiles. Tile 0 stays transparent.
  uint8_t tiles[5][32];
  memset(tiles[0], 0, 32);
  makeTile4(tiles[1], s_tileFlag);
  makeTile4(tiles[2], s_tileChecker);
  makeTile4(tiles[3], s_tileStripes);
  makeTile4(tiles[4], s_tileSolid);
  bmTilesLoadBg(0, 0, tiles, sizeof(tiles));

  // BG0: 256x256, front. Sparse flags in all flip combinations and three palette banks.
  static uint16_t map0[32 * 32] BM_EWRAM_BSS;
  for (int y = 0; y < 32; y++)
  {
    for (int x = 0; x < 32; x++)
    {
      uint16_t e = 0;
      if (((x + y) % 5) == 0)
      {
        e = bmMapEntry(1, x & 1, y & 1, (uint8_t)((x + y) % 3));
      }
      else if (y >= 16 && y < 18)
      {
        e = bmMapEntry(3, x & 2, 0, 0);
      }
      map0[y * 32 + x] = e;
    }
  }
  bmMapLoad(31, 0, map0, 32 * 32);
  bmBgSetup(0, &(BmBgInfo){ .charBlock = 0, .screenBlock = 31, .size = BM_BG_SIZE_256x256, .priority = 1 });
  bmBgSetScroll(0, 4, 2);

  // BG1: 512x256 (screen blocks 28 and 29), checkers and solid tiles, scrolled.
  static uint16_t map1[64 * 32] BM_EWRAM_BSS;
  for (int y = 0; y < 32; y++)
  {
    for (int x = 0; x < 64; x++)
    {
      uint16_t tile = (x / 4 + y / 4) % 2 ? 2 : 4;
      uint16_t e = bmMapEntry(tile, false, (x / 8) % 2, (uint8_t)(x >= 32 ? 1 : 0));
      // Screen block layout: left 32 columns in block 28, right 32 in block 29.
      int block = x / 32;
      map1[block * 1024 + y * 32 + (x % 32)] = e;
    }
  }
  bmMapLoad(28, 0, map1, 64 * 32);
  bmBgSetup(1, &(BmBgInfo){ .charBlock = 0, .screenBlock = 28, .size = BM_BG_SIZE_512x256, .priority = 2 });
  bmBgSetScroll(1, 200, 20);

  if (lineTable)
  {
    int16_t offsets[160];
    for (int y = 0; y < 160; y++)
    {
      int t = y % 32;
      int wave = t < 16 ? t : 31 - t;  // 0..15..0
      offsets[y] = (int16_t)(200 + wave - 8);
    }
    bmBgSetLineScrollX(1, offsets);
  }

  // BG2: 8 bpp, char block 2, colour gradient tiles, back.
  uint8_t tiles8[2][64];
  for (int i = 0; i < 64; i++)
  {
    tiles8[0][i] = (uint8_t)(64 + (i / 8) * 8 + (i % 8));      // gradient over the tile
    tiles8[1][i] = (uint8_t)(64 + 63 - ((i % 8) * 8 + i / 8)); // transposed and reversed
  }
  bmTilesLoadBg(2, 0, tiles8, sizeof(tiles8));
  static uint16_t map2[32 * 32] BM_EWRAM_BSS;
  for (int i = 0; i < 32 * 32; i++)
  {
    // 8 bpp tile numbers count 64-byte tiles.
    map2[i] = bmMapEntry((uint16_t)(((i / 32) + i) % 2), (i / 3) % 2, (i / 7) % 2, 0);
  }
  bmMapLoad(26, 0, map2, 32 * 32);
  bmBgSetup(2, &(BmBgInfo){ .charBlock = 2, .screenBlock = 26, .size = BM_BG_SIZE_256x256,
                            .colors256 = true, .priority = 3 });
}

static void loadSprites(void)
{
  // 16x16, 4 bpp, 1D mapping: 4 tiles. An arrow pointing right with a mark at the top.
  static const char *arrow[16] = {
    "2222222222222222",
    "2111111100000002",
    "2100000000000002",
    "2100000003000002",
    "2100000003300002",
    "2111111113330002",
    "2111111113333002",
    "2111111113333302",
    "2111111113333302",
    "2111111113333002",
    "2111111113330002",
    "2100000003300002",
    "2100000003000002",
    "2100000000000002",
    "2100000000000002",
    "2222222222222222",
  };
  uint8_t tiles[4][32];
  for (int ty = 0; ty < 2; ty++)
  {
    for (int tx = 0; tx < 2; tx++)
    {
      const char *rows[8];
      static char buf[4][8][9];
      for (int r = 0; r < 8; r++)
      {
        memcpy(buf[ty * 2 + tx][r], arrow[ty * 8 + r] + tx * 8, 8);
        buf[ty * 2 + tx][r][8] = 0;
        rows[r] = buf[ty * 2 + tx][r];
      }
      makeTile4(tiles[ty * 2 + tx], rows);
    }
  }
  bmTilesLoadObj(0, tiles, sizeof(tiles));

  // 32x16 (wide), 4 bpp, 8 tiles: stripes.
  uint8_t wide[8][32];
  for (int t = 0; t < 8; t++)
  {
    for (int i = 0; i < 32; i++)
    {
      wide[t][i] = (uint8_t)(((i / 4 + t) % 3 == 0) ? 0x44 : ((i % 4) == t % 4 ? 0x11 : 0x00));
    }
  }
  bmTilesLoadObj(4, wide, sizeof(wide));

  // 16x16, 8 bpp (4 tiles of 64 bytes = 8 units), indices 128..159.
  uint8_t deep[4][64];
  for (int t = 0; t < 4; t++)
  {
    for (int i = 0; i < 64; i++)
    {
      int x = (t % 2) * 8 + i % 8, y = (t / 2) * 8 + i / 8;
      deep[t][i] = (x + y) % 5 == 0 ? 0 : (uint8_t)(128 + (x + y));
    }
  }
  bmTilesLoadObj(12, deep, sizeof(deep));

  bmVideoSetSpriteMapping1D(true);
  const BmSprite base = { .shape = BM_SPRITE_SQUARE, .size = 1, .tile = 0, .visible = true };
  BmSprite s = base;

  s.x = 20; s.y = 30;                                        bmSpriteSet(0, &s);
  s.x = 44; s.y = 30; s.hflip = true;                        bmSpriteSet(1, &s);
  s.x = 68; s.y = 30; s.hflip = false; s.vflip = true;       bmSpriteSet(2, &s);
  s.x = 92; s.y = 30; s.hflip = true;  s.paletteBank = 1;    bmSpriteSet(3, &s);
  // Overlapping sprites: the lower index is in front.
  s = base; s.x = 120; s.y = 60;                             bmSpriteSet(4, &s);
  s = base; s.x = 128; s.y = 66; s.paletteBank = 1;          bmSpriteSet(5, &s);
  // Behind BG0 (priority 2 vs BG0's 1) but in front of BG1 (2, sprite wins ties).
  s = base; s.x = 20; s.y = 100; s.priority = 2;             bmSpriteSet(6, &s);
  // Partly off screen: left edge and bottom edge (y wraps around 256).
  s = base; s.x = -6; s.y = 70;                              bmSpriteSet(7, &s);
  s = base; s.x = 200; s.y = 152;                            bmSpriteSet(8, &s);
  s = base; s.x = 220; s.y = -6;                             bmSpriteSet(9, &s);
  // Wide sprite and 8 bpp sprite.
  s = base; s.shape = BM_SPRITE_WIDE; s.size = 1; s.tile = 4; s.x = 150; s.y = 110; bmSpriteSet(10, &s);
  s = base; s.colors256 = true; s.tile = 12; s.x = 180; s.y = 40; bmSpriteSet(11, &s);
  s.vflip = true; s.x = 200;                                 bmSpriteSet(12, &s);
}

void sceneSetup(bool lineTable)
{
  bmVideoInit();
  loadPalettes();
  loadBackgrounds(lineTable);
  loadSprites();
  bmVideoSetLayers(0x7, true);
}
