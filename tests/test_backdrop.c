#include "backdrop.h"
#include "hal.h"
#include "hal_host.h"

#include "test.h"

static void testBackdropGoesToPaletteEntryZero(void)
{
  bmHalHostReset();
  bmHalVideoInit();
  TEST_ASSERT(bmBackdropSetColor(bmColorRGB(31, 0, 8)) == BM_SUCCESS);
  const BmPpuState *ppu = bmHalHostGetPpu();
  TEST_ASSERT((bmPpuReg(ppu, BM_PPU_DISPCNT) & BM_PPU_DISPCNT_FORCED_BLANK) == 0);
  TEST_ASSERT(ppu->palette[0] == (31 | (8 << 10)));
  TEST_ASSERT(bmBackdropGetColor().color == ppu->palette[0]);

  static uint16_t pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT];
  bmHalHostRender(pixels);
  TEST_ASSERT(pixels[0] == ppu->palette[0] && pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT - 1] == ppu->palette[0]);
}

static void testColorPacking(void)
{
  TEST_ASSERT(bmColorRGB(31, 31, 31).color == 0x7FFF);
  TEST_ASSERT(bmColorRGB(0, 31, 0).color == (31 << 5));
  TEST_ASSERT(bmColorRGB(255, 0, 0).color == 31);  // components are 5 bit
}

int main(void)
{
  RUN_TEST(testBackdropGoesToPaletteEntryZero);
  RUN_TEST(testColorPacking);
  return TEST_RESULT();
}
