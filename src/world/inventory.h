/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// The player's inventory (docs/plans/structures.md §7): what they carry, by record key (the
// FNV-1a hash of the record's or item's name), with an amount; equipment and quick slots;
// the player's own stat values. All of it goes into the save game and over the link.

#include <stdbool.h>
#include <stdint.h>

#define BM_INVENTORY_SLOTS 64
#define BM_INVENTORY_EQUIP 8
#define BM_INVENTORY_QUICK 4
#define BM_INVENTORY_STATS 16
#define BM_INVENTORY_MAX_AMOUNT 255

typedef struct BmInventory
{
  uint8_t count;                            // used entries, in the order they were first given
  uint32_t key[BM_INVENTORY_SLOTS];
  uint8_t amount[BM_INVENTORY_SLOTS];       // 1 to 255
  uint32_t equipped[BM_INVENTORY_EQUIP];    // a record per slot, 0 none (structures.md step 4)
  uint32_t quick[BM_INVENTORY_QUICK];       // records on the quick slots, 0 none
  int16_t statBase[BM_INVENTORY_STATS];     // the player's own values (level-ups, upgrades)
} BmInventory;

/// How many of `key` the player has (0 if none).
uint32_t bmInventoryCount(const BmInventory *pInv, uint32_t key);

/// Adds `amount`, at most up to 255 of one kind; a new kind needs a free entry. Returns how
/// many were added (0 if the inventory is full or key is 0).
uint32_t bmInventoryGive(BmInventory *pInv, uint32_t key, uint32_t amount);

/// Takes `amount` away if the player has at least that many; an entry that reaches 0 is
/// removed (the ones behind it move up). False, and nothing changes, if there are fewer.
bool bmInventoryTake(BmInventory *pInv, uint32_t key, uint32_t amount);

/// The script calls on the inventory (item_count, give, take, stat, add_stat; script_api.h).
/// False if `api` is none of them.
bool bmInventoryScriptCall(BmInventory *pInv, uint16_t api, const int32_t *pArgs, int32_t *pResult);
