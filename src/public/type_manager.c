#include "shared.h"
#include "type_manager.h"
#include "errors.h"

BmTypeManager *g_typeManager = NULL;

void bmTypeManagerInit(BmTypeManager *manager)
{
  g_typeManager = manager;
}

void bmTypeManagerFinalize(BmTypeManager *manager)
{
  (void)manager;
  g_typeManager = NULL;
}

BmResult bmTypeManagerCreate(BmTypeManager *manager, const BmTypeCreateInfo *pCreateInfo, BmTypeHandle *pHandle)
{
  if (manager->typeCount >= BM_TYPE_MANAGER_MAX_TYPES)
  {
    return BM_ERROR_OUT_OF_MEMORY;
  }

  BmTypePrivate *type = &manager->types[manager->typeCount];
  type->name = pCreateInfo->name;
  type->size = pCreateInfo->size;
  type->init = pCreateInfo->init;
  type->free = pCreateInfo->free;

#ifdef BM_TYPE_USE_INT_HANDLE
  *pHandle = manager->typeCount;
#else
  *pHandle = (BmTypeHandle)type;
#endif

  manager->typeCount++;

  return BM_SUCCESS;
}

BmTypePrivate *bmTypeManagerGetTypeFromHandle(BmTypeManager *manager, BmTypeHandle handle)
{
  // In case we ever want to use integers for types, for example
  // because we want to safe memory, consider changing the implementation
#ifdef BM_TYPE_USE_INT_HANDLE
  return (BmTypePrivate*)&manager->types[handle];
#else
  (void)manager;
  return (BmTypePrivate*)handle;
#endif
}
