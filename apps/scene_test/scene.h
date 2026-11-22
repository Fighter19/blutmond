/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

#include <stdbool.h>

/// Builds the test scene: three backgrounds (4 and 8 bpp, flips, palette banks, priorities, a
/// 512-pixel-wide map), sprites of several sizes and depths, and optionally a wave on BG1
/// through a line table. Everything is generated in code. The scene is static, so every frame
/// after the first shows the same picture on every platform.
void sceneSetup(bool lineTable);

/// On top of the scene: windows and colour effects, with all three line tables (the wave on
/// slot 0, a darkening gradient on slot 1, the shape of window 1 on slot 2) and a
/// semi-transparent sprite.
void sceneEffects(void);
