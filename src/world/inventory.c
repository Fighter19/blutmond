/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "inventory.h"

#include "script_api.h"

static int32_t find(const BmInventory *pInv, uint32_t key)
{
  for (uint32_t i = 0; i < pInv->count && i < BM_INVENTORY_SLOTS; i++)
    if (pInv->key[i] == key) return (int32_t)i;
  return -1;
}

uint32_t bmInventoryCount(const BmInventory *pInv, uint32_t key)
{
  int32_t i = key != 0 ? find(pInv, key) : -1;
  return i >= 0 ? pInv->amount[i] : 0;
}

uint32_t bmInventoryGive(BmInventory *pInv, uint32_t key, uint32_t amount)
{
  if (key == 0 || amount == 0) return 0;
  int32_t i = find(pInv, key);
  if (i < 0)
  {
    if (pInv->count >= BM_INVENTORY_SLOTS) return 0;
    i = pInv->count++;
    pInv->key[i] = key;
    pInv->amount[i] = 0;
  }
  uint32_t room = BM_INVENTORY_MAX_AMOUNT - pInv->amount[i];
  uint32_t added = amount < room ? amount : room;
  pInv->amount[i] = (uint8_t)(pInv->amount[i] + added);
  return added;
}

bool bmInventoryTake(BmInventory *pInv, uint32_t key, uint32_t amount)
{
  int32_t i = key != 0 ? find(pInv, key) : -1;
  if (i < 0 || pInv->amount[i] < amount) return amount == 0;
  pInv->amount[i] = (uint8_t)(pInv->amount[i] - amount);
  if (pInv->amount[i] == 0)
  {
    for (uint32_t k = (uint32_t)i; k + 1 < pInv->count; k++)
    {
      pInv->key[k] = pInv->key[k + 1];
      pInv->amount[k] = pInv->amount[k + 1];
    }
    pInv->count--;
    pInv->key[pInv->count] = 0;
    pInv->amount[pInv->count] = 0;
  }
  return true;
}

bool bmInventoryScriptCall(BmInventory *pInv, uint16_t api, const int32_t *pArgs, int32_t *pResult)
{
  switch (api)
  {
  case BM_SCRIPT_API_item_count:
    *pResult = (int32_t)bmInventoryCount(pInv, (uint32_t)pArgs[0]);
    return true;
  case BM_SCRIPT_API_give:
    *pResult = pArgs[1] > 0 ? (int32_t)bmInventoryGive(pInv, (uint32_t)pArgs[0], (uint32_t)pArgs[1]) : 0;
    return true;
  case BM_SCRIPT_API_take:
    *pResult = pArgs[1] >= 0 && bmInventoryTake(pInv, (uint32_t)pArgs[0], (uint32_t)pArgs[1]);
    return true;
  case BM_SCRIPT_API_stat:
    *pResult = pArgs[0] >= 0 && pArgs[0] < BM_INVENTORY_STATS ? bmInventoryStat(pInv, (uint32_t)pArgs[0]) : 0;
    return true;
  case BM_SCRIPT_API_add_stat:
    if (pArgs[0] >= 0 && pArgs[0] < BM_INVENTORY_STATS)
    {
      int32_t v = pInv->statBase[pArgs[0]] + pArgs[1];
      pInv->statBase[pArgs[0]] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
    return true;
  default:
    return false;
  }
}

int32_t bmInventoryStat(const BmInventory *pInv, uint32_t stat)
{
  return stat < BM_INVENTORY_STATS ? (int32_t)pInv->statBase[stat] + pInv->statBonus[stat] : 0;
}
