#include "type.h"
#include "type_private.h"

void bmTypeInitializeMemory(BmTypeHandle type, void *data)
{
  type->init(data);
}