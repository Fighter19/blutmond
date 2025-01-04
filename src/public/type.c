#include "type.h"
#include "type_private.h"

#include "type_manager.h"

void bmTypeInitializeForType(BmTypeHandle type, void *data)
{
  BmTypePrivate *type_priv = bmTypeManagerGetTypeFromHandle(g_typeManager, type);
  if (type_priv->init != NULL)
    type_priv->init(data);
}