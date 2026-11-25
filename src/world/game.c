/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#include "game.h"

#include "data.h"
#include "commands.h"
#include "effects.h"
#include "light.h"
#include "text.h"

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
  if (strcmp(cls, "Breakable") == 0) return BM_OBJECT_BREAKABLE;
  if (strcmp(cls, "PushBlock") == 0 || strcmp(cls, "PressurePlate") == 0 || strcmp(cls, "Gate") == 0) return BM_OBJECT_PUZZLE;
  if (strcmp(cls, "GrappleAnchor") == 0) return BM_OBJECT_GRAPPLE_ANCHOR;
  if (strcmp(cls, "Light") == 0) return BM_OBJECT_LIGHT;
  if (strcmp(cls, "Puddle") == 0) return BM_OBJECT_PUDDLE;
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

// Which sheet each object of the room is drawn with.
static void bindObjectSprites(BmGame *g)
{
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    g->objectSheet[i] = -1;
    for (uint8_t k = 0; k < g->sheetCount; k++)
    {
      const char *name = bmPackString(g->pPack, g->sheets[k].nameId);
      const char *cls = g->objects[i].className;
      if (name == NULL || cls == NULL) continue;
      // The sprite named like the class, whatever the case ("Bat" uses "bat").
      const char *a = name, *b = cls;
      for (; *a && *b; a++, b++)
      {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) break;
      }
      if (*a == 0 && *b == 0) g->objectSheet[i] = (int8_t)k;
    }
    // A door's panel (effects.h): the sprite its `panel` property names, and where it stands.
    const char *panel = g->objects[i].kind == BM_OBJECT_DOOR ? bmGameObjectProperty(g, &g->objects[i], "panel") : NULL;
    const char *dir = g->objects[i].kind == BM_OBJECT_DOOR ? bmGameObjectProperty(g, &g->objects[i], "direction") : NULL;
    g->objectDoorSide[i] = (uint8_t)(dir == NULL ? 3 : strcmp(dir, "north") == 0 ? 0 : strcmp(dir, "east") == 0 ? 1 : strcmp(dir, "west") == 0 ? 2 : 3);
    if (panel != NULL)
    {
      // The floor under the door's middle (from its top to 32 px below it), else its bottom.
      const BmObjectView *o = &g->objects[i];
      int32_t ground = bmCollisionFloor(&g->collision, o->x + o->w / 2, o->y, o->y + o->h + 32, true);
      g->objectDoorGround[i] = (int16_t)(ground != INT32_MAX ? ground : o->y + o->h);
    }
    for (uint8_t k = 0; panel != NULL && k < g->sheetCount; k++)
    {
      const char *name = bmPackString(g->pPack, g->sheets[k].nameId);
      if (name != NULL && strcmp(name, panel) == 0) g->objectSheet[i] = (int8_t)k;
    }
  }
}

// The sprite sheet named by string id `name`, or -1.
static int8_t sheetNamed(const BmGame *g, uint16_t name)
{
  for (uint8_t s = 0; s < g->sheetCount; s++)
    if (g->sheets[s].nameId == name) return (int8_t)s;
  return -1;
}

// The tiles the built-in graphics take at the start of sprite memory: the figure (8), the
// placeholder frame (4).
#define BUILTIN_TILES 12

// Forgets every sheet in sprite memory but the player's and gives back the banks
// (layered-sprites.md §3): a room loads what it shows.
static void forgetSprites(BmGame *g)
{
  for (uint8_t k = 0; k < g->sheetCount; k++)
  {
    bool player = false;
    for (uint8_t i = 0; i < g->playerSheetCount; i++) player |= g->playerSheets[i] == (int8_t)k;
    if (!player) g->sheetTile[k] = BM_GAME_NO_TILES;
  }
  memset(g->bankSheet, 0xFF, sizeof(g->bankSheet));
  g->tileNext = g->tileFloor;
  for (uint8_t i = 0; i < g->playerSheetCount; i++)
  {
    g->bankSheet[i] = (uint8_t)g->playerSheets[i];
    g->bankPalette[i] = g->playerPalettes[i];
    g->sheets[g->playerSheets[i]].bank = i;
  }
}

// The player's sheets: the figure "player"'s, else the sprite "player" (layered-sprites.md §4).
static void findPlayer(BmGame *g)
{
  memset(&g->playerFigure, 0, sizeof(g->playerFigure));
  memset(g->playerLayerSheet, 0xFF, sizeof(g->playerLayerSheet));
  memset(g->playerLayers, 0, sizeof(g->playerLayers));
  memset(g->playerLayerPalette, 0, sizeof(g->playerLayerPalette));
  for (uint8_t i = 0; i < BM_FIGURE_MAX_LAYERS; i++)
  {
    g->playerLayers[i].sprite = 0xFFFF;
    g->playerLayers[i].palette = 0xFF;
  }
  g->playerSheetCount = 0;
  g->playerSheet = -1;
  if (bmFigureFind(&g->playerFigure, g->pPack, "player") == BM_SUCCESS)
  {
    for (uint8_t i = 0; i < g->playerFigure.layerCount; i++)
    {
      const BmFigureLayer *l = &g->playerFigure.layers[i];
      int8_t s = l->sprite == 0xFFFF ? -1 : sheetNamed(g, l->sprite);
      g->playerLayerSheet[i] = s;
      if (s >= 0 && l->palette != 0xFFFF)
      {
        int32_t named = bmSpriteSheetPalette(&g->sheets[s], g->pPack, bmPackString(g->pPack, l->palette));
        g->playerLayerPalette[i] = (uint8_t)(named < 0 ? 0 : named);
      }
      bool have = s < 0;
      for (uint8_t k = 0; k < g->playerSheetCount; k++) have |= g->playerSheets[k] == s;
      if (have) continue;
      int32_t pal = l->palette == 0xFFFF ? 0 : bmSpriteSheetPalette(&g->sheets[s], g->pPack, bmPackString(g->pPack, l->palette));
      g->playerLayerPalette[i] = (uint8_t)(pal < 0 ? 0 : pal);
      // An `aim` layer starts on its first direction set.
      int32_t set = bmSpriteSheetFirstDirections(&g->sheets[s]);
      if (l->mode == BM_FIGURE_AIM && set >= 0) g->playerLayers[i].anim.anim = (uint8_t)set;
      g->playerSheets[g->playerSheetCount] = s;
      g->playerPalettes[g->playerSheetCount++] = (uint8_t)(pal < 0 ? 0 : pal);
    }
    g->playerSheet = g->playerLayerSheet[0];
  }
  if (g->playerSheet < 0)
  {
    g->playerFigure.layerCount = 0;
    g->playerSheetCount = 0;
    for (uint8_t k = 0; k < g->sheetCount && g->playerSheet < 0; k++)
    {
      const char *name = bmPackString(g->pPack, g->sheets[k].nameId);
      if (name != NULL && strcmp(name, "player") == 0) g->playerSheet = (int8_t)k;
    }
    if (g->playerSheet >= 0)
    {
      g->playerSheets[0] = g->playerSheet;
      g->playerPalettes[0] = 0;
      g->playerSheetCount = 1;
    }
  }
  if (g->playerSheet < 0) return;
  static const char *const anims[BM_PLAYER_ANIM_COUNT] = { "idle", "run", "jump", "fall", "climb", "push" };
  const BmSpriteSheet *s = &g->sheets[g->playerSheet];
  for (int i = 0; i < BM_PLAYER_ANIM_COUNT; i++)
  {
    int32_t a = bmSpriteSheetAnim(s, g->pPack, anims[i]);
    g->playerAnims[i] = (int8_t)(a < 0 ? 0 : a);
  }
  // Without a push animation, pushing looks like running.
  if (bmSpriteSheetAnim(s, g->pPack, "push") < 0) g->playerAnims[BM_PLAYER_ANIM_PUSH] = g->playerAnims[BM_PLAYER_ANIM_RUN];
}

static int8_t sheetBank(BmGame *g, int8_t sheet, uint8_t palette);

// Notes a pair the room draws (once).
static void noteUsed(BmGame *g, int8_t sheet, uint8_t palette)
{
  for (uint8_t k = 0; k < g->usedCount; k++)
    if (g->usedSheet[k] == sheet && g->usedPalette[k] == palette) return;
  if (g->usedCount < sizeof(g->usedSheet)) g->usedSheet[g->usedCount] = sheet, g->usedPalette[g->usedCount++] = palette;
}

int8_t bmGameSheetBank(BmGame *g, int8_t sheet, uint8_t palette)
{
  if (sheet < 0 || (uint8_t)sheet >= g->sheetCount) return -1;
  if (palette >= g->sheets[sheet].paletteCount) palette = 0;
  uint16_t misses = g->spriteMisses;
  int8_t bank = sheetBank(g, sheet, palette);
  if (g->spriteMisses == misses)
  {
    if (bank >= 0) noteUsed(g, sheet, palette);
    return bank;
  }
  // It did not fit beside what earlier rooms left (a dialogue's portrait, an effect): those
  // go, what this room has drawn is loaded again, then this one. Pictures already given out
  // this frame may show the old tiles for one frame.
  bool stale = false;   // something loaded that this room has not drawn (not the player's)
  for (uint8_t k = 0; k < g->sheetCount && !stale; k++)
  {
    if (g->sheetTile[k] == BM_GAME_NO_TILES) continue;
    bool kept = false;
    for (uint8_t i = 0; i < g->playerSheetCount; i++) kept |= g->playerSheets[i] == (int8_t)k;
    for (uint8_t i = 0; i < g->usedCount; i++) kept |= g->usedSheet[i] == (int8_t)k;
    stale = !kept;
  }
  for (uint8_t b = 0; b < BM_GAME_BANKS && !stale; b++)
  {
    if (g->bankSheet[b] == 0xFF) continue;
    bool kept = false;
    for (uint8_t i = 0; i < g->playerSheetCount; i++) kept |= g->playerSheets[i] == (int8_t)g->bankSheet[b];
    for (uint8_t i = 0; i < g->usedCount; i++) kept |= g->usedSheet[i] == (int8_t)g->bankSheet[b] && g->usedPalette[i] == g->bankPalette[b];
    stale = !kept;
  }
  if (!stale) return bank;   // nothing to make room with: it stays missing (counted)
  g->spriteMisses = misses;
  forgetSprites(g);
  for (uint8_t k = 0; k < g->usedCount; k++) sheetBank(g, g->usedSheet[k], g->usedPalette[k]);
  bank = sheetBank(g, sheet, palette);
  if (bank >= 0) noteUsed(g, sheet, palette);
  return bank;
}

static int8_t sheetBank(BmGame *g, int8_t sheet, uint8_t palette)
{
  if (sheet < 0 || (uint8_t)sheet >= g->sheetCount) return -1;
  BmSpriteSheet *s = &g->sheets[sheet];
  if (palette >= s->paletteCount) palette = 0;
  for (uint8_t b = 0; b < BM_GAME_BANKS; b++)
    if (g->bankSheet[b] == (uint8_t)sheet && g->bankPalette[b] == palette) return (int8_t)b;
  // Its tiles first; a sheet that does not fit is not drawn.
  if (g->sheetTile[sheet] == BM_GAME_NO_TILES)
  {
    if ((uint32_t)g->tileNext + s->tileCount > BM_GAME_OBJ_TILES || bmSpriteSheetLoadTiles(s, g->tileNext) != BM_SUCCESS)
    {
      g->spriteMisses++;
      return -1;
    }
    g->sheetTile[sheet] = g->tileNext;
    g->tileNext = (uint16_t)(g->tileNext + s->tileCount);
  }
  else
  {
    s->tileBase = g->sheetTile[sheet];
  }
  // A free bank; with none, one with another palette of the sheet will do.
  int8_t bank = -1;
  for (uint8_t b = 0; b < BM_GAME_BANKS && bank < 0; b++)
    if (g->bankSheet[b] == 0xFF) bank = (int8_t)b;
  if (bank < 0)
  {
    g->spriteMisses++;
    for (uint8_t b = 0; b < BM_GAME_BANKS; b++)
      if (g->bankSheet[b] == (uint8_t)sheet) return (int8_t)b;
    return -1;
  }
  g->bankSheet[bank] = (uint8_t)sheet;
  g->bankPalette[bank] = palette;
  bmSpriteSheetLoadPalette(s, palette, (uint8_t)bank);
  if (palette == 0) s->bank = (uint8_t)bank;
  return bank;
}

// The built-in sprites' banks.
static void loadBuiltinPalettes(void)
{
  // Bank 13: a struck object's flash, every colour white (hits.h).
  BmColor flash[16];
  for (int c = 0; c < 16; c++) flash[c] = bmColorRGB(31, 31, 31);
  bmPaletteLoadObj(BM_HITS_FLASH_BANK * 16, flash, 16);

  // Banks 14 and 15: the built-in figure and the placeholder frame.
  BmColor pal[32];
  memset(pal, 0, sizeof(pal));
  pal[1] = bmColorRGB(6, 4, 4);     // boots
  pal[2] = bmColorRGB(20, 4, 6);    // hair
  pal[3] = bmColorRGB(31, 24, 18);  // skin
  pal[4] = bmColorRGB(22, 2, 4);    // coat
  pal[5] = bmColorRGB(28, 26, 22);  // shirt
  pal[6] = bmColorRGB(6, 6, 12);    // trousers
  pal[16 + 1] = bmColorRGB(4, 31, 31);
  bmPaletteLoadObj(14 * 16, pal, 32);
}

static void loadSprites(BmGame *g)
{
  // Once per pack: every sheet's data (what logic reads: frames, boxes, animations). What is
  // in sprite memory is given out as rooms need it (bmGameSheetBank) and stays across rooms
  // until a room needs more than is left (preloadSprites).
  if (g->pSpritesFrom == g->pPack->pData && g->spritesCrc == g->pPack->header.crc32)
  {
    bindObjectSprites(g);
    return;
  }
  g->pSpritesFrom = g->pPack->pData;
  g->spritesCrc = g->pPack->header.crc32;

  // The built-in graphics at the start of sprite memory, the player's sheet after them (it
  // stays), the rest from there on as rooms need them.
  g->sheetCount = 0;
  g->playerSheet = -1;
  g->sparkSheet = -1;
  g->debrisSheet = -1;
  for (uint32_t n = 0; g->sheetCount < BM_GAME_MAX_SHEETS; n++)
  {
    int32_t chunk = bmSpriteSheetChunk(g->pPack, n);
    if (chunk < 0) break;
    BmSpriteSheet *s = &g->sheets[g->sheetCount];
    if (bmSpriteSheetOpen(s, g->pPack, (uint32_t)chunk) != BM_SUCCESS) continue;
    g->sheetTile[g->sheetCount] = BM_GAME_NO_TILES;
    const char *name = bmPackString(g->pPack, s->nameId);
    if (name != NULL && strcmp(name, "spark") == 0) g->sparkSheet = (int8_t)g->sheetCount;
    if (name != NULL && strcmp(name, "debris") == 0) g->debrisSheet = (int8_t)g->sheetCount;
    g->sheetCount++;
  }
  findPlayer(g);
  g->builtinTile = 0;
  g->tileFloor = BUILTIN_TILES;
  for (uint8_t i = 0; i < g->playerSheetCount; i++)
  {
    BmSpriteSheet *s = &g->sheets[g->playerSheets[i]];
    if ((uint32_t)g->tileFloor + s->tileCount > BM_GAME_OBJ_TILES || bmSpriteSheetLoadTiles(s, g->tileFloor) != BM_SUCCESS) continue;
    g->sheetTile[g->playerSheets[i]] = g->tileFloor;
    g->tileFloor = (uint16_t)(g->tileFloor + s->tileCount);
    bmSpriteSheetLoadPalette(s, g->playerPalettes[i], i);
  }
  forgetSprites(g);
  bindObjectSprites(g);

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
  bmTilesLoadObj(g->builtinTile, tiles, sizeof(tiles));

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
  bmTilesLoadObj((uint16_t)(g->builtinTile + 8), box, sizeof(box));

  loadBuiltinPalettes();
  bmVideoSetSpriteMapping1D(true);
}


// Picks the player's animation from what the body does and advances it by one frame.
void bmGamePlayPlayerAnim(BmGame *g, uint16_t name)
{
  if (g->playerSheet < 0) return;
  int32_t anim = bmSpriteSheetAnim(&g->sheets[g->playerSheet], g->pPack, bmPackString(g->pPack, name));
  if (anim < 0) return;
  bmAnimPlay(&g->playerAnim, (uint8_t)anim, true);
  g->animHeld = g->lockTimer != 0 ? 2 : 1;
}

// The sheet the player's layer `i` shows now (-1: none).
static int8_t layerSheet(const BmGame *g, uint8_t i)
{
  uint16_t shown = g->playerLayers[i].sprite;
  return shown != 0xFFFF ? sheetNamed(g, shown) : g->playerLayerSheet[i];
}

// The player's layers (figure.h): turning toward their goal; an `own` layer plays the animation
// named like the root's, an `aim` layer or one a script plays its own; each at its own pace.
static void animateLayers(BmGame *g)
{
  for (uint8_t i = 0; i < g->playerFigure.layerCount; i++)
  {
    BmFigureLayerState *l = &g->playerLayers[i];
    // Tracking an object: the goal is the way to its middle from the layer's anchor; once the
    // object is gone the layer stays where it got to.
    if (l->track != 0)
    {
      int32_t t = bmActorsHandleIndex(g, l->track);
      if (t < 0) l->track = 0;
      else
      {
        int32_t lx, ly, tx, ty;
        uint8_t unused;
        bmGamePlayerPoint(g, (int8_t)i, 0xFFFF, &lx, &ly, &unused);
        bmActorsCentre(g, (uint8_t)t, &tx, &ty);
        l->goal = bmAngleOf(tx - BM_FIX_INT(lx), ty - BM_FIX_INT(ly));
      }
    }
    if (l->speed != 0) l->angle = bmAngleToward(l->angle, l->goal, l->speed);
    int8_t s = layerSheet(g, i);
    if (i == 0 || s < 0) continue;
    uint8_t mode = g->playerFigure.layers[i].mode;
    if (!(l->flags & BM_LAYER_PLAYED) && mode == BM_FIGURE_OWN)
    {
      int32_t a = bmFigureLayerAnim(&g->sheets[g->playerSheet], g->playerAnim.anim, &g->sheets[s]);
      if (a < 0) continue;
      bmAnimPlay(&l->anim, (uint8_t)a, false);
    }
    if ((l->flags & BM_LAYER_PLAYED) || mode != BM_FIGURE_SYNC) bmAnimTick(&g->sheets[s], &l->anim);
  }
}

// The palette the player's layer `i` is drawn in: one a script chose (layer_palette), else the
// one the equipment names (look_palette) if its sprite has it, else the figure's.
static uint8_t playerLayerPaletteNow(const BmGame *g, uint8_t i)
{
  const BmFigureLayerState *l = &g->playerLayers[i];
  if (l->palette != 0xFF) return l->palette;
  int8_t s = layerSheet(g, i);
  if (g->equipPalette != 0xFFFF && s >= 0)
  {
    int32_t p = bmSpriteSheetPalette(&g->sheets[s], g->pPack, bmPackString(g->pPack, g->equipPalette));
    if (p >= 0) return (uint8_t)p;
  }
  return g->playerLayerPalette[i];
}

// The player's figure as it stands now: its layers' sheets and pose (figure.h).
static void playerPose(const BmGame *g, const BmSpriteSheet *sheets[BM_FIGURE_MAX_LAYERS], BmFigurePose *pose)
{
  BmFigure f = g->playerFigure;
  BmAnimState states[BM_FIGURE_MAX_LAYERS];
  uint8_t angles[BM_FIGURE_MAX_LAYERS];
  bool left = g->player.facing < 0, up = g->player.flipped;
  for (uint8_t i = 0; i < BM_FIGURE_MAX_LAYERS; i++)
  {
    int8_t s = i < f.layerCount ? layerSheet(g, i) : -1;
    sheets[i] = s >= 0 ? &g->sheets[s] : NULL;
    const BmFigureLayerState *l = &g->playerLayers[i];
    states[i] = i == 0 ? g->playerAnim : l->anim;
    angles[i] = bmAngleOfDrawing(l->angle, left, up);
    // A layer a script plays goes its own way.
    if (i != 0 && (l->flags & BM_LAYER_PLAYED)) f.layers[i].mode = BM_FIGURE_OWN;
  }
  bmFigurePose(&f, sheets, states, angles, left, up, pose);
}

int32_t bmGameLayerCall(BmGame *g, uint16_t api, const int32_t *args)
{
  int8_t layer = bmGamePlayerLayer(g, (uint16_t)args[0]);
  if (layer < 0) return 0;
  BmFigureLayerState *l = &g->playerLayers[layer];
  int8_t sheet = layerSheet(g, (uint8_t)layer);
  const char *name = api == BM_SCRIPT_API_layer_play || api == BM_SCRIPT_API_layer_show || api == BM_SCRIPT_API_layer_palette
                         ? bmPackString(g->pPack, (uint16_t)args[1])
                         : NULL;
  bool byDefault = name != NULL && strcmp(name, "default") == 0;
  switch (api)
  {
  case BM_SCRIPT_API_layer_aim:
    l->angle = (uint8_t)args[1];
    l->speed = 0;
    l->track = 0;
    return 0;
  case BM_SCRIPT_API_layer_track:
    l->track = bmActorsHandleIndex(g, (uint32_t)args[1]) >= 0 ? (uint32_t)args[1] : 0;
    l->speed = (uint8_t)(args[2] < 0 ? 0 : args[2] > 255 ? 255 : args[2]);
    return 0;
  case BM_SCRIPT_API_layer_turn:
    l->track = 0;
    l->goal = (uint8_t)args[1];
    l->speed = (uint8_t)(args[2] < 0 ? 0 : args[2] > 255 ? 255 : args[2]);
    if (l->speed == 0) l->angle = l->goal;
    return 0;
  case BM_SCRIPT_API_layer_angle:
    return l->angle;
  case BM_SCRIPT_API_layer_play:
  {
    int32_t anim = sheet >= 0 ? bmSpriteSheetAnimById(&g->sheets[sheet], (uint16_t)args[1]) : -1;
    if (anim >= 0 && layer != 0)
    {
      l->flags |= BM_LAYER_PLAYED;
      bmAnimPlay(&l->anim, (uint8_t)anim, true);
    }
    else if (layer == 0) bmGamePlayPlayerAnim(g, (uint16_t)args[1]);   // the root: as owner_play_anim
    else
    {
      // Back to what the figure says: an `aim` layer to its first direction set.
      l->flags &= (uint8_t)~BM_LAYER_PLAYED;
      int32_t set = sheet >= 0 ? bmSpriteSheetFirstDirections(&g->sheets[sheet]) : -1;
      if (g->playerFigure.layers[layer].mode == BM_FIGURE_AIM && set >= 0) bmAnimPlay(&l->anim, (uint8_t)set, true);
    }
    return 0;
  }
  case BM_SCRIPT_API_layer_show:
    l->sprite = byDefault ? 0xFFFF : (uint16_t)args[1];
    l->flags &= (uint8_t)~BM_LAYER_HIDDEN;
    return 0;
  case BM_SCRIPT_API_layer_hide:
    l->flags |= BM_LAYER_HIDDEN;
    return 0;
  case BM_SCRIPT_API_layer_palette:
  {
    int32_t p = !byDefault && sheet >= 0 ? bmSpriteSheetPalette(&g->sheets[sheet], g->pPack, name) : -1;
    l->palette = (uint8_t)(p < 0 ? 0xFF : p);
    return 0;
  }
  case BM_SCRIPT_API_layer_point_x:
  case BM_SCRIPT_API_layer_point_y:
  case BM_SCRIPT_API_layer_point_angle:
  {
    int32_t x, y;
    uint8_t angle;
    bmGamePlayerPoint(g, layer, (uint16_t)args[1], &x, &y, &angle);
    return api == BM_SCRIPT_API_layer_point_x ? x : api == BM_SCRIPT_API_layer_point_y ? y : angle;
  }
  default:
    return 0;
  }
}

int8_t bmGamePlayerLayer(const BmGame *g, uint16_t name)
{
  for (uint8_t i = 0; i < g->playerFigure.layerCount; i++)
    if (g->playerFigure.layers[i].name == name) return (int8_t)i;
  return -1;
}

bool bmGamePlayerPoint(BmGame *g, int8_t layer, uint16_t point, int32_t *pX, int32_t *pY, uint8_t *pAngle)
{
  bool left = g->player.facing < 0, up = g->player.flipped;
  int32_t x = g->player.x, y = up ? g->player.y - BM_FIX(g->playerParams.height) : g->player.y;
  *pAngle = left ? 128 : 0;
  *pX = x;
  *pY = y;
  if (layer < 0 || layer >= g->playerFigure.layerCount) return false;
  const BmSpriteSheet *sheets[BM_FIGURE_MAX_LAYERS];
  BmFigurePose pose;
  playerPose(g, sheets, &pose);
  *pX = x + BM_FIX(pose.x[layer]);
  *pY = y + BM_FIX(pose.y[layer]);
  BmSpritePoint pt;
  if (sheets[layer] == NULL || !bmSpriteSheetPoint(sheets[layer], pose.frame[layer], point, left, up, &pt)) return false;
  *pX += BM_FIX(pt.x);
  *pY += BM_FIX(pt.y);
  *pAngle = bmAngleOfDrawing(pt.angle, left, false);   // bmSpriteSheetPoint turned it upside down already
  return true;
}

static void animateBody(BmGame *g);

static void animatePlayer(BmGame *g)
{
  if (g->playerSheet < 0) return;
  animateBody(g);
  animateLayers(g);
}

static void animateBody(BmGame *g)
{
  // A script's animation holds until its lock is over, or until it has finished.
  if (g->animHeld != 0 && (g->animHeld == 2 ? g->lockTimer == 0 : g->playerAnim.finished != 0)) g->animHeld = 0;
  if (g->animHeld != 0)
  {
    bmAnimTick(&g->sheets[g->playerSheet], &g->playerAnim);
    return;
  }
  const BmBody *p = &g->player;
  int which = BM_PLAYER_ANIM_IDLE;
  bool advance = true;
  if (p->climbing)
  {
    which = BM_PLAYER_ANIM_CLIMB;
    advance = p->vy != 0 || p->vx != 0;
  }
  else if (!p->onGround) which = p->vy < 0 ? BM_PLAYER_ANIM_JUMP : BM_PLAYER_ANIM_FALL;
  else if (g->puzzle.pushing != 0) which = BM_PLAYER_ANIM_PUSH;   // against a push block (puzzle.h)
  else if (p->vx > BM_FIX(1) / 8 || p->vx < -(BM_FIX(1) / 8)) which = BM_PLAYER_ANIM_RUN;
  bmAnimPlay(&g->playerAnim, (uint8_t)g->playerAnims[which], false);
  if (advance) bmAnimTick(&g->sheets[g->playerSheet], &g->playerAnim);
}

static void updateSprites(BmGame *g)
{
  int32_t camX = g->room.cameraX, camY = g->room.cameraY;
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  // The player sways with a wave (a teleport's) as the room behind does.
  px -= bmEffectsWaveAt(g, py - 16 - camY);
  bool blink = g->hurtTimer && (g->hurtTimer & 4);
  uint8_t next = 0;
  // The speaker's portrait in the dialogue box, first: in front of everything.
  if (g->dialogue != 0 && g->screens.count == 0 && g->dialoguePortrait != 0xFFFF)
  {
    int8_t s = sheetNamed(g, g->dialoguePortrait);
    if (bmGameSheetBank(g, s, 0) >= 0)
      next = (uint8_t)(next + bmSpriteSheetDraw(&g->sheets[s], bmAnimFrameAt(&g->sheets[s], 0, g->frame), BM_GAME_PORTRAIT_X,
                                                BM_GAME_PORTRAIT_Y, false, 0, next, 4));
  }
  // The marker (marker_show) at its object's middle, in front of it.
  if (g->markerTarget != 0)
  {
    int32_t t = bmActorsHandleIndex(g, g->markerTarget), s = sheetNamed(g, g->markerSprite), mx, my;
    if (t < 0) g->markerTarget = 0;
    else if (bmGameSheetBank(g, (int8_t)s, 0) >= 0)
    {
      bmActorsCentre(g, (uint8_t)t, &mx, &my);
      next = (uint8_t)(next + bmSpriteSheetDraw(&g->sheets[s], bmAnimFrameAt(&g->sheets[s], 0, g->frame), mx - camX, my - camY, false, 0,
                                                next, 4));
    }
  }
  // Effects (sparks, debris) in front of the figures they are on, the player too.
  for (uint8_t k = 0; k < BM_GAME_EFFECTS && next < BM_GAME_MAX_OAM; k++)
  {
    if (g->effectAge[k] == 0 || bmGameSheetBank(g, g->effectSheet[k], 0) < 0) continue;
    const BmSpriteSheet *s = &g->sheets[g->effectSheet[k]];
    next = (uint8_t)(next + bmSpriteSheetDraw(s, bmAnimFrameAt(s, 0, (uint32_t)(g->effectAge[k] - 1)), g->effectX[k] - camX,
                                              g->effectY[k] - camY, false, 1, next, (uint8_t)(BM_GAME_MAX_OAM - next)));
  }
  if (g->playerSheet >= 0)
  {
    const BmSpriteSheet *s = &g->sheets[g->playerSheet];
    // Upside down with gravity flipped: the feet at the top of the body's box.
    bool up = g->player.flipped, left = g->player.facing < 0;
    int32_t x = px - camX, y = (up ? py - g->playerParams.height : py) - camY;
    if (!blink && g->playerFigure.layerCount == 0)
      next = (uint8_t)(next + bmSpriteSheetDrawFlipped(s, bmAnimFrame(s, &g->playerAnim), x, y, left, up, 1, s->bank, next, 8));
    else if (!blink)
    {
      // A figure: its layers front to back (figure.h), 12 hardware sprites in all.
      const BmSpriteSheet *sheets[BM_FIGURE_MAX_LAYERS];
      BmFigurePose pose;
      playerPose(g, sheets, &pose);
      uint8_t used = 0;
      for (uint8_t k = 0; k < pose.count && used < 12; k++)
      {
        uint8_t i = pose.order[k];
        const BmFigureLayerState *l = &g->playerLayers[i];
        int8_t bank = bmGameSheetBank(g, layerSheet(g, i), playerLayerPaletteNow(g, i));
        if ((l->flags & BM_LAYER_HIDDEN) || bank < 0) continue;
        used = (uint8_t)(used + bmSpriteSheetDrawFlipped(sheets[i], pose.frame[i], x + pose.x[i], y + pose.y[i], left, up, 1, (uint8_t)bank,
                                                         (uint8_t)(next + used), (uint8_t)(12 - used)));
      }
      next = (uint8_t)(next + used);
      // Over a puddle (reflect.h): mirrored in it, behind the room's main layer (priority 2), so
      // it shows only where the room is empty, the puddle.
      int32_t surface;
      uint8_t squash;
      if (!up && bmReflectMirrors(g, px - 16, px + 16, py, &surface, &squash))
        for (uint8_t k = 0; k < pose.count && next < BM_GAME_MAX_OAM; k++)
        {
          uint8_t i = pose.order[k];
          int8_t bank = bmGameSheetBank(g, layerSheet(g, i), playerLayerPaletteNow(g, i));
          if ((g->playerLayers[i].flags & BM_LAYER_HIDDEN) || bank < 0) continue;
          next = (uint8_t)(next + bmSpriteSheetDrawReflected(sheets[i], pose.frame[i], x + pose.x[i], y + pose.y[i], left, surface, squash,
                                                             BM_REFLECT_MATRIX(left), 2, (uint8_t)bank, next, (uint8_t)(BM_GAME_MAX_OAM - next)));
        }
    }
  }
  else
  {
    BmSprite s = {
      .x = (int16_t)(px - 8 - camX), .y = (int16_t)(py - 32 - camY),
      .shape = BM_SPRITE_TALL, .size = 2, .tile = g->builtinTile, .paletteBank = 14, .priority = 1,
      .hflip = g->player.facing < 0, .vflip = g->player.flipped, .visible = !blink,
    };
    bmSpriteSet(next++, &s);
  }

  next = (uint8_t)(next + bmActorsDraw(g, next, (uint8_t)(BM_GAME_MAX_OAM - next)));
  next = (uint8_t)(next + bmPuzzleDraw(g, next, (uint8_t)(BM_GAME_MAX_OAM - next)));
  next = (uint8_t)(next + bmGrappleDraw(g, next, (uint8_t)(BM_GAME_MAX_OAM - next)));
  next = (uint8_t)(next + bmEffectsDrawDoors(g, next, (uint8_t)(BM_GAME_MAX_OAM - next)));

  // Other objects: the sprite named like their class (its first animation, until scripts play
  // animations), or a placeholder frame for classes without graphics.
  for (uint16_t i = 0; i < g->objectCount && next < BM_GAME_MAX_OAM; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if ((o->kind != BM_OBJECT_OTHER && o->kind != BM_OBJECT_SAVE_POINT) || o->className[0] == 0) continue;
    if (bmActorsOwnObject(g, i)) continue;
    if (g->objectSheet[i] >= 0)
    {
      if (bmGameSheetBank(g, g->objectSheet[i], 0) < 0) continue;
      const BmSpriteSheet *s = &g->sheets[g->objectSheet[i]];
      next = (uint8_t)(next + bmSpriteSheetDraw(s, bmAnimFrameAt(s, 0, g->frame), o->x - camX, o->y - camY, false, 1, next,
                                                (uint8_t)(BM_GAME_MAX_OAM - next)));
      continue;
    }
    int32_t ox = o->x - 8 - camX, oy = (o->w || o->h ? o->y : o->y - 16) - camY;
    if (ox < -16 || ox > 240 || oy < -16 || oy > 160) continue;
    BmSprite b = { .x = (int16_t)ox, .y = (int16_t)oy, .shape = BM_SPRITE_SQUARE, .size = 1, .tile = (uint16_t)(g->builtinTile + 8),
                   .paletteBank = 15, .priority = 1, .visible = true };
    bmSpriteSet(next++, &b);
  }
  while (next < BM_GAME_MAX_OAM)
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

// The echo's delay lines.
static int8_t s_echo[2 * BM_SOUND_ECHO_MAX] BM_EWRAM_BSS;

// A room's acoustics (room-format.md §1): `acoustics` names a preset, `echo` (ms),
// `echo_feedback`, `echo_damping` and `muffle` (0-255) change it or make one of their own.
static const struct { const char *name; uint16_t echoMs; uint8_t feedback, damping, muffle; } s_acoustics[] = {
  { "dry",   0,   0,   0,   0 },     // no echo at all, not even the song's
  { "hall",  120, 150, 60,  0 },
  { "cave",  200, 120, 150, 0 },     // a far, dull echo
  { "water", 70,  70,  200, 200 },   // everything muffled
};

static bool numberProperty(const char *text, int32_t *pValue)
{
  if (text == NULL || *text < '0' || *text > '9') return false;
  int32_t v = 0;
  for (; *text >= '0' && *text <= '9'; text++) v = v * 10 + (*text - '0');
  *pValue = v;
  return true;
}

static uint8_t byteOf(int32_t v)
{
  return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static void roomAcoustics(BmGame *g)
{
  BmSoundRoom room = {0};
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    const BmObjectView *o = &g->objects[i];
    if (strcmp(o->className, "@room") != 0) continue;
    const char *preset = bmGameObjectProperty(g, o, "acoustics");
    int32_t echoMs = 0;
    for (uint32_t k = 0; preset != NULL && k < sizeof(s_acoustics) / sizeof(s_acoustics[0]); k++)
    {
      if (strcmp(preset, s_acoustics[k].name) != 0) continue;
      room.on = 1;
      echoMs = s_acoustics[k].echoMs;
      room.feedback = s_acoustics[k].feedback;
      room.damping = s_acoustics[k].damping;
      room.muffle = s_acoustics[k].muffle;
    }
    int32_t v;
    if (numberProperty(bmGameObjectProperty(g, o, "echo"), &v)) { room.on = 1; echoMs = v; }
    if (numberProperty(bmGameObjectProperty(g, o, "echo_feedback"), &v)) { room.on = 1; room.feedback = byteOf(v); }
    if (numberProperty(bmGameObjectProperty(g, o, "echo_damping"), &v)) { room.on = 1; room.damping = byteOf(v); }
    if (numberProperty(bmGameObjectProperty(g, o, "muffle"), &v)) { room.on = 1; room.muffle = byteOf(v); }
    int32_t samples = echoMs * BM_SOUND_RATE / 1000;
    room.echoDelay = (uint16_t)(samples > BM_SOUND_ECHO_MAX ? BM_SOUND_ECHO_MAX : samples);
    break;
  }
  bmSoundSetRoom(&g->sound, &room);
}

// The sound engine reads songs in place: a new pack means starting over. Then the room's song,
// unless it is the one playing, and its acoustics.
static void enterRoomSound(BmGame *g)
{
  if (g->pSoundFrom != g->pPack->pData || g->sound.pPack != g->pPack)
  {
    bmSoundInit(&g->sound, g->pPack, s_echo);
    g->pSoundFrom = g->pPack->pData;
    bmHalAudioStart();
  }
  const char *music = g->room.musicId != 0xFFFF ? bmPackString(g->pPack, g->room.musicId) : NULL;
  // "none": what plays fades away (a boss's room: the boss brings its own); a song starts
  // (full volume, all its layers) unless it is the one playing.
  if (music != NULL && strcmp(music, "none") == 0) bmSoundFadeMusic(&g->sound, 0, BM_GAME_MUSIC_FADE, true);
  else if (music != NULL)
  {
    bmSoundSetIntensity(&g->sound, BM_SOUND_LAYERS - 1);
    bmSoundPlayMusic(&g->sound, music);
  }
  roomAcoustics(g);
}

static void enterBreakables(BmGame *g);

// What the room shows from the start into sprite memory now, not on its first frame: its
// objects' sprites (in their palettes), the puzzle's. Effects and objects made later load
// when they are first drawn.
// Whether sheet `sheet` in `palette` is in sprite memory with a bank of its own.
static bool sheetBanked(const BmGame *g, int8_t sheet, uint8_t palette)
{
  for (uint8_t b = 0; b < BM_GAME_BANKS; b++)
    if (g->bankSheet[b] == (uint8_t)sheet && g->bankPalette[b] == palette) return true;
  return false;
}

// What the room shows from the start into sprite memory now, not on its first frame: its
// objects' sprites (in their palettes), the puzzle's. What earlier rooms loaded stays (a room
// change unpacks only what is new); only if the room needs more tiles or banks than are left
// is everything but the player's forgotten first. Effects and objects made later load when
// they are first drawn.
static void preloadSprites(BmGame *g)
{
  int8_t sheets[24];
  uint8_t palettes[24], n = 0;
  const BmActors *a = &g->actors;
  g->usedCount = 0;   // a new room: what it draws is counted anew
  for (uint16_t i = 0; i < g->objectCount && n < 24; i++)
    if (g->objectSheet[i] >= 0) sheets[n] = g->objectSheet[i], palettes[n++] = 0;
  for (uint8_t i = 0; a->hasModule && i < a->count && n < 24; i++)
    if (!a->gone[i] && a->sheet[i] >= 0) sheets[n] = a->sheet[i], palettes[n++] = a->palette[i];
  if (g->puzzle.sheet >= 0 && n < 24) sheets[n] = g->puzzle.sheet, palettes[n++] = 0;
  // The grapple's anchors, and the extra layers of kinds (the Iron Bat's armour).
  if (g->grapple.anchorCount != 0 && g->grapple.sheet >= 0 && n < 24) sheets[n] = g->grapple.sheet, palettes[n++] = 0;
  for (uint8_t i = 0; a->hasModule && i < a->count && n < 24; i++)
    if (!a->gone[i] && a->extraSheet[i] >= 0) sheets[n] = a->extraSheet[i], palettes[n++] = 0;
  // What is new: tiles of sheets not loaded, banks for pairs without one (each counted once).
  uint32_t tiles = 0, banks = 0;
  for (uint8_t k = 0; k < n; k++)
  {
    bool seen = false;
    for (uint8_t j = 0; j < k && !seen; j++) seen = sheets[j] == sheets[k] && palettes[j] == palettes[k];
    if (seen || sheetBanked(g, sheets[k], palettes[k])) continue;
    banks++;
    bool sheetSeen = false;
    for (uint8_t j = 0; j < k && !sheetSeen; j++) sheetSeen = sheets[j] == sheets[k];
    if (!sheetSeen && g->sheetTile[sheets[k]] == BM_GAME_NO_TILES) tiles += g->sheets[sheets[k]].tileCount;
  }
  uint32_t freeBanks = 0;
  for (uint8_t b = 0; b < BM_GAME_BANKS; b++) freeBanks += g->bankSheet[b] == 0xFF;
  if (g->tileNext + tiles > BM_GAME_OBJ_TILES || banks > freeBanks) forgetSprites(g);
  for (uint8_t k = 0; k < n; k++) bmGameSheetBank(g, sheets[k], palettes[k]);
}

static BmResult enterRoom(BmGame *g, uint32_t index, bool fresh)
{
  // A pack pushed over the link: the UI's font is somewhere else now.
  if (g->ui.ready && g->ui.pPackData != g->pPack->pData) bmUiRebind(&g->ui, g->pPack);
  // The room being left takes its attachments with it (they get detached() while it is
  // still there). A state just loaded (not fresh) brings its own.
  if (fresh) bmAttachmentsLeaveRoom(g);
  memset(g->effectAge, 0, sizeof(g->effectAge));
  if (fresh) bmHitsClear(g);
  BmResult r = bmRoomOpen(&g->room, g->pPack, index);
  if (r != BM_SUCCESS) return r;
  g->roomIndex = index;
  if (!bmCollisionInit(&g->collision, g->room.pCollision, g->room.collisionSize, (g->room.flags & 1) != 0)) return BM_ERROR_INVALID_ARGUMENT;
  g->physics.gravity = g->room.gravity;
  g->physics.maxFallSpeed = g->room.maxFallSpeed;
  g->physics.waterGravity = g->room.waterGravity;
  g->physics.waterMaxFallSpeed = g->room.waterMaxFallSpeed;
  readObjects(g);
  g->puzzle.blockCount = g->puzzle.gateCount = g->puzzle.plateCount = 0;   // the last room's are gone
  enterBreakables(g);
  if (fresh)
  {
    bmEffectsEnterRoom(g);
    // The last room's notice goes (its title); a room with a `title` says it in the corner when
    // it is entered.
    if (g->noticeTimer != 0) g->noticeTimer = 1;
    for (uint16_t i = 0; i < g->objectCount; i++)
    {
      const BmObjectView *o = &g->objects[i];
      if (strcmp(o->className, "@room") != 0) continue;
      for (uint16_t k = 0; k < o->propertyCount; k++)
      {
        const char *key = bmPackString(g->pPack, bmRead16(o->pProperties + k * 4));
        if (key != NULL && strcmp(key, "title") == 0) bmGameNotice(g, 0, bmRead16(o->pProperties + k * 4 + 2), 1);
      }
    }
  }
  loadSprites(g);
  bmPuzzleEnterRoom(g, fresh);
  bmGrappleEnterRoom(g, fresh);
  bmCommandsPrepare(g);
  bmActorsEnterRoom(g, fresh);
  if (fresh) bmAttachmentsEnterRoom(g);
  bmActorsVerifyInUse(g);   // the rest of the scripts is verified in spare time (main.c)
  enterRoomSound(g);
  bmMapEnterRoom(g);
  preloadSprites(g);
  BmResult shown = bmRoomShow(&g->room, 0, 0);
  if (shown == BM_SUCCESS) bmLightEnterRoom(g);
  if (shown == BM_SUCCESS) bmReflectEnterRoom(g);
  return shown;
}

static void readOptions(BmGame *g);

BmResult bmGameStart(BmGame *g, const BmPack *pPack, const char *roomId)
{
  memset(g, 0, sizeof(*g));
  g->pPack = pPack;
  g->equipPalette = 0xFFFF;
  bmBodyDefaultParams(&g->playerParams);
  int32_t index = roomId ? bmGameFindRoom(pPack, roomId) : 0;
  if (index < 0) return BM_ERROR_INVALID_ARGUMENT;
  bmVideoInit();
  bmUiInit(&g->ui, pPack, NULL);   // without a font the UI draws nothing
  readOptions(g);
  g->actors.seed = 1;
  g->health = (int16_t)bmGameMaxHealth(g);
  g->mana = (int16_t)bmGameMaxMana(g);
  g->striker = 0xFF;
  g->noticeText = 0xFFFF;
  g->hudDirty = true;
  bmDataPrepare(pPack);   // the record tables, checked before the first frame needs them
  {
    // The save game's schema hash now, not in the frame of the first save point.
    BmStateBlock block = bmGameStateBlock(g);
    bmStateSchemaHash(&block, 1);
  }
  BmResult r = enterRoom(g, (uint32_t)index, true);
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
  // Until a save point is reached, the Return Feather leads to the start.
  g->savedRoom = (uint32_t)index;
  g->savedX = BM_FIX_INT(g->player.x);
  g->savedY = BM_FIX_INT(g->player.y);
  updateCamera(g);
  updateSprites(g);
  bmEffectsPresent(g);
  bmLightPresent(g);
  bmReflectPresent(g);
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
  g->doorSeq = 0;   // a door being walked through (fallen into a pit before it) is left
}

// The walk through a door with a panel (effects.h): side doors are walked through, a door in
// the back wall opens in front of the player; A, B or Start ends it at once.
#define DOOR_WALK_FRAMES 30
#define DOOR_FACING_FRAMES 20
#define DOOR_CLOSE_FRAMES 16

static void goThroughDoor(BmGame *g, const BmObjectView *door);

static void useDoor(BmGame *g, uint16_t index)
{
  if (index >= g->objectCount || g->doorSeq != 0) return;
  if (g->objects[index].kind == BM_OBJECT_DOOR && g->objectSheet[index] >= 0)
  {
    g->doorSeq = (uint8_t)(index + 1);
    g->doorTimer = 0;
    g->doorArrive = 0;
    return;
  }
  BmObjectView door = g->objects[index];   // the room's objects change when the room does
  goThroughDoor(g, &door);
}

// Whether the pack leaves out chunks (a trimmed one: rooms can come later over the link).
static bool packHasAbsent(const BmPack *pPack)
{
  for (uint32_t i = 0; i < bmPackChunkCount(pPack); i++)
  {
    BmChunkEntry e;
    if (bmPackGetChunk(pPack, i, &e) && (e.flags & BM_PACK_ENTRY_ABSENT)) return true;
  }
  return false;
}

bool bmGameWaitingForRoom(const BmGame *g, bool *pMenu, uint8_t *pChoice)
{
  if (pMenu) *pMenu = g->wantTimer >= BM_GAME_ROOM_WAIT_FRAMES;
  if (pChoice) *pChoice = g->wantChoice;
  return g->wantRoom != 0;
}

static void goThroughDoor(BmGame *g, const BmObjectView *door)
{
  const char *target = bmGameObjectProperty(g, door, "target_room");
  const char *targetDoor = bmGameObjectProperty(g, door, "target_door");
  int32_t index = target ? bmGameFindRoom(g->pPack, target) : -1;
  if (index < 0 && target != NULL && packHasAbsent(g->pPack))
  {
    // A room the pack left out: ask for it over the link and hold still meanwhile.
    for (uint16_t i = 0; i < door->propertyCount; i++)
    {
      const char *k = bmPackString(g->pPack, bmRead16(door->pProperties + i * 4));
      if (k != NULL && strcmp(k, "target_room") == 0) g->wantRoom = (uint16_t)(bmRead16(door->pProperties + i * 4 + 2) + 1);
    }
    g->wantDoor = 0;
    for (uint16_t i = 0; i < g->objectCount; i++)
      if (g->objects[i].persistentId == door->persistentId && g->objects[i].kind == BM_OBJECT_DOOR) g->wantDoor = (uint8_t)(i + 1);
    g->wantTimer = 0;
    g->wantChoice = 0;
    g->player.vx = 0;
    return;
  }
  if (index < 0) return;
  bm_fix vx = g->player.vx;
  if (enterRoom(g, (uint32_t)index, true) != BM_SUCCESS) return;
  g->roomChanges++;
  g->doorCooldown = 20;

  BmObjectView arrive;
  int32_t x = 32, y = 32;
  if (targetDoor && findObject(g, BM_OBJECT_DOOR, targetDoor, &arrive))
  {
    // Come through it: it closes behind the player.
    for (uint16_t i = 0; i < g->objectCount; i++)
      if (g->objects[i].kind == BM_OBJECT_DOOR && g->objectSheet[i] >= 0 && g->objects[i].name == arrive.name)
      {
        g->doorArrive = (uint8_t)(i + 1);
        g->doorArriveTimer = DOOR_CLOSE_FRAMES;
      }
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

// An effect: sheet `sheet`'s first animation once at room pixel (x, y), in a free place, else
// the oldest one's.
static void effectAt(BmGame *g, int8_t sheet, int32_t x, int32_t y)
{
  if (sheet < 0) return;
  uint8_t k = 0;
  for (uint8_t i = 0; i < BM_GAME_EFFECTS; i++)
  {
    if (g->effectAge[i] == 0) { k = i; break; }
    if (g->effectAge[i] > g->effectAge[k]) k = i;
  }
  g->effectSheet[k] = sheet;
  g->effectX[k] = (int16_t)x;
  g->effectY[k] = (int16_t)y;
  g->effectAge[k] = 1;
}

void bmGameSpark(BmGame *g, int32_t x, int32_t y)
{
  effectAt(g, g->sparkSheet, x, y);
}

void bmGameEffect(BmGame *g, uint16_t name, int32_t x, int32_t y)
{
  effectAt(g, sheetNamed(g, name), x, y);
}

// --- Breakable walls and floors -------------------------------------------------------------

static uint32_t breakKey(const BmGame *g, uint16_t i)
{
  return ((g->roomIndex & 0xFF) << 24) | ((uint32_t)g->objects[i].persistentId << 8) | 0xFF;
}

// What a Breakable leaves: its `cell` in its cells, a hole in the room's pictures.
static void applyBreak(BmGame *g, uint16_t i)
{
  const BmObjectView *o = &g->objects[i];
  g->objectBreak[i] |= 0x80;
  if (g->breakCount >= BM_ROOM_MAX_HOLES) return;
  const char *cell = bmGameObjectProperty(g, o, "cell");
  uint8_t shape = 0;
  if (cell != NULL && strcmp(cell, "top") == 0) shape = BM_SHAPE_FULL | BM_CELL_SOLID_TOP;
  else if (cell != NULL && strcmp(cell, "full") == 0) shape = BM_SHAPE_FULL | BM_CELL_SOLID_TOP | BM_CELL_SOLID_SIDES;
  int32_t c = g->collision.cellSize;
  BmCellOverride *r = &g->breakCells[g->breakCount++];
  r->x0 = (int16_t)(o->x / c);
  r->y0 = (int16_t)(o->y / c);
  r->x1 = (int16_t)((o->x + o->w + c - 1) / c);
  r->y1 = (int16_t)((o->y + o->h + c - 1) / c);
  r->shape = shape;
  r->material = 0;
  bmPuzzleOverrides(g);
  bmRoomAddHole(&g->room, o->x, o->y, o->w, o->h);
}

// The room's Breakables when it is entered: what breaks them, and the broken ones broken.
static void enterBreakables(BmGame *g)
{
  g->breakCount = 0;
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    g->objectBreak[i] = 0;
    if (g->objects[i].kind != BM_OBJECT_BREAKABLE) continue;
    const char *by = bmGameObjectProperty(g, &g->objects[i], "by");
    g->objectBreak[i] = (uint8_t)(by != NULL && strcmp(by, "pierce") == 0 ? BM_BREAK_PIERCE : by != NULL && strcmp(by, "above") == 0 ? BM_BREAK_ABOVE : BM_BREAK_ANY);
    bool found = false;
    bmActorsStoreGet(g, breakKey(g, i), &found);
    if (found) applyBreak(g, i);
  }
}

static uint32_t recordKey(const char *s)
{
  uint32_t h = 2166136261u;
  for (; s != NULL && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

void bmGameBreak(BmGame *g, uint16_t i)
{
  if (i >= g->objectCount || g->objects[i].kind != BM_OBJECT_BREAKABLE || (g->objectBreak[i] & 0x80)) return;
  const BmObjectView *o = &g->objects[i];
  if (!bmActorsStorePut(g, breakKey(g, i), 1)) bm_script_errors++;   // more kept than this version holds
  applyBreak(g, i);
  // Debris from every cell of it (as many as there are places for).
  for (int32_t y = o->y; y < o->y + o->h; y += 16)
    for (int32_t x = o->x; x < o->x + o->w; x += 16)
      effectAt(g, g->debrisSheet, x + 8, y + 8);
  bmSoundPlayEffect(&g->sound, "crumble");
  const char *drop = bmGameObjectProperty(g, o, "drop");
  if (drop != NULL && drop[0] != 0)
    bmActorsSpawnNamed(g, "Drop", BM_FIX(o->x + o->w / 2), BM_FIX(o->y + o->h), recordKey(drop));
}

void bmGameHurtPlayer(BmGame *g, int32_t damage)
{
  // As if from straight ahead: pushed back.
  bmGameStrikePlayer(g, damage, g->player.x + g->player.facing);
}

void bmGameEquipmentChanged(BmGame *g)
{
  BmInventory *inv = &g->inventory;
  memset(inv->statEquip, 0, sizeof(inv->statEquip));
  g->equipPalette = 0xFFFF;
  // What is equipped, then the abilities owned.
  for (uint8_t s = 0; s < BM_INVENTORY_EQUIP + BM_INVENTORY_SLOTS; s++)
  {
    BmDataTable table;
    const uint8_t *record;
    bool owned = s >= BM_INVENTORY_EQUIP;
    uint32_t key = owned ? (s - BM_INVENTORY_EQUIP < inv->count ? inv->key[s - BM_INVENTORY_EQUIP] : 0) : inv->equipped[s];
    if (key == 0 || !bmDataFindRecord(g->pPack, key, &table, &record)) continue;
    if (owned != ((bmDataTableRoles(&table) & BM_DATA_ROLE_ABILITY) != 0)) continue;
    // Equipment that shows (layered-sprites.md §5): a field `look_palette` names a palette the
    // player's layers take while it is worn (the last slot that has one).
    for (uint16_t f = 0; !owned && f < table.fieldCount; f++)
    {
      BmDataField field = bmDataTableField(&table, f);
      const char *n = bmPackString(g->pPack, field.name);
      if (field.type != BM_DATA_NAME || n == NULL || strcmp(n, "look_palette") != 0) continue;
      uint16_t palette = (uint16_t)bmDataRead(&table, record, field.offset, BM_DATA_NAME);
      const char *pn = bmPackString(g->pPack, palette);
      if (pn != NULL && strcmp(pn, "default") != 0) g->equipPalette = palette;
    }
    int32_t stats[BM_DATA_MAX_STATS];
    uint32_t n = bmDataRecordStats(&table, record, stats);
    for (uint32_t i = 0; i < n && i < BM_INVENTORY_STATS; i++)
    {
      int32_t v = inv->statEquip[i] + stats[i];
      inv->statEquip[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
  }
}

void bmGameInventoryChanged(BmGame *g)
{
  bmGameEquipmentChanged(g);
  bmAttachmentsAbilities(g);
}

int32_t bmGameMaxHealth(const BmGame *g)
{
  int32_t max = BM_GAME_PLAYER_HEALTH + bmInventoryStat(&g->inventory, BM_STAT_MAX_HEALTH);
  return max < 1 ? 1 : max > 32767 ? 32767 : max;
}

void bmGameSay(BmGame *g, uint16_t speaker, uint16_t line)
{
  bmGameSayWith(g, speaker, line, 0xFFFF);
}

bool bmGameRecordField(const BmGame *g, uint32_t key, const char *name, int32_t *pValue)
{
  BmDataTable table;
  const uint8_t *record;
  if (key == 0 || !bmDataFindRecord(g->pPack, key, &table, &record)) return false;
  for (uint16_t f = 0; f < table.fieldCount; f++)
  {
    BmDataField field = bmDataTableField(&table, f);
    const char *n = bmPackString(g->pPack, field.name);
    if (n == NULL || strcmp(n, name) != 0) continue;
    *pValue = bmDataRead(&table, record, field.offset, (BmDataType)field.type);
    return true;
  }
  return false;
}

void bmGameSayWith(BmGame *g, uint16_t speaker, uint16_t line, uint16_t portrait)
{
  bmGameSpeak(g, speaker, line, portrait, 0xFFFF);
}

void bmGameSpeak(BmGame *g, uint16_t speaker, uint16_t line, uint16_t portrait, uint16_t voice)
{
  g->dialogue = 1;
  g->dialogueSpeaker = speaker;
  g->dialogueLine = line;
  g->dialoguePortrait = portrait;
  g->dialogueVoice = voice;
  g->dialogueShown = 0;   // it appears letter by letter (dialogueFrame)
  g->dialogueRow = 0;
  bmGameDrawDialogue(g);
}

// Where the line goes in the box: right of a portrait (drawn by updateSprites over the box's
// left end) if there is one.
static int dialogueIndent(const BmGame *g)
{
  return g->dialoguePortrait != 0xFFFF && sheetNamed(g, g->dialoguePortrait) >= 0 ? BM_GAME_PORTRAIT_CELLS : 0;
}

// The line is longer than the box and the box is full: a mark at its bottom right says A
// scrolls on.
static void dialogueMarker(BmGame *g)
{
  bool on = g->dialogueMore && g->dialogueShown >= g->dialogueLength;
  // U+25BC, a filled triangle pointing down; it bobs: every 16 frames a pixel lower (U+E800,
  // the same glyph lower in the engine's font) and back.
  bool low = (g->frame / 16) & 1;
  bmUiLabel(&g->ui, 28, 18, !on ? " " : low ? "\xEE\xA0\x80" : "\xE2\x96\xBC");
}

void bmGameDrawDialogue(BmGame *g)
{
  if (g->dialogue == 0 || g->screens.count != 0) return;   // drawn when the menu closes
  bmUiClearRect(&g->ui, 0, 15, 32, 5);
  g->noticeWidth = 0;   // the box covers the notice's place
  bmUiFrame(&g->ui, 0, 15, 30, 5);
  int x = dialogueIndent(g);
  bmUiLabel(&g->ui, 1 + x, 16, bmText(g->pPack, g->language, g->dialogueSpeaker));
  // The letters shown so far (from the row scrolled to); and how many there are up to the
  // box's last row, and whether more follow.
  int32_t count = 0;
  bmUiTextScroll(&g->ui, 2 + x, 17, 26 - x, 2, g->dialogueLine, NULL, 0, g->dialogueRow, 0, g->dialogueShown, &count, &g->dialogueMore);
  g->dialogueLength = (uint8_t)(count > 255 ? 255 : count);
  if (g->dialogueShown > g->dialogueLength) g->dialogueShown = g->dialogueLength;
  dialogueMarker(g);
  bmUiShow(&g->ui, true);
}

// Once a frame while the box is shown: the next letter appears, and with every second letter
// (not a space or a mark) the speaker's voice, a little higher or lower by the letter.
static void dialogueFrame(BmGame *g)
{
  if (g->dialogue == 0 || g->screens.count != 0 || g->dialogueShown >= g->dialogueLength) return;
  int32_t n = g->dialogueShown++;
  int x = dialogueIndent(g);
  bmUiTextScroll(&g->ui, 2 + x, 17, 26 - x, 2, g->dialogueLine, NULL, 0, g->dialogueRow, n, n + 1, NULL, NULL);
  if (g->dialogueShown >= g->dialogueLength) dialogueMarker(g);
  if (g->dialogueVoice == 0xFFFF || (n & 1) != 0) return;
  // The letter: the n-th character of the line as shown.
  const char *p = bmText(g->pPack, g->language, g->dialogueLine);
  uint32_t c = 0;
  for (int32_t i = 0; p != NULL && *p && i <= n; i++) c = bmTextNextChar(&p);
  bool letter = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0;
  if (letter) bmSoundPlayEffectAt(&g->sound, bmPackString(g->pPack, g->dialogueVoice), (int8_t)((int32_t)(c * 7u % 5u) - 2));
}

void bmGameSayLine(BmGame *g, uint32_t line)
{
  int32_t speaker = 0, says = 0xFFFF, name = 0xFFFF, portrait = 0xFFFF;
  bmGameRecordField(g, line, "speaker", &speaker);
  bmGameRecordField(g, line, "says", &says);
  bmGameRecordField(g, (uint32_t)speaker, "name", &name);
  bmGameRecordField(g, (uint32_t)speaker, "portrait", &portrait);
  int32_t voice = 0xFFFF;
  bmGameRecordField(g, (uint32_t)speaker, "voice", &voice);
  bmGameSpeak(g, (uint16_t)name, (uint16_t)says, (uint16_t)portrait, (uint16_t)voice);
}

// A pressed: the dialogue box goes.
static void endDialogue(BmGame *g)
{
  g->dialogue = 0;
  bmUiClearRect(&g->ui, 0, 15, 32, 5);
  g->noticeDrawn = false;   // a notice still running comes back
  g->noticeWidth = 0;
}

// --- HUD and notices ---------------------------------------------------------------------------

// The record's text field `title` (string id), or 0xFFFF.
static uint16_t titleOf(const BmGame *g, uint32_t key)
{
  BmDataTable table;
  const uint8_t *record;
  if (key == 0 || !bmDataFindRecord(g->pPack, key, &table, &record)) return 0xFFFF;
  for (uint16_t f = 0; f < table.fieldCount; f++)
  {
    BmDataField field = bmDataTableField(&table, f);
    const char *name = bmPackString(g->pPack, field.name);
    if (name != NULL && strcmp(name, "title") == 0) return (uint16_t)bmDataRead(&table, record, field.offset, (BmDataType)field.type);
  }
  return 0xFFFF;
}

void bmGameNotice(BmGame *g, uint32_t key, uint16_t nameId, int32_t count)
{
  // Only kept here: finding the record's title takes time, the frame an item comes has its
  // scripts' work already; hudFrame looks it up later.
  g->noticeKey = key;
  g->noticeText = nameId;
  g->noticeTitled = false;
  g->noticeCount = (uint8_t)(count < 1 ? 1 : count > 99 ? 99 : count);
  g->noticeTimer = BM_GAME_NOTICE_FRAMES;
  g->noticeDrawn = false;
}

// Characters of a UTF-8 text (cells it takes).
static int cellsOf(const char *t)
{
  int n = 0;
  for (; t != NULL && *t; t++)
    if (((uint8_t)*t & 0xC0) != 0x80) n++;
  return n;
}

// Once a frame without a screen: the HUD where its numbers changed, the notice while it runs.
static void hudFrame(BmGame *g)
{
  BmUi *ui = &g->ui;
  if (!ui->ready) return;
  bool whole = g->hudDirty;
  if (g->hudDirty)
  {
    g->hudDirty = false;
    g->noticeDrawn = false;
    g->noticeWidth = 0;
    for (int i = 0; i < 4; i++) g->hudShown[i] = -1;
  }
  if (!g->hudHidden)
  {
    int16_t now[4] = { g->health, (int16_t)bmGameMaxHealth(g), g->mana, (int16_t)bmGameMaxMana(g) };
    if (g->hudShown[0] < 0)
    {
      bmUiFrame(ui, 0, 0, 13, 4);
      bmUiLabel(ui, 1, 1, "HP");
      bmUiLabel(ui, 1, 2, "MP");
    }
    if (now[0] != g->hudShown[0] || now[1] != g->hudShown[1])
    {
      bmUiGauge(ui, 4, 1, 5, now[0], now[1]);
      bmUiNumber(ui, 9, 1, now[0], 3);
    }
    if (now[2] != g->hudShown[2] || now[3] != g->hudShown[3])
    {
      bmUiGauge2(ui, 4, 2, 5, now[2], now[3]);
      bmUiNumber(ui, 9, 2, now[2], 3);
    }
    for (int i = 0; i < 4; i++) g->hudShown[i] = now[i];
    // The small map at the top right (map.h), if the game's map has one.
    bmMapHud(g, whole);
    bmUiShow(ui, true);
  }
  // The notice, unless the dialogue box is where it goes.
  if (g->noticeTimer != 0) g->noticeTimer--;
  if (g->dialogue != 0) return;
  if (g->noticeTimer == 0)
  {
    if (g->noticeWidth != 0) bmUiClearRect(ui, 0, 17, g->noticeWidth, 3);
    g->noticeWidth = 0;
    return;
  }
  // Spread over frames on the GBA: not in the frame the item came (its scripts have done
  // enough), the record's title found in the next, drawn in the one after (new glyphs cost).
  if (g->noticeDrawn || g->noticeTimer >= BM_GAME_NOTICE_FRAMES - 1) return;
  if (g->noticeKey != 0)
  {
    uint16_t title = titleOf(g, g->noticeKey);
    g->noticeTitled = title != 0xFFFF;
    if (title != 0xFFFF) g->noticeText = title;
    g->noticeKey = 0;
    return;
  }
  bool titleIsName = !g->noticeTitled;
  const char *text = g->noticeText != 0xFFFF ? bmText(g->pPack, g->language, g->noticeText) : NULL;
  // A name without a record (give_item(#heart_max_up)) shown tidily: "Heart max up".
  char tidy[24];
  if (text != NULL && titleIsName)
  {
    int n = 0;
    for (; text[n] != 0 && n < (int)sizeof(tidy) - 1; n++) tidy[n] = text[n] == '_' ? ' ' : text[n];
    tidy[n] = 0;
    if (tidy[0] >= 'a' && tidy[0] <= 'z') tidy[0] = (char)(tidy[0] - 'a' + 'A');
    text = tidy;
  }
  char count[8] = "";
  if (g->noticeCount > 1)
  {
    count[0] = ' ';
    count[1] = 'x';
    int n = 2;
    if (g->noticeCount >= 10) count[n++] = (char)('0' + g->noticeCount / 10);
    count[n++] = (char)('0' + g->noticeCount % 10);
    count[n] = 0;
  }
  int w = cellsOf(text) + cellsOf(count) + 2;
  if (w > 30) w = 30;
  if (g->noticeWidth > w) bmUiClearRect(ui, 0, 17, g->noticeWidth, 3);
  bmUiFrame(ui, 0, 17, w, 3);
  int used = text != NULL ? bmUiLabel(ui, 1, 18, text) : 0;
  bmUiLabel(ui, 1 + used, 18, count);
  g->noticeWidth = (uint8_t)w;
  g->noticeDrawn = true;
  bmUiShow(ui, true);
}

bool bmGameStrikePlayer(BmGame *g, int32_t amount, int32_t fromX)
{
  return bmGameStrikePlayerWith(g, amount, fromX, 0, 0xFF);
}

int32_t bmGameMaxMana(const BmGame *g)
{
  int32_t max = BM_GAME_PLAYER_MANA + bmInventoryStat(&g->inventory, BM_STAT_MAX_MANA);
  return max < 0 ? 0 : max > 32767 ? 32767 : max;
}

void bmGameReturnToSave(BmGame *g)
{
  g->returnPending = 1;
}

// The Return Feather's journey (bmGameReturnToSave), at a frame's start.
static void returnToSave(BmGame *g)
{
  g->returnPending = 0;
  while (g->screens.count != 0) bmScreensClose(g);
  // With the wave, as every teleport (effects.h).
  bmEffectsTeleport(g, g->savedRoom, g->savedX, g->savedY);
}

void bmGameMovePlayer(BmGame *g, uint32_t roomIndex, int32_t x, int32_t y)
{
  if (roomIndex != g->roomIndex)
  {
    if (enterRoom(g, roomIndex, true) != BM_SUCCESS) return;
    g->roomChanges++;
  }
  int8_t facing = g->player.facing;
  bmBodyPlace(&g->player, x, y);
  g->player.facing = facing;
  g->doorCooldown = 20;
  updateCamera(g);
}

bool bmGameUseQuick(BmGame *g, uint8_t slot)
{
  if (slot >= BM_INVENTORY_QUICK || g->inventory.quick[slot] == 0) return false;
  return bmAttachmentsUse(g, g->inventory.quick[slot]);
}

bool bmGameStrikePlayerWith(BmGame *g, int32_t amount, int32_t fromX, uint8_t element, uint8_t striker)
{
  if (amount <= 0 || g->hurtTimer != 0) return false;
  // The damage rule (first-items.md §1): less the player's defence, at least 1; then halved
  // if something the player wears resists the element, at least 1.
  amount -= bmInventoryStat(&g->inventory, BM_STAT_DEFENCE);
  if (amount < 1) amount = 1;
  if (element != 0 && element < BM_GAME_ELEMENTS && g->resists[element] != 0) amount = amount / 2 < 1 ? 1 : amount / 2;
  // The attachments' filters (a shield, Second Wind): nothing left, no strike.
  amount = bmAttachmentsFilter(g, BM_SCRIPT_EVENT_damage_in, amount, element);
  if (amount <= 0) return false;
  bmSoundPlayEffect(&g->sound, "hurt");
  g->hurts++;
  g->hurtTimer = BM_HITS_PLAYER_INVULNERABLE;
  // The push: the hit's own (hit_knock), else 2 away and 3 up.
  g->player.vy = -(g->strikeKnockY != 0 ? g->strikeKnockY : BM_FIX(3));
  int32_t away = fromX > g->player.x ? -1 : fromX < g->player.x ? 1 : -g->player.facing;
  g->player.vx = away * (g->strikeKnockX != 0 ? g->strikeKnockX : BM_FIX(2));
  // A struck figure's own hits end.
  for (uint8_t k = 0; k < BM_HITS_MAX; k++)
    if (g->hits.source[k] == BM_HITS_PLAYER) bmHitsDisarm(g, BM_HITS_PLAYER, g->hits.name[k]);
  g->health = (int16_t)(g->health - (amount > g->health ? g->health : amount));
  // Who struck is known to the attachments' hit() (strike_back: the Thorn Band).
  g->striker = striker;
  bmAttachmentsPlayerHit(g, amount);
  g->striker = 0xFF;
  if (g->health <= 0)
  {
    respawn(g);
    g->health = (int16_t)bmGameMaxHealth(g);
  }
  return true;
}

void bmGameFrame(BmGame *g, uint16_t keys)
{
  g->frame++;
  uint16_t previous = g->keysPrevious;
  uint16_t pressed = (uint16_t)(keys & ~previous);
  g->keysPrevious = keys;
  if (g->returnPending) returnToSave(g);
  // Hit stop: the game holds still for a moment after a heavy hit (pictures and sound go on).
  if (g->hitStop != 0)
  {
    g->hitStop--;
    updateSprites(g);
    bmUiPresent(&g->ui);
    int8_t *l, *r;
    bmHalAudioBuffers(&l, &r);
    bmSoundFrame(&g->sound, l, r, BM_HAL_AUDIO_FRAME_SAMPLES);
    return;
  }
  // Waiting for a room over the link (a door into one the pack left out): the game holds still.
  if (g->wantRoom != 0)
  {
    const char *target = bmPackString(g->pPack, (uint16_t)(g->wantRoom - 1));
    if (target != NULL && bmGameFindRoom(g->pPack, target) >= 0 && g->wantDoor != 0 && g->wantDoor <= g->objectCount)
    {
      // It came (a pack pushed hot): through the door.
      BmObjectView door = g->objects[g->wantDoor - 1];
      g->wantRoom = 0;
      g->hudDirty = true;
      goThroughDoor(g, &door);
    }
    else
    {
      if (g->wantTimer < 0xFFFF) g->wantTimer++;
      if (g->wantTimer >= BM_GAME_ROOM_WAIT_FRAMES)
      {
        // No answer: try again, or cancel (back into the room, away from the door).
        if (pressed & (BM_KEY_UP | BM_KEY_DOWN)) g->wantChoice ^= 1;
        if (pressed & BM_KEY_B) g->wantChoice = 1;
        if (pressed & (BM_KEY_A | BM_KEY_START | BM_KEY_B))
        {
          if (g->wantChoice == 0) g->wantTimer = 0;
          else
          {
            const BmObjectView *d = g->wantDoor != 0 && g->wantDoor <= g->objectCount ? &g->objects[g->wantDoor - 1] : NULL;
            g->wantRoom = 0;
            g->hudDirty = true;
            g->doorCooldown = 60;
            if (d != NULL)
            {
              // Back into the room, away from the door (an east door: to its left; a west one:
              // to its right; one in the back wall: where they stand).
              uint8_t side = g->objectDoorSide[g->wantDoor - 1];
              int32_t x = side == 1 ? d->x - 20 : side == 2 ? d->x + d->w + 20 : BM_FIX_INT(g->player.x);
              int8_t facing = side == 1 ? -1 : side == 2 ? 1 : g->player.facing;
              int32_t sx = g->player.safeX, sy = g->player.safeY;
              bmBodyPlace(&g->player, x, d->y + d->h);
              g->player.safeX = sx;
              g->player.safeY = sy;
              g->player.facing = facing;
            }
          }
        }
      }
      updateSprites(g);
      int8_t *l, *r;
      bmHalAudioBuffers(&l, &r);
      bmSoundFrame(&g->sound, l, r, BM_HAL_AUDIO_FRAME_SAMPLES);
      return;
    }
  }
  // An item obtained last frame: its sound now (the frame it came had the item's work already).
  if (g->pickupSound)
  {
    g->pickupSound = 0;
    bmSoundPlayEffect(&g->sound, "pickup");
  }
  bmEffectsFrame(g);
  // Walking through a door: the buttons are not the game's meanwhile, A, B or Start skips it.
  bool doorSkip = false;
  if (g->doorSeq != 0)
  {
    doorSkip = (pressed & (BM_KEY_A | BM_KEY_B | BM_KEY_START)) != 0;
    keys = pressed = previous = 0;
    // In the air (a jump into the door): down first, the walk through starts on the ground.
    bool standing = g->player.onGround || g->player.climbing || g->player.inWater;
    if (g->doorTimer < 255 && (standing || g->doorTimer != 0)) g->doorTimer++;
    if (g->hurtTimer < 2) g->hurtTimer = 2;
  }
  if (g->doorArriveTimer != 0 && --g->doorArriveTimer == 0) g->doorArrive = 0;
  // Mana comes back slowly (first-items.md §1).
  if (g->frame % BM_GAME_MANA_EVERY == 0 && g->mana < bmGameMaxMana(g)) g->mana++;

  BmBodyInput in = {
    .dx = (int8_t)(((keys & BM_KEY_RIGHT) ? 1 : 0) - ((keys & BM_KEY_LEFT) ? 1 : 0)),
    .dy = (int8_t)(((keys & BM_KEY_DOWN) ? 1 : 0) - ((keys & BM_KEY_UP) ? 1 : 0)),
    .jumpPressed = (pressed & BM_KEY_A) != 0,
    .jumpHeld = (keys & BM_KEY_A) != 0,
  };
  // A screen open (or Start opening the Start menu): the room stands still, the screens get
  // the buttons.
  // Debug: L + R + Start gives the Debug Mirror (the debug lobby, effects.h), if the game has
  // one; the menu stays shut.
  if (g->screens.count == 0 && (pressed & BM_KEY_START) && (keys & BM_KEY_L) && (keys & BM_KEY_R))
  {
    uint32_t mirror = 2166136261u;
    for (const char *n = "DebugMirror"; *n; n++) mirror = (mirror ^ (uint8_t)*n) * 16777619u;
    BmDataTable table;
    const uint8_t *record;
    if (bmDataFindRecord(g->pPack, mirror, &table, &record) && bmInventoryCount(&g->inventory, mirror) == 0 &&
        bmInventoryGive(&g->inventory, mirror, 1) > 0)
      bmGameNotice(g, mirror, 0xFFFF, 1);
    pressed = (uint16_t)(pressed & ~BM_KEY_START);
  }
  if (g->screens.count == 0 && (pressed & BM_KEY_START) && g->dialogue == 0)
  {
    uint16_t start = 0xFFFF;
    for (uint32_t id = 0; id < 0xFFFF; id++)
    {
      const char *t = bmPackString(g->pPack, (uint16_t)id);
      if (t == NULL) break;
      if (strcmp(t, "StartMenu") == 0) { start = (uint16_t)id; break; }
    }
    // The press that opened it is not one for the menu.
    if (start != 0xFFFF && bmScreensOpen(g, start)) previous = keys;
  }
  if (g->screens.count != 0)
  {
    bmScreensFrame(g, keys, previous);
    bmUiPresent(&g->ui);
    updateSprites(g);
    int8_t *l, *r;
    bmHalAudioBuffers(&l, &r);
    bmSoundFrame(&g->sound, l, r, BM_HAL_AUDIO_FRAME_SAMPLES);
    return;
  }
  if (g->dialogue != 0 && (pressed & BM_KEY_A))
  {
    // A shows the rest of what fits at once; with the box full and more of the line to come,
    // the text scrolls up a row and goes on appearing; when it is all there, the box goes.
    if (g->dialogueShown < g->dialogueLength)
    {
      int x = dialogueIndent(g);
      bmUiTextScroll(&g->ui, 2 + x, 17, 26 - x, 2, g->dialogueLine, NULL, 0, g->dialogueRow, g->dialogueShown, 0x7FFFFFFF, NULL, NULL);
      g->dialogueShown = g->dialogueLength;
      dialogueMarker(g);
    }
    else if (g->dialogueMore && g->dialogueRow < 255)
    {
      g->dialogueRow++;
      bmGameDrawDialogue(g);
    }
    else endDialogue(g);
    pressed = (uint16_t)(pressed & ~BM_KEY_A);
    in.jumpPressed = false;
  }
  dialogueFrame(g);
  // The marker bobs while the box waits for A.
  if (g->dialogue != 0 && g->screens.count == 0 && g->dialogueMore && g->dialogueShown >= g->dialogueLength && g->frame % 16 == 0)
    dialogueMarker(g);
  // The quick slots (first-items.md §1): L uses the first, Select the second (not while L or
  // R is held: L + R + Select is the debug overlay's).
  if (g->dialogue == 0)
  {
    if ((pressed & BM_KEY_L) && !(keys & (BM_KEY_R | BM_KEY_SELECT))) bmGameUseQuick(g, 0);
    if ((pressed & BM_KEY_SELECT) && !(keys & (BM_KEY_L | BM_KEY_R))) bmGameUseQuick(g, 1);
  }
  // A command (commands.h) takes its button: the attachments do not see it pressed.
  uint16_t taken = g->dialogue == 0 ? bmCommandsFrame(g, keys, previous) : 0;
  // Attachments first: what they decide this frame moves the player this frame.
  bmAttachmentsFrame(g, keys, (uint16_t)(previous | taken));
  // A lock (a swing) takes walking and jumping away; set this frame, it already counts.
  bool keepFacing = false;
  int8_t facing = g->player.facing;
  if (g->lockTimer != 0)
  {
    // What the player's movement does meanwhile is the lock's (lock_move): by default an
    // attack cancels walking but not the speed, which fades with friction, and in the air the
    // player steers; the facing stays (the swing goes where it was aimed).
    if (g->player.onGround)
    {
      if (g->lockGround == BM_LOCK_STOP) g->player.vx = 0;
      if (g->lockGround != BM_LOCK_WALK) in.dx = 0;
      in.coast = g->lockGround == BM_LOCK_KEEP;
    }
    else
    {
      if (g->lockAir != BM_LOCK_STEER) in.dx = 0;
      in.coast = g->lockAir == BM_LOCK_AIR_KEEP;
    }
    keepFacing = true;
    in.jumpPressed = false;
    g->lockTimer--;
  }
  if (g->doorSeq != 0)
  {
    // Through a side door the player walks on, out of the room; at a door in the back wall
    // the player stands while it opens.
    uint8_t side = g->objectDoorSide[g->doorSeq - 1];
    in.dx = (int8_t)(g->doorTimer == 0 ? 0 : side == 1 ? 1 : side == 2 ? -1 : 0);
    in.dy = 0;
    in.jumpPressed = in.jumpHeld = false;
    in.coast = false;
  }
  // Heavy armour and the like change the running speed (add_speed).
  BmBodyParams params = g->playerParams;
  // Capabilities from the stats (abilities, equipment, attachments).
  int32_t airJumps = bmInventoryStat(&g->inventory, BM_STAT_AIR_JUMPS);
  params.airJumps = (uint8_t)(airJumps < 0 ? 0 : airJumps > 255 ? 255 : airJumps);
  params.wallSlide = bmInventoryStat(&g->inventory, BM_STAT_WALL_SLIDE) > 0;
  if (g->speedPercent != 0)
  {
    int32_t percent = 100 + g->speedPercent;
    params.runSpeed = (bm_fix)(params.runSpeed * (percent < 10 ? 10 : percent) / 100);
  }
  uint32_t events = 0;
  g->puzzle.pushing = 0;   // bmPuzzleFrame says when the player pushes
  // The grapple (grapple.h): while it pulls the player, it moves them, not the body step.
  bool grappled = g->doorSeq == 0 && g->dialogue == 0 && bmGrappleFrame(g, keys, pressed);
  if (grappled) {}
  else if (g->doorSeq != 0 && in.dx != 0)
  {
    // Through a side door: walking at running speed out over the room's edge, the walls and
    // edges do not hold the player there.
    g->player.facing = in.dx;
    g->player.vx = in.dx * params.runSpeed;
    g->player.vy = 0;
    g->player.x += g->player.vx;
  }
  else
  {
    // Blocks pushed (the way the player walks), plates, gates: before the player moves.
    bmPuzzleFrame(g, in.dx);
    events = bmBodyStep(&g->player, &params, &g->collision, &g->physics, in);
    if (keepFacing) g->player.facing = facing;
  }
  bmAttachmentsBodyEvents(g, events);
  if (g->hurtTimer) g->hurtTimer--;
  for (uint8_t k = 0; k < BM_GAME_EFFECTS; k++)
    if (g->effectAge[k] != 0 && ++g->effectAge[k] > bmAnimLength(&g->sheets[g->effectSheet[k]], 0)) g->effectAge[k] = 0;
  if (g->doorCooldown) g->doorCooldown--;
  if (g->saveCooldown) g->saveCooldown--;

  if (events & BM_BODY_JUMPED) bmSoundPlayEffect(&g->sound, "jump");
  if (events & BM_BODY_DEATH)
  {
    respawn(g);
  }
  else if (events & BM_BODY_DAMAGE)
  {
    bmGameHurtPlayer(g, 1);
  }

  // Engine objects the player touches.
  int32_t px = BM_FIX_INT(g->player.x), py = BM_FIX_INT(g->player.y);
  int32_t hw = g->playerParams.halfWidth, h = g->playerParams.height;
  for (uint16_t i = 0; i < g->objectCount; i++)
  {
    const BmObjectView *o = &g->objects[i];
    // A door in the back wall (direction north) is gone through with Up, standing in it. A
    // scripted one hears touch() again then (it decides: a key it needs).
    bool backWall = o->kind == BM_OBJECT_DOOR && g->objectDoorSide[i] == 0;
    if (backWall && (pressed & BM_KEY_UP) && o->w != 0 && overlaps(px - hw, py - h, hw * 2, h, o->x, o->y, o->w, o->h))
    {
      for (uint8_t k = 0; k < g->actors.count; k++)
        if (g->actors.object[k] == i) g->actors.touching[k] = 0;
    }
    if (bmActorsOwnObject(g, i)) continue;  // its script decides what happens
    if (o->kind == BM_OBJECT_SAVE_POINT)
    {
      // Points: the area of a 16x32 marker standing on the point.
      if (g->saveCooldown == 0 && g->player.onGround && overlaps(px - hw, py - h, hw * 2, h, o->x - 8, o->y - 32, 16, 32))
      {
        g->savedRoom = g->roomIndex;
        g->savedX = o->x;
        g->savedY = o->y;
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
    if (g->doorCooldown == 0 && g->doorSeq == 0 && (!backWall || (pressed & BM_KEY_UP)))
    {
      useDoor(g, i);
      break;
    }
  }
  // The room's attachments, then its scripted objects; a door one of them opened is gone
  // through after all had their turn.
  bmAttachmentsRoomFrame(g);
  bmActorsFrame(g);
  // Objects the engine moves (projectiles).
  bmActorsMove(g);
  // Objects struck earlier are pushed on (after their scripts moved them).
  bmHitsKnockFrame(g);
  if (g->actors.pendingDoor != 0)
  {
    uint16_t index = (uint16_t)(g->actors.pendingDoor - 1);
    g->actors.pendingDoor = 0;
    // A door in the back wall only with Up (touch() as the player walks in does nothing).
    bool backWall = index < g->objectCount && g->objectDoorSide[index] == 0;
    if (g->doorCooldown == 0 && index < g->objectCount && (!backWall || (pressed & BM_KEY_UP))) useDoor(g, index);
    else if (g->doorCooldown != 0)
    {
      // Just come through a door, and already into one (straight back): its touch() comes
      // again until the cooldown is over, or the player would have to step out and in again.
      for (uint8_t k = 0; k < g->actors.count; k++)
        if (g->actors.object[k] == index) g->actors.touching[k] = 0;
    }
  }
  // Through the door being walked through, when the walk is over or skipped.
  if (g->doorSeq != 0 && (doorSkip || g->doorTimer >= (g->objectDoorSide[g->doorSeq - 1] == 1 || g->objectDoorSide[g->doorSeq - 1] == 2 ? DOOR_WALK_FRAMES : DOOR_FACING_FRAMES)))
  {
    BmObjectView door = g->objects[g->doorSeq - 1];
    g->doorSeq = 0;
    goThroughDoor(g, &door);
  }
  // Falling out of the bottom without a kill zone.
  if (BM_FIX_INT(g->player.y) > g->room.heightPx + 64 || (g->player.flipped && BM_FIX_INT(g->player.y) < -64))
  {
    respawn(g);
  }

  // The cell the player is in now is explored (map.h).
  bmMapFrame(g);
  animatePlayer(g);
  // Scripts and animations have had their turn: who is struck.
  bmHitsFrame(g);
  updateCamera(g);
  updateSprites(g);
  bmEffectsPresent(g);
  bmLightPresent(g);
  bmReflectPresent(g);
  hudFrame(g);
  bmUiPresent(&g->ui);

  // This frame's sound; it plays from the next vertical blank.
  int8_t *left, *right;
  bmHalAudioBuffers(&left, &right);
  bmSoundFrame(&g->sound, left, right, BM_HAL_AUDIO_FRAME_SAMPLES);
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
  F(player.flipped, BM_TYPE_BOOL, BM_STATE_SNAPSHOT | BM_STATE_LINK),
  F(player.airJumpsLeft, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(player.wallSide, BM_TYPE_S8, BM_STATE_SNAPSHOT),
  F(player.wallTouch, BM_TYPE_S8, BM_STATE_SNAPSHOT),
  F(player.wallCoyote, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(player.wallCoyoteSide, BM_TYPE_S8, BM_STATE_SNAPSHOT),
  F(player.safeX, BM_TYPE_S32, BM_STATE_SAVE),
  F(player.safeY, BM_TYPE_S32, BM_STATE_SAVE),
  F(frame, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(keysPrevious, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(hurtTimer, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(lockTimer, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(animHeld, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(health, BM_TYPE_S16, BM_STATE_SAVE | BM_STATE_LINK),
  F(mana, BM_TYPE_S16, BM_STATE_SAVE | BM_STATE_LINK),
  F(speedPercent, BM_TYPE_S16, BM_STATE_SNAPSHOT),
  BM_STATE_ARRAY(BmGame, resists, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_GAME_ELEMENTS),
  F(returnPending, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(hitStop, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(lockGround, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(pickupSound, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(lockAir, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(doorSeq, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(doorTimer, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(doorArrive, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(doorArriveTimer, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  BM_STATE_ARRAY(BmGame, cmdDirs, BM_TYPE_U8, BM_STATE_SNAPSHOT, 8),
  BM_STATE_ARRAY(BmGame, cmdTimes, BM_TYPE_U16, BM_STATE_SNAPSHOT, 8),
  F(cmdHead, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(cmdCount, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(cmdLast, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(cmdDone, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(fxKind, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(fxA, BM_TYPE_S16, BM_STATE_SNAPSHOT),
  F(fxB, BM_TYPE_S16, BM_STATE_SNAPSHOT),
  F(teleTimer, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(teleRoom, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(teleX, BM_TYPE_S16, BM_STATE_SNAPSHOT),
  F(teleY, BM_TYPE_S16, BM_STATE_SNAPSHOT),
  F(noticeKey, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(noticeTitled, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(noticeText, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(noticeCount, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(noticeTimer, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  // The world map: what the player has seen of it.
  BM_STATE_ARRAY(BmGame, mapExplored, BM_TYPE_U8, BM_STATE_SAVE | BM_STATE_LINK, BM_MAP_MAX_CELLS / 8),
  BM_STATE_ARRAY(BmGame, mapRevealed, BM_TYPE_U8, BM_STATE_SAVE | BM_STATE_LINK, BM_MAP_MAX_ROOMS / 8),
  F(savedRoom, BM_TYPE_U32, BM_STATE_SAVE),
  F(savedX, BM_TYPE_S32, BM_STATE_SAVE),
  F(savedY, BM_TYPE_S32, BM_STATE_SAVE),
  F(dialogue, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(dialogueSpeaker, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(dialogueLine, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(dialoguePortrait, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(dialogueVoice, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(dialogueShown, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(dialogueLength, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(dialogueRow, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(doorCooldown, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(saveCooldown, BM_TYPE_U16, BM_STATE_TRANSIENT),
  F(deaths, BM_TYPE_U32, BM_STATE_SAVE),
  F(roomChanges, BM_TYPE_U32, BM_STATE_SAVE),
  F(hurts, BM_TYPE_U32, BM_STATE_SAVE),
  F(saves, BM_TYPE_U32, BM_STATE_SAVE),
  F(playerAnim.anim, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerAnim.finished, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerAnim.step, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerAnim.timer, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  // The player's layers (figure.h); the root's animation is playerAnim.
  F(playerLayers[0].track, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(playerLayers[0].sprite, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[0].palette, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[0].angle, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[0].goal, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[0].speed, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[0].flags, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[1].anim.anim, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[1].anim.finished, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[1].anim.step, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[1].anim.timer, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[1].track, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(playerLayers[1].sprite, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[1].palette, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[1].angle, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[1].goal, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[1].speed, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[1].flags, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[2].anim.anim, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[2].anim.finished, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[2].anim.step, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[2].anim.timer, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[2].track, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(playerLayers[2].sprite, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[2].palette, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[2].angle, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[2].goal, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[2].speed, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[2].flags, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[3].anim.anim, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[3].anim.finished, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[3].anim.step, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[3].anim.timer, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[3].track, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(playerLayers[3].sprite, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  F(playerLayers[3].palette, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[3].angle, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[3].goal, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[3].speed, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(playerLayers[3].flags, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  // Scripted objects: where each script is and what its variables hold.
  F(actors.moduleHash, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(actors.count, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  BM_STATE_ARRAY(BmGame, actors.classIndex, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.state, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.wait, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.pc, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.timer, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.fields, BM_TYPE_S32, BM_STATE_SNAPSHOT, BM_ACTORS_MAX * BM_SCRIPT_MAX_FIELDS),
  BM_STATE_ARRAY(BmGame, actors.x, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.y, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.facing, BM_TYPE_S8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.hidden, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.touching, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.anim, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.palette, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.flash, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.weight, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.angle, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.extraHidden, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.serial, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.spawnHomeX, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.spawnHomeY, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  F(markerTarget, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  F(markerSprite, BM_TYPE_U16, BM_STATE_SNAPSHOT),
  BM_STATE_ARRAY(BmGame, actors.animFinished, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.animStep, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.animTimer, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.health, BM_TYPE_S32, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.team, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.invulnerable, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.affinity, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.knock, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.steady, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.spawned, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.gone, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.vx, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.vy, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.fall, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.life, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.madeBy, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  BM_STATE_ARRAY(BmGame, actors.madeBySlot, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ACTORS_MAX),
  F(actors.seed, BM_TYPE_U32, BM_STATE_SNAPSHOT),
  // What `save` variables hold: this goes into the save game.
  F(actors.storeCount, BM_TYPE_U8, BM_STATE_SAVE),
  BM_STATE_ARRAY(BmGame, actors.storeKey, BM_TYPE_U32, BM_STATE_SAVE, BM_ACTORS_STORE),
  BM_STATE_ARRAY(BmGame, actors.storeValue, BM_TYPE_S32, BM_STATE_SAVE, BM_ACTORS_STORE),
  // The inventory: into the save game and to another device.
  F(inventory.count, BM_TYPE_U8, BM_STATE_SAVE | BM_STATE_LINK),
  BM_STATE_ARRAY(BmGame, inventory.key, BM_TYPE_U32, BM_STATE_SAVE | BM_STATE_LINK, BM_INVENTORY_SLOTS),
  BM_STATE_ARRAY(BmGame, inventory.amount, BM_TYPE_U8, BM_STATE_SAVE | BM_STATE_LINK, BM_INVENTORY_SLOTS),
  BM_STATE_ARRAY(BmGame, inventory.equipped, BM_TYPE_U32, BM_STATE_SAVE | BM_STATE_LINK, BM_INVENTORY_EQUIP),
  BM_STATE_ARRAY(BmGame, inventory.quick, BM_TYPE_U32, BM_STATE_SAVE | BM_STATE_LINK, BM_INVENTORY_QUICK),
  BM_STATE_ARRAY(BmGame, inventory.statBase, BM_TYPE_S16, BM_STATE_SAVE | BM_STATE_LINK, BM_INVENTORY_STATS),
  BM_STATE_ARRAY(BmGame, inventory.statBonus, BM_TYPE_S16, BM_STATE_SNAPSHOT, BM_INVENTORY_STATS),
  // Attachments: in the middle of what they do, exactly (snapshot); a save game makes them
  // again from what the player owns (not yet: attachable-scripts.md step 6).
  BM_STATE_ARRAY(BmGame, attachments.classKey, BM_TYPE_U32, BM_STATE_SNAPSHOT, BM_ATTACH_SLOTS),
  BM_STATE_ARRAY(BmGame, attachments.state, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ATTACH_SLOTS),
  BM_STATE_ARRAY(BmGame, attachments.wait, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ATTACH_SLOTS),
  BM_STATE_ARRAY(BmGame, attachments.pc, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ATTACH_SLOTS),
  BM_STATE_ARRAY(BmGame, attachments.timer, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ATTACH_SLOTS),
  BM_STATE_ARRAY(BmGame, attachments.fields, BM_TYPE_S32, BM_STATE_SNAPSHOT, BM_ATTACH_SLOTS * BM_SCRIPT_MAX_FIELDS),
  BM_STATE_ARRAY(BmGame, attachments.lifetime, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_ATTACH_SLOTS),
  BM_STATE_ARRAY(BmGame, attachments.statOf, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_ATTACH_SLOTS * BM_ATTACH_STATS),
  BM_STATE_ARRAY(BmGame, attachments.statAdded, BM_TYPE_S16, BM_STATE_SNAPSHOT, BM_ATTACH_SLOTS * BM_ATTACH_STATS),
  BM_STATE_ARRAY(BmGame, attachments.abilityKey, BM_TYPE_U32, BM_STATE_SNAPSHOT, BM_ATTACH_ABILITY_SLOTS),
  // Hits in the middle of a swing: a snapshot continues them with the same targets still to come.
  BM_STATE_ARRAY(BmGame, hits.source, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.slot, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.name, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.damage, BM_TYPE_S16, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.element, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.anim, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.frames, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.reach, BM_TYPE_S16, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.aim, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.aimNext, BM_TYPE_U8, BM_STATE_SNAPSHOT, 9),
  BM_STATE_ARRAY(BmGame, hits.flags, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.multi, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.multiCount, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.stop, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.knockX, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.knockY, BM_TYPE_FIX, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  BM_STATE_ARRAY(BmGame, hits.nextFlags, BM_TYPE_U8, BM_STATE_SNAPSHOT, 9),
  BM_STATE_ARRAY(BmGame, hits.nextMulti, BM_TYPE_U8, BM_STATE_SNAPSHOT, 9),
  BM_STATE_ARRAY(BmGame, hits.nextStop, BM_TYPE_U8, BM_STATE_SNAPSHOT, 9),
  BM_STATE_ARRAY(BmGame, hits.nextKnockX, BM_TYPE_FIX, BM_STATE_SNAPSHOT, 9),
  BM_STATE_ARRAY(BmGame, hits.nextKnockY, BM_TYPE_FIX, BM_STATE_SNAPSHOT, 9),
  BM_STATE_ARRAY(BmGame, hits.struck, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_HITS_MAX),
  // Open screens, in the middle of what their scripts do.
  F(screens.count, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  BM_STATE_ARRAY(BmGame, screens.classKey, BM_TYPE_U32, BM_STATE_SNAPSHOT, BM_SCREENS_MAX),
  BM_STATE_ARRAY(BmGame, screens.state, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_SCREENS_MAX),
  BM_STATE_ARRAY(BmGame, screens.wait, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_SCREENS_MAX),
  BM_STATE_ARRAY(BmGame, screens.pc, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_SCREENS_MAX),
  BM_STATE_ARRAY(BmGame, screens.timer, BM_TYPE_U16, BM_STATE_SNAPSHOT, BM_SCREENS_MAX),
  BM_STATE_ARRAY(BmGame, screens.fields, BM_TYPE_S32, BM_STATE_SNAPSHOT, BM_SCREENS_MAX * BM_SCRIPT_MAX_FIELDS),
  F(screens.repeat, BM_TYPE_U8, BM_STATE_TRANSIENT),
  BM_STATE_ARRAY(BmGame, puzzle.blockX, BM_TYPE_S16, BM_STATE_SNAPSHOT, BM_PUZZLE_BLOCKS),
  BM_STATE_ARRAY(BmGame, puzzle.blockY, BM_TYPE_S16, BM_STATE_SNAPSHOT, BM_PUZZLE_BLOCKS),
  BM_STATE_ARRAY(BmGame, puzzle.blockFraction, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_PUZZLE_BLOCKS),
  BM_STATE_ARRAY(BmGame, puzzle.blockFall, BM_TYPE_S16, BM_STATE_SNAPSHOT, BM_PUZZLE_BLOCKS),
  BM_STATE_ARRAY(BmGame, puzzle.blockPush, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_PUZZLE_BLOCKS),
  BM_STATE_ARRAY(BmGame, puzzle.gateLift, BM_TYPE_U8, BM_STATE_SNAPSHOT, BM_PUZZLE_GATES),
  F(puzzle.pressed, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(grapple.phase, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(grapple.target, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(grapple.object, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(grapple.timer, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(grapple.cooldown, BM_TYPE_U8, BM_STATE_SNAPSHOT),
  F(grapple.side, BM_TYPE_S8, BM_STATE_SNAPSHOT),
  F(grapple.hookX, BM_TYPE_S32, BM_STATE_SNAPSHOT),
  F(grapple.hookY, BM_TYPE_S32, BM_STATE_SNAPSHOT),
  F(grapple.speedX, BM_TYPE_S32, BM_STATE_SNAPSHOT),
  F(grapple.speedY, BM_TYPE_S32, BM_STATE_SNAPSHOT),
  F(grapple.targetX, BM_TYPE_S16, BM_STATE_SNAPSHOT),
  F(grapple.targetY, BM_TYPE_S16, BM_STATE_SNAPSHOT),
  F(grapple.travelled, BM_TYPE_U16, BM_STATE_SNAPSHOT),
};
#undef F

BmStateBlock bmGameStateBlock(BmGame *pGame)
{
  return (BmStateBlock){ "game", s_gameFields, (uint16_t)(sizeof(s_gameFields) / sizeof(s_gameFields[0])), pGame };
}

// `fresh`: the scripted objects start anew (a save game holds only what `save` variables kept).
static BmResult afterLoad(BmGame *g, bool fresh)
{
  BmBody player = g->player;
  uint32_t roomIndex = g->roomIndex;
  if (roomIndex >= bmRoomCount(g->pPack)) return BM_ERROR_INVALID_ARGUMENT;
  BmResult r = enterRoom(g, roomIndex, fresh);
  if (r != BM_SUCCESS) return r;
  g->player = player;
  g->saveCooldown = 0;
  // What was on the UI layer is not in the state: the screens and the dialogue draw it again.
  bmUiClear(&g->ui);
  bmUiShow(&g->ui, false);
  g->hudDirty = true;
  bmDataPrepare(g->pPack);
  // A changed record (a pushed pack) counts at once; the abilities owned are attached (a save
  // game has no attachments).
  bmGameEquipmentChanged(g);
  bmAttachmentsAbilities(g);
  bmUiBlend();
  bmGameDrawDialogue(g);
  bmScreensRedraw(g);
  updateCamera(g);
  updateSprites(g);
  g->fxApplied = 7;   // the video state was rebuilt: every slot is set again or switched off
  bmEffectsPresent(g);
  bmLightPresent(g);
  bmReflectPresent(g);
  return BM_SUCCESS;
}

BmResult bmGameAfterLoad(BmGame *g)
{
  return afterLoad(g, false);
}

// Save memory layout: the save game at offset 0 (bmStateWrite format), the options at 1024.
#define SAVE_CAPACITY 1024
#define OPTIONS_OFFSET 1024
#define OPTIONS_MAGIC 0x504F4D42u   // "BMOP"

// Options: u32 magic, u8 language, u8 colour choice (BmGame::colorChoice; 0 in older ones), u16 0.
static void writeOptions(const BmGame *g)
{
  uint8_t o[8] = { (uint8_t)OPTIONS_MAGIC, (uint8_t)(OPTIONS_MAGIC >> 8), (uint8_t)(OPTIONS_MAGIC >> 16), (uint8_t)(OPTIONS_MAGIC >> 24),
                   g->language, g->colorChoice, 0, 0 };
  bmHalSaveWrite(OPTIONS_OFFSET, o, sizeof(o));
}

bool bmGameColorCorrection(const BmGame *g)
{
  // Not chosen: corrected on a GBA's own screen only (an SP's looks like it to the game).
  return g->colorCorrectable && (g->colorChoice == 0 ? bmHalScreen() == BM_HAL_SCREEN_GBA : g->colorChoice == 1);
}

// The pack's colour correction (CCOR, pack-format.md: after 4 bytes the matrix and the curve,
// laid out as BmColorCorrection) for the colours written from now on, if it has one and it is on.
static void applyColorCorrection(BmGame *g)
{
  int32_t c = bmPackFindChunk(g->pPack, BM_CHUNK_CCOR, 0);
  uint32_t size = 0;
  const uint8_t *p = c >= 0 ? bmPackChunkData(g->pPack, (uint32_t)c, &size) : NULL;
  g->colorCorrectable = p != NULL && size >= 4 + sizeof(BmColorCorrection) && p[0] == 1;
  BmColorCorrection cc;
  if (g->colorCorrectable) memcpy(&cc, p + 4, sizeof(cc));
  for (int i = 0; i < 32; i++) cc.curve[i] = cc.curve[i] > 31 ? 31 : cc.curve[i];   // the light looks it up as it is
  bmPaletteSetCorrection(bmGameColorCorrection(g) ? &cc : NULL);
}

static void readOptions(BmGame *g)
{
  uint8_t o[8];
  bmHalSaveRead(OPTIONS_OFFSET, o, sizeof(o));
  uint32_t magic = (uint32_t)o[0] | ((uint32_t)o[1] << 8) | ((uint32_t)o[2] << 16) | ((uint32_t)o[3] << 24);
  g->language = magic == OPTIONS_MAGIC && o[4] < bmTextLanguageCount(g->pPack) ? o[4] : 0;
  g->colorChoice = magic == OPTIONS_MAGIC && o[5] <= 2 ? o[5] : 0;
  bmUiSetLanguage(&g->ui, g->language);
  applyColorCorrection(g);
}

void bmGameSetLanguage(BmGame *g, uint8_t language)
{
  if (language >= bmTextLanguageCount(g->pPack)) return;
  g->language = language;
  bmUiSetLanguage(&g->ui, language);
  g->noticeDrawn = false;   // its text in the new language
  writeOptions(g);
}

// Every colour in palette memory written again (the correction changed): the room's, the UI's,
// the sprites' banks, the built-in ones, and the light's on top.
static void reloadPalettes(BmGame *g)
{
  bmRoomLoadPalette(&g->room);
  bmUiLoadPalette();
  for (uint8_t b = 0; b < BM_GAME_BANKS; b++)
    if (g->bankSheet[b] != 0xFF) bmSpriteSheetLoadPalette(&g->sheets[g->bankSheet[b]], g->bankPalette[b], b);
  loadBuiltinPalettes();
  g->light.fresh = true;
  g->light.objDark = 0;   // the banks out of the light were written once: again
  bmLightPresent(g);
  bmReflectPresent(g);
}

void bmGameSetColorCorrection(BmGame *g, bool on)
{
  if (!g->colorCorrectable) return;
  g->colorChoice = on ? 1 : 2;
  writeOptions(g);
  applyColorCorrection(g);
  reloadPalettes(g);
}

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
  BmStateReadResult r = bmStateRead(&block, 1, buffer, sizeof(buffer));
  if (r == BM_STATE_OTHER_SCHEMA)
  {
    // Written by an earlier version: its layout is in the pack's MIGR chunk (state.md §7).
    uint32_t written = bmStateWrittenSchema(buffer, sizeof(buffer));
    int32_t c = bmPackFindChunk(g->pPack, BM_CHUNK_MIGR, 0);
    uint32_t size = 0;
    const uint8_t *m = c >= 0 ? bmPackChunkData(g->pPack, (uint32_t)c, &size) : NULL;
    uint32_t at = 4;
    for (uint16_t n = m != NULL && size >= 4 ? bmRead16(m) : 0; n > 0 && at + 8 <= size; n--)
    {
      uint16_t fields = bmRead16(m + at + 4);
      if (at + 8 + fields * 8u > size) break;
      if (bmRead32(m + at) == written && (m[at + 6] & BM_STATE_SAVE))
      {
        r = bmStateMigrate(&block, 1, buffer, sizeof(buffer), m + at + 8, fields);
        break;
      }
      at += 8 + fields * 8u;
    }
  }
  if (r != BM_STATE_OK) return false;
  g->player.vx = g->player.vy = 0;
  if (afterLoad(g, true) != BM_SUCCESS) return false;
  // Attachments are not in a save game: what is equipped brings its own again.
  bmAttachmentsEquipAll(g);
  return true;
}
