#include "ppu.h"

#include "test.h"

#include <string.h>

static BmPpuState s;
static uint16_t line[BM_PPU_WIDTH];

// A 4 bpp tile whose pixel (x, y) has colour index `pattern(x, y)`.
static void setTile4(uint32_t addr, uint8_t (*pattern)(int x, int y))
{
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x += 2)
      s.vram[addr + y * 4 + x / 2] = (uint8_t)(pattern(x, y) | (pattern(x + 1, y) << 4));
}

static uint8_t leftColumn(int x, int y) { (void)y; return x == 0 ? 1 : 0; }
static uint8_t solid2(int x, int y) { (void)x; (void)y; return 2; }
static uint8_t topRow3(int x, int y) { (void)x; return y == 0 ? 3 : 0; }

static void setUp(void)
{
  bmPpuReset(&s);
  s.palette[0] = 0x1111;   // backdrop
  s.palette[1] = 0x001F;   // red
  s.palette[2] = 0x03E0;   // green
  s.palette[3] = 0x7C00;   // blue
  s.palette[16 + 1] = 0x7FFF;
  s.palette[256 + 2] = 0x0210;
}

static void testForcedBlankIsWhite(void)
{
  setUp();
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_FORCED_BLANK);
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == 0x7FFF && line[239] == 0x7FFF);
}

static void testBackdropOnly(void)
{
  setUp();
  bmPpuRenderLine(&s, 5, line);
  TEST_ASSERT(line[0] == 0x1111 && line[100] == 0x1111);
}

static void testBgTileFlipAndPaletteBank(void)
{
  setUp();
  setTile4(32, leftColumn);  // tile 1 in char block 0
  // Screen block 8: entry (0,0) tile 1, entry (1,0) tile 1 hflip, entry (2,0) tile 1 bank 1.
  uint16_t *map = (uint16_t *)(void *)&s.vram[8 * 0x800];
  map[0] = 1;
  map[1] = 1 | 0x400;
  map[2] = 1 | (1 << 12);
  bmPpuSetReg(&s, BM_PPU_BGCNT(0), (uint16_t)(8 << 8));
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == 0x001F);    // left column of tile 1
  TEST_ASSERT(line[1] == 0x1111);    // transparent
  TEST_ASSERT(line[15] == 0x001F);   // flipped: right column
  TEST_ASSERT(line[8] == 0x1111);
  TEST_ASSERT(line[16] == 0x7FFF);   // palette bank 1, index 1
}

static void testScrollWrapsAround(void)
{
  setUp();
  setTile4(32, leftColumn);
  uint16_t *map = (uint16_t *)(void *)&s.vram[8 * 0x800];
  map[31] = 1;  // last column of a 256-wide map
  bmPpuSetReg(&s, BM_PPU_BGCNT(0), (uint16_t)(8 << 8));
  bmPpuSetReg(&s, BM_PPU_BGHOFS(0), 248);
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == 0x001F);
}

static void testWideMapUsesSecondScreenBlock(void)
{
  setUp();
  setTile4(32, leftColumn);
  uint16_t *map = (uint16_t *)(void *)&s.vram[8 * 0x800];
  map[1024] = 1;  // first entry of screen block 9 = column 32 of a 512-wide map
  bmPpuSetReg(&s, BM_PPU_BGCNT(0), (uint16_t)((8 << 8) | (1 << 14)));
  bmPpuSetReg(&s, BM_PPU_BGHOFS(0), 256);
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == 0x001F);
}

static void testPriorities(void)
{
  setUp();
  setTile4(32, solid2);     // tile 1: green
  setTile4(64, leftColumn); // tile 2: red left column
  uint16_t *map8 = (uint16_t *)(void *)&s.vram[8 * 0x800];
  uint16_t *map9 = (uint16_t *)(void *)&s.vram[9 * 0x800];
  map8[0] = 1;
  map9[0] = 2;
  // BG0 priority 2 (green), BG1 priority 1 (red column): BG1 in front where opaque.
  bmPpuSetReg(&s, BM_PPU_BGCNT(0), (uint16_t)((8 << 8) | 2));
  bmPpuSetReg(&s, BM_PPU_BGCNT(1), (uint16_t)((9 << 8) | 1));
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0) | BM_PPU_DISPCNT_BG(1));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == 0x001F);
  TEST_ASSERT(line[1] == 0x03E0);
  // Same priority: the lower-numbered background wins.
  bmPpuSetReg(&s, BM_PPU_BGCNT(1), (uint16_t)((9 << 8) | 2));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == 0x03E0);
}

static void testSpriteInFrontAndFlipped(void)
{
  setUp();
  setTile4(0x10000, topRow3);  // OBJ tile 0: top row colour 3 (palette entry 256+3)
  s.palette[256 + 3] = 0x4210;
  // Sprite 0: 8x8 at (10, 20); sprite 1 at (100, 20) with vflip.
  s.oam[0] = 20; s.oam[1] = 10; s.oam[2] = 0;
  s.oam[4] = 20; s.oam[5] = (uint16_t)(100 | 0x2000); s.oam[6] = 0;
  for (int i = 2; i < 128; i++) s.oam[i * 4] = 0x0200;
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_OBJ | BM_PPU_DISPCNT_OBJ_1D);
  bmPpuRenderLine(&s, 20, line);
  TEST_ASSERT(line[10] == 0x4210 && line[17] == 0x4210 && line[18] == 0x1111);
  TEST_ASSERT(line[100] == 0x1111);       // vflip: top row moved to the bottom
  bmPpuRenderLine(&s, 27, line);
  TEST_ASSERT(line[100] == 0x4210);
  TEST_ASSERT(line[10] == 0x1111);
}

static void testSpriteWrapsAtScreenEdges(void)
{
  setUp();
  setTile4(0x10000, solid2);
  s.palette[256 + 2] = 0x0210;
  s.oam[0] = 252;               // y = -4
  s.oam[1] = (uint16_t)(0x1FC); // x = -4
  s.oam[2] = 0;
  for (int i = 1; i < 128; i++) s.oam[i * 4] = 0x0200;
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_OBJ);
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == 0x0210 && line[3] == 0x0210 && line[4] == 0x1111);
  bmPpuRenderLine(&s, 4, line);
  TEST_ASSERT(line[0] == 0x1111);
}

static void testLineTableChangesScrollPerLine(void)
{
  setUp();
  setTile4(32, leftColumn);
  uint16_t *map = (uint16_t *)(void *)&s.vram[8 * 0x800];
  for (int i = 0; i < 32 * 32; i++) map[i] = 1;
  bmPpuSetReg(&s, BM_PPU_BGCNT(0), (uint16_t)(8 << 8));
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0));
  s.lineTables[0].reg = BM_PPU_BGHOFS(0);
  for (int y = 0; y < BM_PPU_HEIGHT; y++) s.lineTables[0].values[y] = (uint16_t)(y % 8);
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == 0x001F);
  bmPpuRenderLine(&s, 3, line);
  TEST_ASSERT(line[0] == 0x1111 && line[5] == 0x001F);
}

static void testSnapshotRoundTrip(void)
{
  setUp();
  s.regs[3] = 0xBEEF;
  s.vram[0x17FFF] = 0x5A;
  s.oam[511] = 0x1234;
  s.lineTables[0].reg = BM_PPU_BGVOFS(2);
  s.lineTables[0].values[159] = 77;
  static uint8_t buffer[200 * 1024];
  TEST_ASSERT(bmPpuSnapshotSize() <= sizeof(buffer));
  bmPpuSnapshotWrite(&s, buffer);
  static BmPpuState copy;
  TEST_ASSERT(bmPpuSnapshotRead(&copy, buffer, bmPpuSnapshotSize()));
  TEST_ASSERT(memcmp(&copy, &s, sizeof(s)) == 0);
  buffer[0] = 'X';
  TEST_ASSERT(!bmPpuSnapshotRead(&copy, buffer, bmPpuSnapshotSize()));
}

int main(void)
{
  RUN_TEST(testForcedBlankIsWhite);
  RUN_TEST(testBackdropOnly);
  RUN_TEST(testBgTileFlipAndPaletteBank);
  RUN_TEST(testScrollWrapsAround);
  RUN_TEST(testWideMapUsesSecondScreenBlock);
  RUN_TEST(testPriorities);
  RUN_TEST(testSpriteInFrontAndFlipped);
  RUN_TEST(testSpriteWrapsAtScreenEdges);
  RUN_TEST(testLineTableChangesScrollPerLine);
  RUN_TEST(testSnapshotRoundTrip);
  return TEST_RESULT();
}
