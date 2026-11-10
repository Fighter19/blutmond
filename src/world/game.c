/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "game.h"

#include "hal.h"
#include "video.h"

#include <string.h>

#define OBJS_ENTRY_SIZE 20

// --- Objects --------------------------------------------------------------------------------

static uint8_t kindOf(const char *cls)
{
  if (strcmp(cls, "PlayerStart") == 0) return BM_OBJECT_PLAYER_START;
  if (strcmp(cls, "CameraZone") == 0) return BM_OBJECT_CAMERA_ZONE;
  if (strcmp(cls, "KillZone") == 0) return BM_OBJECT_KILL_ZONE;
  if (strcmp(cls, "Door") == 0) return BM_OBJECT_DOOR;
  if (strcmp(cls, "Trigger") == 0) return BM_OBJECT_TRIGGER;
  if (strcmp(cls, "SavePoint") == 0) return BM_OBJECT_SAVE_POINT;
  return BM_OBJECT_OTHER;
}

// Reads the room's objects once (string lookups are too slow to repeat every frame).
static void readObjects(BmGame *g)
{
  g->objectCount = 0;
  uint32_t size = 0;
  const uint8_t *p = bmPackChunkData(g->pPack, g->room.objectsChunk, &size);
  if (p == NULL || size < 4) return;
  uint16_t n = bmRead16(p);
  for (uint16_t i = 0; i < n && g->objectCount < BM_GAME_MAX_OBJECTS && 4u + (i + 1u) * OBJS_ENTRY_SIZE <= size; i++)
  {
    const uint8_t *o = p + 4 + i * OBJS_ENTRY_SIZE;
    BmObjectView *v = &g->objects[g->objectCount++];
    v->className = bmPackString(g->pPack, bmRead16(o));
    v->persistentId = bmRead16(o + 2);
    v->x = (int16_t)bmRead16(o + 4);
    v->y = (int16_t)bmRead16(o + 6);
    v->w = bmRead16(o + 8);
    v->h = bmRead16(o + 10);
    v->name = bmPackString(g->pPack, bmRead16(o + 12));
    v->propertyCount = bmRead16(o + 14);
    uint32_t offset = bmRead32(o + 16);
    v->pProperties = offset + v->propertyCount * 4u <= size ? p + offset : NULL;
    if (v->pProperties == NULL) v->propertyCount = 0;
    if (v->className == NULL) v->className = "";
    if (v->name == NULL) v->name = "";
    v->kind = kindOf(v->className);
  }
}

uint16_t bmGameObjectCount(const BmGame *pGame)
{
  return pGame->objectCount;
}

bool bmGameObject(const BmGame *pGame, uint16_t index, BmObjectView *pObject)
{
  if (index >= pGame->objectCount) return false;
  *pObject = pGame->objects[index];
  return true;
}

const char *bmGameObjectProperty(const BmGame *pGame, const BmObjectView *pObject, const char *key)
{
  for (uint16_t i = 0; i < pObject->propertyCount; i++)
  {
    const char *k = bmPackString(pGame->pPack, bmRead16(pObject->pProperties + i * 4));
    if (k && strcmp(k, key) == 0)
    {
      return bmPackString(pGame->pPack, bmRead16(pObject->pProperties + i * 4 + 2));
    }
  }
  return NULL;
}

static bool findObject(const BmGame *g, uint8_t kind, const char *name, BmObjectView *pOut)
{
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if (o->kind == kind && (name == NULL || strcmp(o->name, name) == 0))
    {
      *pOut = *o;
      return true;
    }
  }
  return false;
}

int32_t bmGameFindRoom(const BmPack *pPack, const char *roomId)
{
  uint32_t n = bmRoomCount(pPack);
  int32_t chunk = -1;
  for (uint32_t i = 0; i < n; i++)
  {
    chunk = bmPackFindChunk(pPack, BM_CHUNK_ROOM, (uint32_t)(chunk + 1));
    uint32_t size = 0;
    const uint8_t *p = bmPackChunkData(pPack, (uint32_t)chunk, &size);
    if (p && size >= 44)
    {
      const char *id = bmPackString(pPack, bmRead16(p + 40));
      if (id && strcmp(id, roomId) == 0) return (int32_t)i;
    }
  }
  return -1;
}

const char *bmGameRoomId(const BmGame *pGame)
{
  int32_t chunk = -1;
  for (uint32_t i = 0; i <= pGame->roomIndex; i++)
  {
    chunk = bmPackFindChunk(pGame->pPack, BM_CHUNK_ROOM, (uint32_t)(chunk + 1));
  }
  uint32_t size = 0;
  const uint8_t *p = chunk >= 0 ? bmPackChunkData(pGame->pPack, (uint32_t)chunk, &size) : NULL;
  return p && size >= 44 ? bmPackString(pGame->pPack, bmRead16(p + 40)) : NULL;
}

// --- Sprites --------------------------------------------------------------------------------

// The player until sprites come from packs: 16x32, drawn in code (4 bpp, 1D mapping).
static const char *const s_playerRows[32] = {
  "................", "......2222......", ".....222222.....", ".....233332.....",
  ".....231313.....", ".....233332.....", "......2332......", ".......22.......",
  ".....444444.....", "....44444444....", "...4445555444...", "...4455555544...",
  "...4455555544...", "...3.455554.3...", "...3.455554.3...", "...3.455554.3...",
  ".....444444.....", ".....666666.....", ".....66..66.....", ".....66..66.....",
  ".....66..66.....", ".....66..66.....", ".....66..66.....", ".....66..66.....",
  ".....66..66.....", ".....66..66.....", ".....66..66.....", ".....66..66.....",
  ".....11..11.....", "....111..111....", "....111..111....", "................",
};

static void loadSprites(void)
{
  uint8_t tiles[8][32];
  memset(tiles, 0, sizeof(tiles));
  for (int y = 0; y < 32; y++)
  {
    for (int x = 0; x < 16; x++)
    {
      char ch = s_playerRows[y][x];
      uint8_t v = ch == '.' ? 0 : (uint8_t)(ch - '0');
      int tile = (y / 8) * 2 + x / 8;  // 1D: rows of 2 tiles
      uint8_t *b = &tiles[tile][(y % 8) * 4 + (x % 8) / 2];
      *b |= (x & 1) ? (uint8_t)(v << 4) : v;
    }
  }
  bmTilesLoadObj(0, tiles, sizeof(tiles));

  // Placeholder: a 16x16 frame for objects whose class has no graphics yet.
  uint8_t box[4][32];
  memset(box, 0, sizeof(box));
  for (int y = 0; y < 16; y++)
  {
    for (int x = 0; x < 16; x++)
    {
      bool edge = x == 0 || y == 0 || x == 15 || y == 15 || x == y || x == 15 - y;
      if (!edge) continue;
      int tile = (y / 8) * 2 + x / 8;
      uint8_t *b = &box[tile][(y % 8) * 4 + (x % 8) / 2];
      *b |= (x & 1) ? (uint8_t)(1 << 4) : 1;
    }
  }
  bmTilesLoadObj(8, box, sizeof(box));

  BmColor pal[32];
  memset(pal, 0, sizeof(pal));
  pal[1] = bmColorRGB(6, 4, 4);     // boots
  pal[2] = bmColorRGB(20, 4, 6);    // hair
  pal[3] = bmColorRGB(31, 24, 18);  // skin
  pal[4] = bmColorRGB(22, 2, 4);    // coat
  pal[5] = bmColorRGB(28, 26, 22);  // shirt
  pal[6] = bmColorRGB(6, 6, 12);    // trousers
  pal[16 + 1] = bmColorRGB(4, 31, 31);
  bmPaletteLoadObj(0, pal, 32);
  bmVideoSetSpriteMapping1D(true);
}


static void updateSprites(BmGame *g)
{
  int32_t camX = g->room.cameraX, camY = g->room.cameraY;
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  bool blink = g->hurtTimer && (g->hurtTimer & 4);
  BmSprite s = {
    .x = (int16_t)(px - 8 - camX), .y = (int16_t)(py - 32 - camY),
    .shape = BM_SPRITE_TALL, .size = 2, .tile = 0, .priority = 1,
    .hflip = g->player.facing < 0, .visible = !blink,
  };
  bmSpriteSet(0, &s);

  // Placeholders for objects of classes that have no graphics yet (scripts come later).
  uint8_t next = 1;
  for (uint16_t i = 0; i < g->objectCount && next <= BM_GAME_MAX_PLACEHOLDERS; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if ((o->kind != BM_OBJECT_OTHER && o->kind != BM_OBJECT_SAVE_POINT) || o->className[0] == 0) continue;
    int32_t ox = o->x - 8 - camX, oy = (o->w || o->h ? o->y : o->y - 16) - camY;
    if (ox < -16 || ox > 240 || oy < -16 || oy > 160) continue;
    BmSprite b = { .x = (int16_t)ox, .y = (int16_t)oy, .shape = BM_SPRITE_SQUARE, .size = 1, .tile = 8,
                   .paletteBank = 1, .priority = 1, .visible = true };
    bmSpriteSet(next++, &b);
  }
  while (next <= BM_GAME_MAX_PLACEHOLDERS)
  {
    bmSpriteHide(next++);
  }
}

// --- Rooms, camera ----------------------------------------------------------------------------

static void updateCamera(BmGame *g)
{
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y) - g->playerParams.height / 2;

  // Camera zone that contains the player (the whole room if none does).
  g->zoneX0 = 0;
  g->zoneY0 = 0;
  g->zoneX1 = g->room.widthPx;
  g->zoneY1 = g->room.heightPx;
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if (o->kind == BM_OBJECT_CAMERA_ZONE && px >= o->x && px < o->x + o->w && py >= o->y && py < o->y + o->h)
    {
      g->zoneX0 = o->x;
      g->zoneY0 = o->y;
      g->zoneX1 = o->x + o->w;
      g->zoneY1 = o->y + o->h;
      break;
    }
  }
  int32_t cx = px - BM_HAL_SCREEN_WIDTH / 2, cy = py - BM_HAL_SCREEN_HEIGHT / 2;
  if (cx > g->zoneX1 - BM_HAL_SCREEN_WIDTH) cx = g->zoneX1 - BM_HAL_SCREEN_WIDTH;
  if (cx < g->zoneX0) cx = g->zoneX0;
  if (cy > g->zoneY1 - BM_HAL_SCREEN_HEIGHT) cy = g->zoneY1 - BM_HAL_SCREEN_HEIGHT;
  if (cy < g->zoneY0) cy = g->zoneY0;
  bmRoomSetCamera(&g->room, cx, cy);
}

static BmResult enterRoom(BmGame *g, uint32_t index)
{
  BmResult r = bmRoomOpen(&g->room, g->pPack, index);
  if (r != BM_SUCCESS) return r;
  g->roomIndex = index;
  uint32_t size = 0;
  const uint8_t *coll = bmPackChunkData(g->pPack, g->room.collisionChunk, &size);
  if (!bmCollisionInit(&g->collision, coll, size, (g->room.flags & 1) != 0)) return BM_ERROR_INVALID_ARGUMENT;
  g->physics.gravity = g->room.gravity;
  g->physics.maxFallSpeed = g->room.maxFallSpeed;
  g->physics.waterGravity = g->room.waterGravity;
  g->physics.waterMaxFallSpeed = g->room.waterMaxFallSpeed;
  readObjects(g);
  return bmRoomShow(&g->room, 0, 0);
}

BmResult bmGameStart(BmGame *g, const BmPack *pPack, const char *roomId)
{
  memset(g, 0, sizeof(*g));
  g->pPack = pPack;
  bmBodyDefaultParams(&g->playerParams);
  int32_t index = roomId ? bmGameFindRoom(pPack, roomId) : 0;
  if (index < 0) return BM_ERROR_INVALID_ARGUMENT;
  bmVideoInit();
  loadSprites();
  BmResult r = enterRoom(g, (uint32_t)index);
  if (r != BM_SUCCESS) return r;
  BmObjectView start;
  if (findObject(g, BM_OBJECT_PLAYER_START, NULL, &start))
  {
    bmBodyPlace(&g->player, start.x, start.y);
    const char *facing = bmGameObjectProperty(g, &start, "facing");
    if (facing && strcmp(facing, "left") == 0) g->player.facing = -1;
  }
  else
  {
    bmBodyPlace(&g->player, 32, 32);
  }
  updateCamera(g);
  updateSprites(g);
  return BM_SUCCESS;
}

static bool overlaps(int32_t ax, int32_t ay, int32_t aw, int32_t ah, int32_t bx, int32_t by, int32_t bw, int32_t bh)
{
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void respawn(BmGame *g)
{
  int8_t facing = g->player.facing;
  bmBodyPlace(&g->player, g->player.safeX, g->player.safeY);
  g->player.facing = facing;
  g->deaths++;
  g->hurtTimer = 60;
}

static void useDoor(BmGame *g, const BmObjectView *door)
{
  const char *target = bmGameObjectProperty(g, door, "target_room");
  const char *targetDoor = bmGameObjectProperty(g, door, "target_door");
  int32_t index = target ? bmGameFindRoom(g->pPack, target) : -1;
  if (index < 0) return;
  bm_fix vx = g->player.vx;
  if (enterRoom(g, (uint32_t)index) != BM_SUCCESS) return;
  g->roomChanges++;
  g->doorCooldown = 20;

  BmObjectView arrive;
  int32_t x = 32, y = 32;
  if (targetDoor && findObject(g, BM_OBJECT_DOOR, targetDoor, &arrive))
  {
    // Step into the room, away from the edge the door is on.
    bool rightSide = arrive.x + arrive.w / 2 > g->room.widthPx / 2;
    x = rightSide ? arrive.x - 12 : arrive.x + arrive.w + 12;
    y = arrive.y + arrive.h;
  }
  else if (findObject(g, BM_OBJECT_PLAYER_START, NULL, &arrive))
  {
    x = arrive.x;
    y = arrive.y;
  }
  int8_t facing = g->player.facing;
  bmBodyPlace(&g->player, x, y);
  g->player.facing = facing;
  g->player.vx = vx;
}

void bmGameFrame(BmGame *g, uint16_t keys)
{
  g->frame++;
  uint16_t pressed = (uint16_t)(keys & ~g->keysPrevious);
  g->keysPrevious = keys;

  BmBodyInput in = {
    .dx = (int8_t)(((keys & BM_KEY_RIGHT) ? 1 : 0) - ((keys & BM_KEY_LEFT) ? 1 : 0)),
    .dy = (int8_t)(((keys & BM_KEY_DOWN) ? 1 : 0) - ((keys & BM_KEY_UP) ? 1 : 0)),
    .jumpPressed = (pressed & BM_KEY_A) != 0,
    .jumpHeld = (keys & BM_KEY_A) != 0,
  };
  uint32_t events = bmBodyStep(&g->player, &g->playerParams, &g->collision, &g->physics, in);
  if (g->hurtTimer) g->hurtTimer--;
  if (g->doorCooldown) g->doorCooldown--;
  if (g->saveCooldown) g->saveCooldown--;

  if (events & BM_BODY_DEATH)
  {
    respawn(g);
  }
  else if ((events & BM_BODY_DAMAGE) && g->hurtTimer == 0)
  {
    g->hurts++;
    g->hurtTimer = 60;
    g->player.vy = -BM_FIX(3);
    g->player.vx = -g->player.facing * BM_FIX(2);
  }

  // Engine objects the player touches.
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  int32_t hw = g->playerParams.halfWidth, h = g->playerParams.height;
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if (o->kind == BM_OBJECT_SAVE_POINT)
    {
      // Points: the area of a 16x32 marker standing on the point.
      if (g->saveCooldown == 0 && g->player.onGround && overlaps(px - hw, py - h, hw * 2, h, o->x - 8, o->y - 32, 16, 32))
      {
        bmGameWriteSaveGame(g);
        g->saveCooldown = 120;
      }
      continue;
    }
    if ((o->kind != BM_OBJECT_KILL_ZONE && o->kind != BM_OBJECT_DOOR) || o->w == 0 || o->h == 0) continue;
    if (!overlaps(px - hw, py - h, hw * 2, h, o->x, o->y, o->w, o->h)) continue;
    if (o->kind == BM_OBJECT_KILL_ZONE)
    {
      respawn(g);
      break;
    }
    if (g->doorCooldown == 0)
    {
      BmObjectView door = *o;  // the room's objects change when the room does
      useDoor(g, &door);
      break;
    }
  }
  // Falling out of the bottom without a kill zone.
  if (BM_FIX_INT(g->player.y) > g->room.heightPx + 64)
  {
    respawn(g);
  }

  updateCamera(g);
  updateSprites(g);
}

// --- State --------------------------------------------------------------------------------

#define F(member, type, flags) BM_STATE_FIELD(BmGame, member, type, flags)
static const BmStateField s_gameFields[] = {
  F(roomIndex, BM_TYPE_U32, BM_STATE_SAVE | BM_STATE_LINK),
  F(player.x, BM_TYPE_FIX, BM_STATE_SAVE | BM_STATE_LINK),
  F(player.y, BM_TYPE_FIX, BM_STATE_SAVE | BM_STATE_LINK),
  F(player.vx, BM_TYPE_FIX, BM_STATE_LINK),
  F(player.vy, BM_TYPE_FIX, BM_STATE_LINK),
  F(player.onGround, BM_TYPE_BOOL, BM_STATE_LINK),
  F(player.climbing, BM_TYPE_BOOL, BM_STATE_LINK),
  F(player.inWater, BM_TYPE_BOOL, BM_STATE_DERIVED),
  F(player.jumpCutDone, BM_TYPE_BOOL, BM_STATE_SNAPSHOT),
  F(player.facing, BM_TYPE_S8, BM_STATE_SAVE | BM_STATE_LINK),
  F(player.coyote, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(player.jumpBuffer, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(player.dropThrough, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(player.safeX, BM_TYPE_S32, BM_STATE_SAVE),
  F(player.safeY, BM_TYPE_S32, BM_STATE_SAVE),
  F(frame, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(keysPrevious, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(hurtTimer, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(doorCooldown, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(saveCooldown, BM_TYPE_U16, BM_STATE_TRANSIENT),
  F(deaths, BM_TYPE_U32, BM_STATE_SAVE),
  F(roomChanges, BM_TYPE_U32, BM_STATE_SAVE),
  F(hurts, BM_TYPE_U32, BM_STATE_SAVE),
  F(saves, BM_TYPE_U32, BM_STATE_SAVE),
};
#undef F

BmStateBlock bmGameStateBlock(BmGame *pGame)
{
  return (BmStateBlock){ "game", s_gameFields, (uint16_t)(sizeof(s_gameFields) / sizeof(s_gameFields[0])), pGame };
}

BmResult bmGameAfterLoad(BmGame *g)
{
  BmBody player = g->player;
  uint32_t roomIndex = g->roomIndex;
  if (roomIndex >= bmRoomCount(g->pPack)) return BM_ERROR_INVALID_ARGUMENT;
  BmResult r = enterRoom(g, roomIndex);
  if (r != BM_SUCCESS) return r;
  g->player = player;
  g->saveCooldown = 0;
  updateCamera(g);
  updateSprites(g);
  return BM_SUCCESS;
}

// Save memory layout: the save game at offset 0 (bmStateWrite format).
#define SAVE_CAPACITY 512

bool bmGameWriteSaveGame(BmGame *g)
{
  static uint8_t buffer[SAVE_CAPACITY] BM_EWRAM_BSS;
  BmStateBlock block = bmGameStateBlock(g);
  g->saves++;
  uint32_t size = bmStateWrite(&block, 1, BM_STATE_SAVE, buffer, sizeof(buffer));
  if (size == 0) return false;
  bmHalSaveWrite(0, buffer, size);
  return true;
}

bool bmGameReadSaveGame(BmGame *g)
{
  static uint8_t buffer[SAVE_CAPACITY] BM_EWRAM_BSS;
  bmHalSaveRead(0, buffer, sizeof(buffer));
  BmStateBlock block = bmGameStateBlock(g);
  // Only the `save` fields are read; everything else stays as bmGameStart left it.
  if (bmStateRead(&block, 1, buffer, sizeof(buffer)) != BM_STATE_OK) return false;
  g->player.vx = g->player.vy = 0;
  return bmGameAfterLoad(g) == BM_SUCCESS;
}
