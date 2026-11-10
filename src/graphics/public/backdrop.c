#include "backdrop.h"
#include "hal.h"

BmResult bmBackdropSetColor(BmColor color)
{
  bmHalPaletteWrite(0, color.color);
  return BM_SUCCESS;
}

BmColor bmBackdropGetColor(void)
{
  return (BmColor){ bmHalPaletteRead(0) };
}
