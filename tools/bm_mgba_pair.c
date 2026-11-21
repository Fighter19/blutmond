/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */

// Two GBA images in mGBA, joined by a link cable (GBA to GBA, docs/spec/link-protocol.md §9):
//
//   bm_mgba_pair <a.gba> <b.gba> [--frames N] [--until a|b <elf>:<symbol>[+offset]=<value>]
//                [--check a|b <elf>:<symbol>[+offset][=|<=]<value>]... [--ppm a|b <file>]
//
// Runs both for N frames, or until the value is reached (then one frame more), and then
// checks values in their memory. `--check` without a value prints it.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmlink/mgba_link.h"

#define FRAME_CYCLES 280896u
#define MAX_CHECKS 32

int main(int argc, char **argv)
{
  if (argc < 3)
  {
    fprintf(stderr, "usage: bm_mgba_pair <a.gba> <b.gba> [--frames N] [--until a|b spec] [--check a|b spec]... [--ppm a|b file]\n");
    return 2;
  }
  uint32_t frames = 600;
  const char *untilSide = NULL, *untilSpec = NULL;
  const char *checkSide[MAX_CHECKS], *checkSpec[MAX_CHECKS], *ppmSide[2], *ppmPath[2];
  int checks = 0, ppms = 0;
  for (int i = 3; i < argc; i++)
  {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--until") == 0 && i + 2 < argc) { untilSide = argv[i + 1]; untilSpec = argv[i + 2]; i += 2; }
    else if (strcmp(argv[i], "--check") == 0 && i + 2 < argc && checks < MAX_CHECKS) { checkSide[checks] = argv[i + 1]; checkSpec[checks++] = argv[i + 2]; i += 2; }
    else if (strcmp(argv[i], "--ppm") == 0 && i + 2 < argc && ppms < 2) { ppmSide[ppms] = argv[i + 1]; ppmPath[ppms++] = argv[i + 2]; i += 2; }
    else
    {
      fprintf(stderr, "unknown option %s\n", argv[i]);
      return 2;
    }
  }
  BmMgbaLink *a = bmMgbaLinkOpen(argv[1]), *b = bmMgbaLinkOpen(argv[2]);
  if (a == NULL || b == NULL) return 1;
#define SIDE(s) ((s)[0] == 'b' ? b : a)

  BmMgbaPairStats stats = { 0, 0 };
  uint32_t frame = 0;
  bool reached = untilSpec == NULL;
  for (; frame < frames; frame++)
  {
    bmMgbaPairRun(a, b, FRAME_CYCLES, &stats);
    if (untilSpec != NULL && bmMgbaLinkReached(SIDE(untilSide), untilSpec))
    {
      bmMgbaPairRun(a, b, FRAME_CYCLES, &stats);
      reached = true;
      frame += 2;
      break;
    }
  }
  printf("%u frames, %llu words across the cable, %llu lost\n", (unsigned)frame, (unsigned long long)stats.words, (unsigned long long)stats.lost);
  int failed = 0;
  if (!reached)
  {
    fprintf(stderr, "%s was not reached in %u frames\n", untilSpec, (unsigned)frames);
    failed = 1;
  }
  for (int i = 0; i < checks; i++)
  {
    printf("%c: ", checkSide[i][0] == 'b' ? 'b' : 'a');
    if (!bmMgbaLinkCheck(SIDE(checkSide[i]), checkSpec[i])) failed = 1;
  }
  for (int i = 0; i < ppms; i++)
  {
    if (!bmMgbaLinkScreenshot(SIDE(ppmSide[i]), ppmPath[i])) failed = 1;
  }
  bmMgbaLinkClose(a);
  bmMgbaLinkClose(b);
  return failed;
}
