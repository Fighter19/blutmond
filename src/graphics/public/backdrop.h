#pragma once

#include "color.h"

/// Sets the backdrop colour: the colour shown wherever no layer or sprite draws
/// (BG palette entry 0).
BmResult bmBackdropSetColor(BmColor color);

BmColor bmBackdropGetColor(void);
