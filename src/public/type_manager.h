#pragma once

#include "type.h"
#include "type_private.h"

// Bump up this value if you need more types
#define BM_TYPE_MANAGER_MAX_TYPES 16

typedef struct BmTypeManager
{
  BmTypePrivate types[BM_TYPE_MANAGER_MAX_TYPES];
  size_t typeCount;
} BmTypeManager;

void bmTypeManagerInitialize(BmTypeManager *manager) {(void)manager;}
void bmTypeManagerFinalize(BmTypeManager *manager) {(void)manager;}