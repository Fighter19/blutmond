#pragma once

#include "shared.h"

#ifdef BM_TYPE_USE_INT_HANDLE
typedef uintptr_t BmTypeHandle;
#else
typedef struct BmTypePrivate *BmTypeHandle;
#endif

void bmTypeInitializeForType(BmTypeHandle type, void *data);