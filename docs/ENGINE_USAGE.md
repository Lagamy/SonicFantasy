# Engine usage

How to use the engine as it is: load a level, describe a level in JSON, add a level or a game. Facts about the original games are in the other docs; this one is about our code. Not run: nothing here has been built or tested yet.

## Loading a level

Levels are made in one place: `Loading::loadLevel(name)` (`src/Loading/LevelLoader.cpp`). From Lua:

```lua
local level = sf.loadLevel("EmeraldCoast")   -- or "Stg0100", "Stg0000"; returns the level handle
sf.unloadLevel(level)                         -- queued; done at the start of the next gameplay tick
sf.levelExists(level)                         -- false once the old level is gone
```

`loadLevel` reads `Assets/Levels/<name>/<name>.json` (the repo's `Levels/` folder, copied on every build by the `CopyLevels` target; each level is a folder `Levels/<name>/` holding `<name>.json` and, beside it, the level's own `Models/`, `Textures/`, `Collision/` and `prefabs.json`), makes the `Level`, then gives it its instance, collision and placed entities by its `engine`. A bad file, an unknown engine or an unknown table name throws; Lua gets the message ("Level 'X': ...") as an error. If the level fails halfway it is removed again. Loading the same name twice reuses the keys built the first time.

Load one level at a time: levels share a space and would overlap. The player's start position is the script's choice (`sf.spawnPlayer`, `sf.setCamera`; see `Scripts/game.lua`).

## Level file

All paths are disc paths, as in `ResourceKeys.h`. Offsets are numbers or strings (`"0x1B8D68"`). `position` is optional (default `[0,0,0]`). `shader` is the name of a shader added in `main.cpp` (`SA`, `STH2005`, `GLTF`).

### `"engine": "sadx"` (Sonic Adventure DX land table)

| field | meaning |
|---|---|
| `landTable` | `{ "rel": module on the disc, "offset": land table offset in it }` |
| `meshes` | optional, list of `{ "index", "mesh": model }`: the model of the level's own entity number `index`, written when it differs from its prefab's; applied before `textures`; one that can't be used is reported and the entity keeps its own |
| `textures` | the `.gvm` texture set |
| `skippedTextures` | texture names whose pieces are not drawn (optional) |
| `placementFiles` | SET files (optional) |
| `entityTable` | name of a generated `SAPlacedEntityKey` table (optional) |
| `typeNames` | name of a type id to name map, for the markers (optional) |

### `"cameras"` (both engines, optional; written by the editor's File > Save and Save Source Game Level, 2026-10-08, nothing built or run)

Top level of the level file (not inside `edits` / `source`). List of `{ "name", "position", "front", "yaw", "pitch", "fov", "near", "far", "enabled", "main", "renderTarget": { "width", "height" } }`: the camera entities the editor's Add camera / paste put in the level (`Editor::addLevelCamera`, in `Level::entityIds` after the source entities). `loadLevel` makes each after the edits (`addLevelCameras`, `addCamera`), owned by the level (removed with it and by Restore Source Game Level, which is not a Save). `main: true` makes it `Globals::Game::mainCamera`; `renderTarget` makes a render target of that size for it (absent: the screen); a camera that shared a target with another gets its own on load. Speed and boost are not saved. Cameras made by scripts or `main.cpp` are not in the level and not saved.

### `"edits"` (both engines, optional; written by the editor's File > Save)

The level's own recipe fields stay as they are: the keys keep naming the game's data, which is where every entity's original (source) state comes from, so reverting an edit is dropping it. Only what the editor changed is kept here.

| Field | Meaning |
|---|---|
| `moved` | list of `{ "index", "position", "source" }`: the level's own entity number `index` (creation order from the placement files, then stage entities) is put at `position` (world) after the level is made; `source` is the position the game's data gives, kept for reading |
| `added` | list of `{ "prefab", "position", "rotation", "scale", "behaviors", "shapes", "mesh", "texture" }`: an entity made from the prefab (`"SADX/SPRING"`) at `position`, owned by the level; `rotation` and `scale` (optional, written when not identity / (1,1,1)) are its transform; `behaviors`, `shapes`, `mesh` and `texture` (optional) are its whole behaviour list by name, its whole bound area, its model and its texture (the last two written when they differ from the prefab's), set after it is made |
| `prefabs` | optional, `{ "<prefab>": { "added": [names], "removed": [names], "shapesAdded": [shapes], "shapesRemoved": [shapes], "mesh": mesh, "texture": texture } }`: behaviours and bound area shapes given to or taken from every entity of a prefab, and the model and texture all of them get (`setPrefabMesh` / `setPrefabTexture`) (`addPrefabBehavior` / `removePrefabBehavior` / `addPrefabShape` / `removePrefabShape`); applied first, before `behaviors`, `shapes`, `moved` and `added`. A shape is `{ "kind", "form", "push", "damage", "attr", "center": [x,y,z], "a", "b", "c", "d", "angle": [x,y,z], "behavior": collision behaviour name or null }` (the fields of `Physics::CollisionShape`); a removal takes the first shape equal in every field. A model (`mesh`) is `{ "kind": "default"|"disc"|"external", "name": ... }`: `disc` = a SADX model as `<rel path>@0x<offset in hex>` (the entity keeps its texture set, which must hold the texture ids the model uses; its motion stops) or, on a Shadow entity, a `.DFF` entry of its own archive (a static model, textured from the entity's dictionary by the names the DFF uses); `external` = a model of `Assets/External/Models` by name (the opened level's first), drawn in place of the instance's model with its own textures. A texture is `{ "kind": "default"|"disc"|"external", "name": ... }`: `disc` = a SADX `.gvm` path (the whole texture set replaces the entity's; every texture id the model uses must exist in it) or, on a Shadow entity, a `.TXD` entry of the entity's own archive; `external` = a png / jpeg of `Assets/External/Textures` by file name without extension (the opened level's first), drawn on every textured submesh |
| `transforms` | optional, list of `{ "index", "rotation": [9 numbers], "scale": [x,y,z] }`: the rotation and scale of the level's own entity number `index`, written when either differs from the game's data (written 2026-10-08, nothing built or run) |
| `behaviors` | optional, list of `{ "index", "added", "removed" }`: behaviour names the level's own entity number `index` has more or less than the game's data plus the `prefabs` edits give it |
| `shapes` | optional, list of `{ "index", "shapes": [shapes] }`: the whole bound area of the level's own entity number `index`, written when it differs from the game's data plus the `prefabs` edits (so a later change of the game's data does not reach it) |
| `externals` | optional, list of `{ "index", "values": { "<behaviour>": { "<field>": value } } }`: the external field values (`sf.behavior`'s third argument, `Entity::externals`) of the level's own entity number `index`; `added[]` entries carry the same object as `"externals"`. A number, bool or string is itself, a vec3 `[x,y,z]`, a prefab its name (null = none), an entity its index in the level (own entities: `index`; added ones: `sourceEntityCount` plus their place in `added`; null = none or not in the level), resolved after the added entities exist (`Level::resolveExternalEntities`). Values of a behaviour or field no script declares are reported and skipped (written 2026-10-08, nothing built or run) Values no declared field takes when the level loads (behaviour or field not declared yet, wrong type) are kept as they are and written back by the next save. |
| `textures` | optional, list of `{ "index", "texture": texture }`: the texture of the level's own entity number `index`, written when it differs from its prefab's (`"default"` puts the model's own back over a prefab-wide one); a texture that can't be used (external file gone, disc texture that does not fit) is reported and the entity keeps its own |

### `"source"` (both engines, optional; written by the editor's File > Save Source Game Level; 2026-10-08, nothing built or run)

The level as the editor saved it as its Source Game Level: the same fields as `"edits"` except `prefabs`, with `added` entities included. `loadLevel` makes the game's entities, applies `source` (its `added` entities first, so its per-entity entries can name them: their index follows the level's own entities), and only then marks the entities as the level's source (`Level::markSourceEntities`); `edits` is applied after that. So Restore Source Game Level, Return to Source Game state and the differences `edits` holds are from `source`, and an `added` entity of `source` is a level's own entity (it has an index). An `added` entry of `source` that can't be made (unknown prefab, failed spawn) leaves a gone entity in its place, so the later indices stay as the file counts them. Saving it again composes the new differences into `source` (`composeSource`: a per-entity entry replaces the old one of that index; `behaviors` entries are composed; `added` entries are appended) and empties `edits`.

The prefab-wide edits of the Source Game Level are not in the file: `Assets/Levels/<level>/SourceGamePrefabs/<game>/<KEY>/prefab.json` (also `Levels/...` of the source tree), one file per prefab, the object of a `prefabs` entry (`Level/<name>` for the level's External prefabs). `Loading::loadSourcePrefabs` reads them into `Prefab::source` before the entities are made, a layer between the Changed file and the level's `prefabs` edits (game data, `changed`, `source`, `level`). Save Source Game Level writes `mergePrefabEdits(source, level)`.

**Changed files (prefab edits for every level, 2026-10-08, not run).** A prefab built from a key (`SADX/SPRING`) can also have `Assets/External/Prefabs/<game>/<KEY>/Changed/prefab.json` (mirrored in `External/Prefabs/...` of the source tree, copied by `CopyExternal`): the same object as one `prefabs` entry above (`added`, `removed`, `shapesAdded`, `shapesRemoved`, `mesh`, `texture`), read once at startup by `Loading::loadChangedPrefabs` (a missing file is no changes; External prefabs have none). Order for an entity: game data, then the Changed file, then the level's `prefabs` entry, then the entity's own edits. The level entry is applied on top, so its `removed` takes a behaviour or shape the Changed file added, and its `added` gives one the Changed file took; a level `mesh` / `texture` wins over the Changed one (a level cannot say "no model" against a Changed model: Default means "not set"). **Prefab data files (2026-10-10, not run).** What a key does not say is read by `Loading::loadPrefabData` once after `registerPrefabs` from two files (mirrored in `External/Prefabs/` of the source tree, copied by `CopyExternal`; a missing or bad one is printed and the prefabs keep `StaticObjects` / `Single`). `Assets/External/Prefabs/Groups.json`: `{ "SADX": {"prefixes": {"E ": "Enemies"}, "names": {"NPCs": [key names], "Interactible objects": [...]}}, "STH2005": {...} }` gives `Prefab::group` (hypothesis, the keys do not say what an object is): the first prefix that starts the key name, else the list that names it, else `Static objects`; the group names are the editor's folder names (`prefabGroupFolder`). `Assets/External/Prefabs/STH2005/Placement.json` (written by `tools/genEntityKeys.py`, with its hypotheses): `{ "<KEY>": {"layout": "Single"|"RingLine"|"Variant", "variants": [[model indices of the .BIN's info 0], ...], "loopMotion": true} }`, every field optional (Single, none, false); it gives `Prefab::layout`, `variants` and `loopMotion`, which `Level::setONEEntities` and `spawnPrefab` read from the prefab, not from the `EntityKey`. The players' body shapes are the Changed files of `SADX/Sonic` and `STH2005/Shadow` (one `shapesAdded` entry, behaviour `Body`), applied after the entity is made like any prefab shape edit; they were `DebugSetup::addSonicShape` / `addShadowShape`. An editor Global-scope shape edit of a player rewrites that file. A level file never holds the Changed file's content: `saveLevelEdits` writes only the level's entry, and the per-entity diffs (`behaviors`, `shapes`, `meshes`, `textures`) are against game data plus both. Edits made with the Global scope (`addPrefabBehavior(..., PrefabScope::Global)` etc.) write the file at once; an empty one removes it.

Positions, rotations, scales, behaviours, bound area shapes, models and textures are edited and saved. A rotation is the 9 numbers of the entity's 3x3 orientation matrix, column by column; a scale is 3 numbers (per axis of the entity's own space). Scale also scales the collision shapes of an entity without a physics body (not the player's or other bodies'). The load clears the prefabs' edits first (`clearPrefabEdits`), so one level's edits never reach another; a behaviour name (or collision behaviour name) nothing registered is reported and skipped. An edit that does not fit (index out of range, unknown prefab) is reported on stderr and skipped. Save writes `Assets/Levels/<name>/<name>.json` and `Levels/<name>/<name>.json` of the source tree (`SF_SOURCE_DIR`, set by CMake with EDITOR).

### `"engine": "sth2005"` (Shadow the Hedgehog stage)

| field | meaning |
|---|---|
| `sections` | the stage's `.one` section archives |
| `textures` | `{ "archive": the texture .one, "txd": the .TXD entry }` |
| `skippedEntries` | entry-name parts to skip, e.g. `"_D_"` (optional) |
| `placementFiles` | `.dat` placement files (optional) |
| `entityTable` | name of a generated `PlacedEntityKey` table (optional) |
| `typeNames` | name of a type id to name map (optional) |

Objects whose type the entity table does not list get a marker cross instead (collision view). Without an `entityTable` a level has no objects, only markers.

### `"engine": "gltf"` (levels from glTF files; 2026-10-08, not run)

| field | meaning |
|---|---|
| `collisions` | names of the collision files in use (`Assets/Levels/<Name>/Collision/<name>.collision.json`), merged into one collision; never drawn (optional) |

The glTF is read once, at import (File > New level, File > Swap level model, File > Swap collision > Add collision file), and is not needed afterwards. The level is read from its folder `Assets/Levels/<Name>/` (written there and mirrored into `External/` of the source tree, which the next build copies):

- `Models/<Name>_<cx>_<cz>_<k>.chunk.model.json/.bin`: the visual cut into chunks (`Loading/GLTFImport.*`). A triangle goes to the XZ grid cell (`ChunkCellSize` 256, metres of the glTF; a guess) its centroid is in; a cell with more than `MaxChunkTriangles` (65536) is split in halves at the median along its longer side (`k` numbers the parts). A chunk is a standard `ModelFile` with one submesh per texture and its own bounds; triangles are never cut, so a chunk can reach past its cell. `<Name>_tex<i>.png` (+ `.png.json`) next to them are the images the materials use. `scanExternalAssets` skips `*.chunk.model.json`.
- `Collision/<name>.collision.json/.bin` (`Importers/External/CollisionFile.*`): header `version` 1, `name`, `vertexCount`, `triangleCount`, `hasTriangleValues`, `pieces` [{flags, center, radius, firstTriangle, triangleCount}]; bin little-endian: `vertexCount` x 3 float32, `triangleCount` x 3 u32 indices, then (if `hasTriangleValues`) `triangleCount` u32 attributes. A glTF import has one piece per primitive, flags 0, no per-triangle values. `triangleMaterials` is not stored. Collision is not chunked: the collision quad tree already splits the level's triangles in XZ and is rebuilt whole.

`model`, `placementFiles`, `entityTable`, `typeNames` are not read (New level's model and collision paths are import inputs only). A glTF level has no entities of its own (add prefabs in the editor; they are saved as `edits.added`). Every node with a mesh is imported with its world transform applied; only triangle lists are read. The glTF's space is kept (Y up, metres). The visual is drawn with vertex colour (COLOR_0 times baseColorFactor) and the base colour texture; the level reads the PNGs into `Graphics::textures` (`<Name> <file stem>`) and removes them with the level. Every collision triangle is Solid (the `SADXLevel` remap's fallback, `External/Collision/Remaps.json`), two sided. Its shader is `GLTF` (the form's default; `Shaders/GLTF`): unlit, vertex colour linear. Loading reads only these files (no external-asset scan), so it works without `EDITOR`; the import is `EDITOR` only. Reader of the glTF: `Importers/External/GLTFFile.*` (cgltf 1.15).

Editor, File menu (glTF levels only): **Swap level model...** (path box) replaces the chunks and the live visual; **Swap collision** has a side menu: **Add collision file...** (path box; appended to the live collision and to `collisions`, quad tree rebuilt) and the level's collision files with the ones in use ticked; picking one asks for a `.gltf`/`.glb` path and overwrites that collision file with the new file's triangles (same name; the live collision is rebuilt if the file is in use).

## Named tables

The generated tables (`SAEntityKeys.h`, `STH2005EntityKeys.h`, the bridge keys in `ResourceKeys.h`) are C++, so a file names them and the registry at the top of `LevelLoader.cpp` maps the names to the objects. Registered now: `SAEntityTable_EmeraldCoast`, `SAObjectTypeNames_EmeraldCoast`, `ObjectTypeNames`, `EntityTable_stg0100`.

## Players (`External/Players/`)

Where the two players' resources are: `Assets/External/Players/Sonic.json` and `Shadow.json` (source tree `External/Players/`, copied by `CopyExternal`), read once by `Loading::loadPlayerKeys` in `main` after the remaps; a missing or wrong file throws and stops the game. They hold disc facts only (offsets and entry names, each with its evidence in a `note`); the animation names are ours, the ones the scripts pass to `animate`.

- `Sonic.json`: `textures` (the .gvm), `models` (`body`, `curled`, `jumpball`: `{rel, offset}`), `idle` (`{rel, offset, nodes}`, played when Sonic is made), `animations` (name to `{rel, offset, nodes, model, loop?}`; `model` names an entry of `models`; `loop` false = plays once and holds the last frame, default true). `sf.animate(e, name, rate, blendStep)`: a negative rate plays an SA motion backwards from its last frame (2026-10-11, not run).
- `Shadow.json`: `archive`, `textures` (the .TXD), `model` (`name` and `parts`, each `{dff, bon}` or `{dff, bone}` for a part that attaches to a bone of the main skeleton), `idle` (the motions played when Shadow is made: `{mtp, motion, bon, attachedPart}`), `animations` (name to `{mtp, motion, bon, loop}`; `loop` false plays once and holds the last frame).

## Adding a level

1. In the editor: File > New level... (name, engine, shader, disc paths, or for gltf the model and collision files, which are imported into the level's folder; `Loading::createLevelFile` writes `Assets/Levels/<Name>.json` and `Levels/<Name>.json` and the editor loads it; refuses a name that exists; not run). Or write `Levels/<Name>.json` by hand (copy the nearest of the same engine). `skippedTextures`, `skippedEntries` and `position` are only written by hand.
2. If it needs a generated table: generate it (`tools/genEntityKeys.py`, `tools/genSAEntityKeys.py`), add one line to the matching map in `LevelLoader.cpp`.
3. Call `sf.loadLevel("<Name>")`; add its camera and player starts to `Scripts/game.lua`.
4. Rebuild (the JSON is copied at build time, not at run time).

## Adding a game

A new `"engine"` string (`"gltf"` was added this way: `fillGLTF`, `Level::setGLTFGPUInstance` / `setGLTFCollision`, `GLTF_FILE` in `SourceEngineEnum`): add a `fill<Engine>` function (JSON to key, strings kept in the `Recipe`) and a branch in `readRecipe` and `loadLevel` of `LevelLoader.cpp`, plus the `Level` methods for its instance, collision and entities (`src/Objects/Level.*`).

## Limits

- Entity tables, type names and stage entities stay compiled; only the level recipe is data.
- Shadow stages place all four placement files (mission alternatives are not told apart).
- Levels are keyed by name for the whole run: a changed JSON is read only on the next start.
