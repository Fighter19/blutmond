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

// A full-screen BG0 of tile 1 (solid green, priority 1) and BG1 of tile 1 in bank 1 (index 2
// of bank 1, priority 2), for the window and blending tests.
static void twoLayers(void)
{
  setUp();
  setTile4(32, solid2);
  s.palette[16 + 2] = 0x7C00;   // bank 1, index 2: blue
  uint16_t *map8 = (uint16_t *)(void *)&s.vram[8 * 0x800];
  uint16_t *map9 = (uint16_t *)(void *)&s.vram[9 * 0x800];
  for (int i = 0; i < 32 * 32; i++)
  {
    map8[i] = 1;
    map9[i] = 1 | (1 << 12);
  }
  bmPpuSetReg(&s, BM_PPU_BGCNT(0), (uint16_t)((8 << 8) | 1));
  bmPpuSetReg(&s, BM_PPU_BGCNT(1), (uint16_t)((9 << 8) | 2));
  for (int i = 0; i < 128; i++) s.oam[i * 4] = 0x0200;
}

static void testWindowsChooseLayers(void)
{
  twoLayers();
  // Window 0: x 10..20, lines 5..9, only BG1. Window 1 (behind it): x 15..30, all lines, nothing.
  // Outside: both.
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0) | BM_PPU_DISPCNT_BG(1) | BM_PPU_DISPCNT_WIN0 | BM_PPU_DISPCNT_WIN1);
  bmPpuSetReg(&s, BM_PPU_WIN0H, (10 << 8) | 20);
  bmPpuSetReg(&s, BM_PPU_WIN0V, (5 << 8) | 10);
  bmPpuSetReg(&s, BM_PPU_WIN1H, (15 << 8) | 30);
  bmPpuSetReg(&s, BM_PPU_WIN1V, (0 << 8) | 160);
  bmPpuSetReg(&s, BM_PPU_WININ, BM_PPU_WIN_BG(1) | (0 << 8));
  bmPpuSetReg(&s, BM_PPU_WINOUT, BM_PPU_WIN_BG(0) | BM_PPU_WIN_BG(1));
  bmPpuRenderLine(&s, 5, line);
  TEST_ASSERT(line[9] == 0x03E0 && line[10] == 0x7C00 && line[19] == 0x7C00);   // BG0 outside, BG1 in window 0
  TEST_ASSERT(line[20] == 0x1111 && line[29] == 0x1111 && line[30] == 0x03E0);  // window 1: the backdrop
  bmPpuRenderLine(&s, 10, line);
  TEST_ASSERT(line[10] == 0x03E0 && line[15] == 0x1111);                        // below window 0: window 1 at 15
  // A window whose left is right of its right wraps around: 230..240 and 0..5.
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0) | BM_PPU_DISPCNT_BG(1) | BM_PPU_DISPCNT_WIN0);
  bmPpuSetReg(&s, BM_PPU_WIN0H, (230 << 8) | 5);
  bmPpuRenderLine(&s, 6, line);
  TEST_ASSERT(line[4] == 0x7C00 && line[5] == 0x03E0 && line[229] == 0x03E0 && line[230] == 0x7C00 && line[239] == 0x7C00);
  // A right edge past the screen counts as its edge.
  bmPpuSetReg(&s, BM_PPU_WIN0H, (200 << 8) | 250);
  bmPpuRenderLine(&s, 6, line);
  TEST_ASSERT(line[199] == 0x03E0 && line[200] == 0x7C00 && line[239] == 0x7C00 && line[0] == 0x03E0);
}

static void testObjWindow(void)
{
  twoLayers();
  // An 8x8 sprite in OBJ window mode at (40, 0) whose left column is set: there only BG1 shows,
  // and the sprite itself is not drawn.
  setTile4(0x10000, leftColumn);
  s.palette[256 + 1] = 0x7FFF;
  s.oam[0] = (uint16_t)(0 | (2 << 10));
  s.oam[1] = 40;
  s.oam[2] = 0;
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0) | BM_PPU_DISPCNT_BG(1) | BM_PPU_DISPCNT_OBJ | BM_PPU_DISPCNT_OBJWIN);
  bmPpuSetReg(&s, BM_PPU_WINOUT, (BM_PPU_WIN_BG(0) | BM_PPU_WIN_BG(1) | BM_PPU_WIN_OBJ) | (BM_PPU_WIN_BG(1) << 8));
  bmPpuRenderLine(&s, 3, line);
  TEST_ASSERT(line[39] == 0x03E0 && line[40] == 0x7C00 && line[41] == 0x03E0);
  // Window 0 wins over the OBJ window.
  bmPpuSetReg(&s, BM_PPU_DISPCNT, bmPpuReg(&s, BM_PPU_DISPCNT) | BM_PPU_DISPCNT_WIN0);
  bmPpuSetReg(&s, BM_PPU_WIN0H, (0 << 8) | 100);
  bmPpuSetReg(&s, BM_PPU_WIN0V, (0 << 8) | 160);
  bmPpuSetReg(&s, BM_PPU_WININ, 0);
  bmPpuRenderLine(&s, 3, line);
  TEST_ASSERT(line[40] == 0x1111);
}

static void testBlending(void)
{
  twoLayers();
  s.palette[2] = (uint16_t)(20 << 5);         // BG0: green 20
  s.palette[16 + 2] = (uint16_t)(10 | 30 << 10);   // BG1: red 10, blue 30
  uint16_t layers = BM_PPU_DISPCNT_BG(0) | BM_PPU_DISPCNT_BG(1);
  bmPpuSetReg(&s, BM_PPU_DISPCNT, layers);
  // Alpha: BG0 over BG1, 12/16 and 8/16, each channel at most 31.
  bmPpuSetReg(&s, BM_PPU_BLDCNT, BM_PPU_WIN_BG(0) | BM_PPU_BLD_ALPHA | (BM_PPU_WIN_BG(1) << 8));
  bmPpuSetReg(&s, BM_PPU_BLDALPHA, 12 | (8 << 8));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == (uint16_t)(5 | (15 << 5) | (15 << 10)));   // (0*12+10*8)/16, (20*12)/16, (30*8)/16
  // Weights above 16 count as 16: 20 + 0 green, 10 red, 30 blue -> min(31, ...).
  bmPpuSetReg(&s, BM_PPU_BLDALPHA, 31 | (31 << 8));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == (uint16_t)(10 | (20 << 5) | (30 << 10)));
  // No second target right behind: no blending.
  bmPpuSetReg(&s, BM_PPU_BLDCNT, BM_PPU_WIN_BG(0) | BM_PPU_BLD_ALPHA | (BM_PPU_BLD_BACKDROP << 8));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == (uint16_t)(20 << 5));
  // Brighten by 8/16: green 20 + (11 * 8) / 16 = 25, the others 0 + 31 * 8 / 16 = 15.
  bmPpuSetReg(&s, BM_PPU_BLDCNT, BM_PPU_WIN_BG(0) | BM_PPU_BLD_BRIGHTEN);
  bmPpuSetReg(&s, BM_PPU_BLDY, 8);
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == (uint16_t)(15 | (25 << 5) | (15 << 10)));
  // Darken by 4/16: 20 - 20 * 4 / 16 = 15. Only first targets: BG1 shown alone stays as it is.
  bmPpuSetReg(&s, BM_PPU_BLDCNT, BM_PPU_WIN_BG(0) | BM_PPU_BLD_DARKEN);
  bmPpuSetReg(&s, BM_PPU_BLDY, 4);
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == (uint16_t)(15 << 5));
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(1));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == (uint16_t)(10 | 30 << 10));
  // A window without effects: none there.
  bmPpuSetReg(&s, BM_PPU_DISPCNT, layers | BM_PPU_DISPCNT_WIN0);
  bmPpuSetReg(&s, BM_PPU_WIN0H, (0 << 8) | 10);
  bmPpuSetReg(&s, BM_PPU_WIN0V, (0 << 8) | 160);
  bmPpuSetReg(&s, BM_PPU_WININ, BM_PPU_WIN_BG(0) | BM_PPU_WIN_BG(1));
  bmPpuSetReg(&s, BM_PPU_WINOUT, BM_PPU_WIN_BG(0) | BM_PPU_WIN_BG(1) | BM_PPU_WIN_EFFECTS);
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == (uint16_t)(20 << 5) && line[10] == (uint16_t)(15 << 5));
}

static void testSemiTransparentSprite(void)
{
  twoLayers();
  s.palette[16 + 2] = (uint16_t)(10 | 30 << 10);
  setTile4(0x10000, solid2);
  s.palette[256 + 2] = 0x7FFF;
  // An 8x8 semi-transparent sprite at (0, 0) in front of BG1 (BG0 off). BLDCNT names no first
  // target and no effect: it blends all the same where BG1 is a second target.
  s.oam[0] = (uint16_t)(1 << 10);
  s.oam[1] = 0;
  s.oam[2] = 0;
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(1) | BM_PPU_DISPCNT_OBJ);
  bmPpuSetReg(&s, BM_PPU_BLDCNT, BM_PPU_WIN_BG(1) << 8);
  bmPpuSetReg(&s, BM_PPU_BLDALPHA, 8 | (8 << 8));
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == (uint16_t)(20 | 15 << 5 | 30 << 10));   // (31 + 10) / 2, 31 / 2, (31 + 30) / 2
  TEST_ASSERT(line[8] == (uint16_t)(10 | 30 << 10));
  // Without a second target behind it, it is a first target of the effect: darkened by 8/16.
  bmPpuSetReg(&s, BM_PPU_BLDCNT, BM_PPU_BLD_DARKEN);
  bmPpuSetReg(&s, BM_PPU_BLDY, 8);
  bmPpuRenderLine(&s, 0, line);
  TEST_ASSERT(line[0] == (uint16_t)(16 | 16 << 5 | 16 << 10));
}

static void testThreeLineTables(void)
{
  twoLayers();
  // Slot 0: BG0 scrolled, slot 1: BLDY, slot 2: WIN0H; all at once, each line its own.
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_BG(0) | BM_PPU_DISPCNT_WIN0);
  bmPpuSetReg(&s, BM_PPU_WIN0V, 160);
  bmPpuSetReg(&s, BM_PPU_WININ, BM_PPU_WIN_BG(0) | BM_PPU_WIN_EFFECTS);
  bmPpuSetReg(&s, BM_PPU_BLDCNT, BM_PPU_WIN_BG(0) | BM_PPU_BLD_DARKEN);
  s.palette[2] = (uint16_t)(16 << 5);
  s.lineTables[0].reg = BM_PPU_BGHOFS(0);
  s.lineTables[1].reg = BM_PPU_BLDY;
  s.lineTables[2].reg = BM_PPU_WIN0H;
  for (int y = 0; y < BM_PPU_HEIGHT; y++)
  {
    s.lineTables[0].values[y] = (uint16_t)y;
    s.lineTables[1].values[y] = (uint16_t)(y % 17);
    s.lineTables[2].values[y] = (uint16_t)(y < 100 ? y : 100);
  }
  bmPpuRenderLine(&s, 8, line);
  TEST_ASSERT(line[0] == (uint16_t)(8 << 5) && line[7] == (uint16_t)(8 << 5) && line[8] == 0x1111);   // 16 - 16 * 8 / 16
  bmPpuRenderLine(&s, 16, line);
  TEST_ASSERT(line[0] == 0 && line[15] == 0 && line[16] == 0x1111);
}

static void testSnapshotRoundTrip(void)
{
  setUp();
  s.regs[3] = 0xBEEF;
  s.vram[0x17FFF] = 0x5A;
  s.oam[511] = 0x1234;
  s.lineTables[0].reg = BM_PPU_BGVOFS(2);
  s.lineTables[0].values[159] = 77;
  s.lineTables[2].reg = BM_PPU_BLDY;
  s.lineTables[2].values[3] = 9;
  static uint8_t buffer[200 * 1024];
  TEST_ASSERT(bmPpuSnapshotSize() <= sizeof(buffer));
  bmPpuSnapshotWrite(&s, buffer);
  static BmPpuState copy;
  TEST_ASSERT(bmPpuSnapshotRead(&copy, buffer, bmPpuSnapshotSize()));
  TEST_ASSERT(memcmp(&copy, &s, sizeof(s)) == 0);
  buffer[0] = 'X';
  TEST_ASSERT(!bmPpuSnapshotRead(&copy, buffer, bmPpuSnapshotSize()));
}

// Affine sprites (double size): an 8x8 sprite whose left column is coloured, in a 16x16 box
// at (10, 20), so its centre is (18, 28).
static void setAffine(int16_t pa, int16_t pb, int16_t pc, int16_t pd)
{
  setUp();
  setTile4(0x10000, leftColumn);
  s.palette[256 + 1] = 0x0123;
  for (int i = 0; i < 128; i++) s.oam[i * 4] = 0x0200;
  s.oam[0] = (uint16_t)(20 | 0x0300);   // affine, double size
  s.oam[1] = 10;                        // matrix 0
  s.oam[2] = 0;
  s.oam[3] = (uint16_t)pa; s.oam[7] = (uint16_t)pb; s.oam[11] = (uint16_t)pc; s.oam[15] = (uint16_t)pd;
  bmPpuSetReg(&s, BM_PPU_DISPCNT, BM_PPU_DISPCNT_OBJ | BM_PPU_DISPCNT_OBJ_1D);
}

static void testAffineSpriteIdentity(void)
{
  // Where the regular sprite would be: x 14..21, lines 24..31.
  setAffine(256, 0, 0, 256);
  bmPpuRenderLine(&s, 24, line);
  TEST_ASSERT(line[14] == 0x0123 && line[13] == 0x1111 && line[15] == 0x1111);
  bmPpuRenderLine(&s, 31, line);
  TEST_ASSERT(line[14] == 0x0123);
  bmPpuRenderLine(&s, 23, line);
  TEST_ASSERT(line[14] == 0x1111);
  bmPpuRenderLine(&s, 32, line);
  TEST_ASSERT(line[14] == 0x1111);
}

static void testAffineSpriteScaleAndShear(void)
{
  // Twice as wide: the column is two pixels, from the box's left (x 10, 11).
  setAffine(128, 0, 0, 256);
  bmPpuRenderLine(&s, 28, line);
  TEST_ASSERT(line[10] == 0x0123 && line[11] == 0x0123 && line[12] == 0x1111);
  // Sheared (texture y = dx + dy): the left column moves down by 4 lines.
  setAffine(256, 0, 256, 256);
  bmPpuRenderLine(&s, 24, line);
  TEST_ASSERT(line[14] == 0x1111);
  bmPpuRenderLine(&s, 28, line);
  TEST_ASSERT(line[14] == 0x0123);
  bmPpuRenderLine(&s, 35, line);
  TEST_ASSERT(line[14] == 0x0123);
  bmPpuRenderLine(&s, 36, line);
  TEST_ASSERT(line[14] == 0x1111);
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
  RUN_TEST(testAffineSpriteIdentity);
  RUN_TEST(testAffineSpriteScaleAndShear);
  RUN_TEST(testLineTableChangesScrollPerLine);
  RUN_TEST(testWindowsChooseLayers);
  RUN_TEST(testObjWindow);
  RUN_TEST(testBlending);
  RUN_TEST(testSemiTransparentSprite);
  RUN_TEST(testThreeLineTables);
  RUN_TEST(testSnapshotRoundTrip);
  return TEST_RESULT();
}
