/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
// bmschema: the save game's layout as this runtime writes it, for the project's schema history
// (docs/spec/state.md §7). The cooker turns the history into a MIGR chunk, and the runtime
// loads a save game of any layout in it.
//
//   bmschema                       prints the layout
//   bmschema --add file [label]    appends it to the history file unless it is there already
//
// Only the state API is used, so the same file builds against older versions of the engine
// (that is how the layouts of earlier releases were taken).

#include "game.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BmGame s_game;

static const char *typeName(uint8_t t)
{
  static const char *const names[] = { "u8", "s8", "u16", "s16", "u32", "s32", "bool", "fix" };
  return t < 8 ? names[t] : "?";
}

static void print(FILE *out, const BmStateBlock *b, uint32_t hash, const char *label)
{
  fprintf(out, "schema 0x%08x \"%s\"\n", (unsigned)hash, label);
  for (uint16_t i = 0; i < b->fieldCount; i++)
  {
    const BmStateField *f = &b->pFields[i];
    // What a save game holds: the fields with the flag save.
    if (!(f->flags & BM_STATE_SAVE) || (f->flags & (BM_STATE_TRANSIENT | BM_STATE_DERIVED))) continue;
    fprintf(out, " %s.%s %s %u\n", b->name, f->name, typeName(f->type), (unsigned)f->count);
  }
  fprintf(out, "end\n");
}

int main(int argc, char **argv)
{
  BmStateBlock b = bmGameStateBlock(&s_game);
  uint32_t hash = bmStateSchemaHash(&b, 1);
  if (argc >= 3 && strcmp(argv[1], "--add") == 0)
  {
    char needle[32];
    snprintf(needle, sizeof(needle), "schema 0x%08x", (unsigned)hash);
    FILE *f = fopen(argv[2], "rb");
    if (f != NULL)
    {
      char line[512];
      while (fgets(line, sizeof(line), f))
        if (strncmp(line, needle, strlen(needle)) == 0)
        {
          fclose(f);
          printf("%s is in %s already\n", needle, argv[2]);
          return 0;
        }
      fclose(f);
    }
    f = fopen(argv[2], "ab");
    if (f == NULL)
    {
      fprintf(stderr, "bmschema: cannot write %s\n", argv[2]);
      return 1;
    }
    print(f, &b, hash, argc >= 4 ? argv[3] : "");
    fclose(f);
    printf("added %s to %s\n", needle, argv[2]);
    return 0;
  }
  print(stdout, &b, hash, argc >= 2 ? argv[1] : "");
  return 0;
}
