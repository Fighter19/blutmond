#include "backdrop.h"
#include "errors.h"

#ifndef GBA
BmColor g_backdropColor = {0};
#endif

BmResult bmBackdropSetColor(BmColor color)
{
#ifdef GBA
  *(volatile BmColor*)0x05000000 = color;
#else
  g_backdropColor = color;
#endif
  return BM_SUCCESS;
}