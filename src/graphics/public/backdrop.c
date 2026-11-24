#include "backdrop.h"
#include "video.h"
#include "hal.h"

BmResult bmBackdropSetColor(BmColor color)
{
  bmPaletteWrite(0, &color.color, 1);
  return BM_SUCCESS;
}

BmColor bmBackdropGetColor(void)
{
  return (BmColor){ bmHalPaletteRead(0) };   // as shown (corrected, if the correction is on)
}
