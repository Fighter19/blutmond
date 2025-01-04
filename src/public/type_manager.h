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

typedef struct BmTypeCreateInfo
{
  const char *name;

  BmMemoryType memoryType;

  size_t size;

  void (*init)(void *data);
  void (*free)(void *data);
} BmTypeCreateInfo;

void bmTypeManagerInit(BmTypeManager *manager);
void bmTypeManagerFinalize(BmTypeManager *manager);

BmResult bmTypeManagerCreate(BmTypeManager *manager, const BmTypeCreateInfo *pCreateInfo, BmTypeHandle *pHandle);
BmTypePrivate *bmTypeManagerGetTypeFromHandle(BmTypeManager *manager, BmTypeHandle handle);

extern BmTypeManager *g_typeManager;