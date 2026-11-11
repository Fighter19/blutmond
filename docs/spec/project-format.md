# Project format

Version 1, draft.

A project lists the rooms of a game and the folders that hold its source assets
([asset-model.md](asset-model.md)). It is a small XML file with the extension `.bmproj`,
written by the editor and readable by hand. Everything else (rooms, images, tilesets,
overrides) stays in its own open format next to it.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<project version="1" name="Blutmond test level">
 <room path="test_room/room.tmx"/>
 <room path="test_room/room2.tmx"/>
 <assets path="test_room"/>
 <assets path="engine"/>
</project>
```

| Element | Attributes | Meaning |
|---|---|---|
| `project` | `version` (1), `name` | the root |
| `room` | `path` | a room of the game (Tiled `.tmx`, [room-format.md](room-format.md)). The order is the project's room order: the first room is where a restarted game begins. Cooking the project cooks exactly these rooms |
| `assets` | `path` | a folder with source assets. The editor lists every file in it (recursively) and shows which rooms use it; files no room uses are marked as unused |

Rules:

- Paths are relative to the folder of the project file, with `/` as separator.
- A reader MUST reject versions other than 1 and SHOULD ignore elements it does not know.
- Assets that rooms reference from outside the asset folders are still found (through the
  rooms) and listed.
- Overrides of an asset lie next to it as `<asset file>.bmasset` (asset-model.md, Overrides);
  they are not listed in the project file.

## Editor operations

| Operation | Effect on disk |
|---|---|
| New room | writes a new `.tmx` with the layers the cooker expects (`background`, `main`, `collision` with a solid floor, `objects` with `PlayerStart` and `CameraZone`), using a chosen tileset and the project's engine tilesets, and adds it to the project |
| Add room | adds an existing `.tmx` to the project |
| Remove room | removes it from the project; the file stays |
| Import asset | copies files (and their `.bmasset`) into an asset folder; dropping files onto the editor does the same, and `.tmx` files are added as rooms |
| Delete asset | deletes the file and its `.bmasset`; refused while a room of the project uses it |

The project file is saved after every change to its room list.

## History

- 1: first version (rooms, asset folders).
