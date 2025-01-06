#pragma once

#include "memory.h"

typedef struct BmTypePrivate
{
  const char *name;

  size_t size;

  void (*init)(void *data);
  void (*free)(void *data);
} BmTypePrivate;