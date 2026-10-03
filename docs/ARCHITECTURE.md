# Architecture

Snapshot of what the code shows as of 2026-10-03. The project is at the importer stage: a window, a free-fly
camera, an OpenGL renderer, and importers that load Sonic (SADX GameCube) and Shadow (Shadow the Hedgehog GameCube)
models with textures straight from the disc images, and play one looping idle motion on each (Sonic's Ninja motion,
Shadow's `.MTP` motions with skinning). No gameplay or lighting is implemented yet.

## Build
- CMake (>= 3.28), C++23, `CMAKE_EXPORT_COMPILE_COMMANDS ON`. Single target `SonicFantasy`.
- Existing `build/` is configured with Ninja, `CMAKE_BUILD_TYPE=Debug`.
- Linux only as written: links the vendored static `third-party/GLFW/lib/libglfw3.a` (loads X11 via dlopen), `pthread`, `m`, `GL`, `dl`.
- New `.cpp` files must be added to `add_executable` in `CMakeLists.txt` by hand.
- Include directories are `src` and every folder below it that holds headers (`src/Objects`, `src/Graphics`,
  `src/Graphics/Resources`, `src/Graphics/InstanceData`, `src/Importers`, `src/Importers/GameCube`,
  `src/Importers/Ninja`, `src/Importers/STH2005`, `src/Importers/Renderware`, `src/Loading`, `src/Keys`, `src/Misc`), so headers are included by bare name
  (`"Camera.h"`, not `"Objects/Camera.h"`). A new folder needs adding to `target_include_directories`.
- The executable runs with cwd `build/` (`.vscode/launch.json`, lldb). The `CopyShaders` target copies the repo's
  `Shaders/` to `build/Assets/Shaders` on every build. Other `Assets/` paths (`Assets/Models`, `Assets/Textures`) are
  read relative to `build/` but nothing copies them.
- The disc images are read from `Disks/Shadow2005.iso` and `Disks/SADX.iso` (`Constants.h`), i.e. `build/Disks/` (the executable runs with cwd `build/`).
- `.gitignore` excludes `build/`, every folder starting with a dot, and `sadreamcasttools/`.
- `ENGINE_DEBUG` gates the validity checks in `Pool`/`PoolMap` (including the missing-name error in
  `PoolMap::getIdByName`); `CMakeLists.txt` defines it for Debug builds.

## Directories
| Path | Contents |
|---|---|
| `src/main.cpp` | Window/renderer init, opening the discs, entity creation from resource keys, main loop, WASD + Space/Ctrl free-fly camera input. |
| `src/ResourceKeys.h` | The resource keys: disc paths, entry names and model recipes (parts, bones) of everything the game loads (`SonicMesh`, `SonicTextures`, `ShadowMesh`, `ShadowTextures`, ...). The key types are in `src/Keys/`. |
| `src/Graphics/` | `Renderer`, `GPU_Globals.h` (renderer and resource pools), `GPU_API` (add/get resources by `PoolId`). `namespace Graphics`. |
| `src/Keys/` | Resource key types: `SAKeys.h` (global namespace: `SAModelKey`, `SATextureSetKey`), `STH2005Keys.h` (`namespace STH2005`: `STH2005::ArchiveKey`, `STH2005::ModelPart` with `==`, `STH2005::ModelKey`, `STH2005::TextureKey`, `STH2005::MotionKey`); `SAKeys.h` also `SAMotionKey`. |
| `src/Loading/` | `ResourceLoader` (`namespace Loading`): reads the discs, runs the importers, keeps the user counts and frees unused resources; `addSAInstance`, `addONEInstance`, `playSAMotion`, `releaseSAMesh`, `releaseSAMotion`, `releaseTextureSet`, `releaseONE`. The Graphics pools only store. |
| `src/Graphics/Resources/` | Shared resources: `BasicMesh` (+ `Vertex`), `Texture`, `TextureSet`, `TextureBinding`, `Shader`, `SAMesh`, `SAAnimation.h` (`SAMotion`), `STH2005Motion.h` (`STH2005::Motion`, `namespace STH2005`), `RenderWareMesh` (Shadow meshes live inside their `STH2005::ONE`, not in a pool). Also `namespace Graphics`. `BoneAnimation.h` is empty. |
| `src/Graphics/InstanceData/` | Per-entity state of shared resources: `SAInstance`, `ONEInstance`. Also `namespace Graphics`. |
| `src/Importers/` | Importer folders only. Each folder is its own namespace named after it, so the code says where a format comes from (`GameCube::`, `Ninja::`, `STH2005::`, `Renderware::`); there is no `Importers` namespace. |
| `src/Importers/GameCube/` | `namespace GameCube`: `GCDisc` (disc file table + file reads), `Rel` (relocate a `.rel` module), `GXColor.h` (RGB565/RGB5A3 to RGBA8). |
| `src/Importers/Ninja/` | `namespace Ninja`, SADX (Ninja) formats: `SAMdl` (Ninja Basic models, `.sa1mdl` or a GameCube `.rel`, to `SAMesh`), `SAMotion` (NJS_MOTION to `SAMotion`), `Gvm` (GVM/GVR textures). |
| `src/Importers/STH2005/` | `namespace STH2005`, Shadow the Hedgehog formats: `STH2005::ONE` (`.ONE` archive), `Prs` (PRS decompression), `Bon` (`.BON` skeleton), `STH2005::Model` (builds one mesh from several DFF parts), `STH2005::Mtp` (`.MTP` motion table). |
| `src/Importers/Renderware/` | `namespace Renderware`: `Dff` (clump to `RenderWareMesh`), `Txd` (GameCube texture dictionary), `RwChunk.h` (chunk header reader shared by both). |
| `src/Objects/` | `Camera` (position/yaw/pitch, view+projection in a UBO), `Entity` (source engine, instance id, position; `draw`). |
| `src/Misc/` | Header-only: `Globals.h`, `Utils.h` (GLFW window, callbacks), `Debug.h` (GL debug callback), `Pool.h`, `PoolMap.h`, `PoolId.h`, `Constants.h`, `BinaryRead.h` (bounds-checked `readLE`/`readBE`). `API.cpp` has `addEntity`. |
| `Shaders/` | `Default` (vertex color only, used by the debug floor), `SA` (vertex color x optional texture, placed by `model`) and `STH2005` (same, plus skinning: bone indices/weights at locations 4/5, `skinned` at location 2, skin matrices in an SSBO at binding 1), each `vertex.glsl` + `fragment.glsl`. |
| `third-party/` | Vendored: glad, glm, imgui, stb (image), nlohmann/json, GLFW headers (`include/GLFW`) + static libs. |
| `sadreamcasttools/` | Separate clone of `https://github.com/FraGag/sadtools` (C#, WTFPL). Not part of the CMake build. Used as a reference for Ninja structs (`Data/MATERIAL.cs`, `MESH.cs`, ...). |
| `tools/`, `ResourceMap.txt`, `ResourceMapByCategory.txt` | `genResourceMap.py` and its output: a lookup table of models, textures, audio and animations on the two discs, with disc paths and offsets. `genCategoryMap.py` regroups that table per game into categories (characters, levels, bosses, ...) with their files, as `ResourceMapByCategory.txt`; the grouping is hypothesis from names. `tools/.cache/` holds downloaded SA Tools INIs. |
| `source/figure/` | Not described here. |

## Runtime flow
1. `createWindow` (`Utils.h`): GLFW, OpenGL 4.5 core, debug context, cursor disabled,
   framebuffer-size and cursor-pos callbacks.
2. `Graphics::renderer.init()`: loads GLAD, enables sRGB framebuffer, depth test,
   back-face culling (CCW front), loads the `Default` shader and builds the debug floor mesh.
3. A `Camera` is pushed into `Globals::Game::cameras`; index 0 is the player camera.
4. `main.cpp` adds the `SA` and `STH2005` shaders, opens both discs into `Globals::Discs` (they stay open), then creates each character:
   an `Entity` (position only) and its instance from the keys in `ResourceKeys.h`: `setSAGPUInstance(saShader, SonicMesh,
   SonicTextures)` then `playSAMotion(SonicIdle)`, `setONEGPUInstance(oneShader, ShadowMesh, ShadowTextures)` then `playONEMotion(ShadowIdle)` and the two hand motions. Nothing is imported before that: the
   instance finds or loads what its keys name (see Entity, TextureSet, STH2005::ONE below) and holds one user of it.
5. Loop: compute `Globals::App::deltaTime`, poll events, `processInputs()`, `Entity::update(deltaTime)` for every live
   entity (advances SA and ONE motions), `renderer.run(cameras[0])`: clear,
   bind the camera UBO at binding 0, draw the floor with `Default`, call `Entity::draw` for every live entity, swap.
6. `renderer.destroy()`, `destroyWindow()`.

## Core pieces
- **Globals** (`Globals.h`): `inline` variables in `Globals::App` (window, size, frame timing), `Globals::Game`
  (camera speed, cameras, `entities`), `Globals::Discs` (the two opened `GCDisc`s), `Globals::Mouse`. `Graphics::renderer` and the `basicMeshes`/`textures`/
  `textureSets`/`textureBindings`/`shaders`/`saMeshes`/`oneArchives` pools and instance pools live in `GPU_Globals.h`.
- **Pool<T> / PoolMap<T>**: vector-backed slot storage with generational `PoolId
  {id, generation}`, free-slot reuse, `alive` flags. `PoolMap` adds name lookup (`nameToId`; adding a duplicate name
  keeps the first id; `remove` drops a name only if it points to the removed slot). `remove` calls the element's `destroy` if it has one. `UninitializedPoolId` in `Constants.h`.
- **Camera**: yaw/pitch mouse look (pitch clamped to +-89 degrees), 45 degree perspective,
  near 0.1 / far 100. `view` and `projection` are uploaded to a UBO (`std140`,
  `CameraData { mat4 view; mat4 projection; }` at binding 0) on every change.
- **BasicMesh**: interleaved `Vertex` buffer (locations 0 position `vec3`, 1 normal `vec3`,
  2 uv `vec2`, 3 color `vec4`, 4 bone indices `u8vec4` (integer attribute), 5 bone weights `vec4`; both zero unless skinned), optional `uint32_t` index buffer, drawn as `GL_TRIANGLES`
  with whatever program is bound. Manual `destroy()`.
- **Shader**: `Shader(name)` loads `Assets/Shaders/<name>/vertex.glsl` and `fragment.glsl`, throws
  `std::runtime_error` with the GL log on failure, caches the `model` uniform location.
  `HasTextureLocation` (`Shader.h`) is the fixed location of the `hasTexture` uniform that textured draws set.
- **Texture**: CPU `pixels` (RGBA8) filled by an importer, uploaded by `create()` as `GL_SRGB8_ALPHA8`, no mipmaps.
  `filter` and `wrap` fields (default nearest / clamp to edge) are applied by `create()`. Manual `destroy()`.
- **Entity**: `sourceEngine` (`SA_NINJA` or `STH2005_ONE`) selects which instance pool `instanceId` refers to.
  An entity is created first and has no instance (`UninitializedPoolId`, not drawn) until `setSAGPUInstance` /
  `setONEGPUInstance` attaches one, created from resource keys (only one of the two, once; a second call throws;
  textures are optional, the overload without them is untextured). `draw` only builds the translation matrix and calls the instance's `draw`.
  `Entity::destroy` (run by `entities.remove`) removes the instance, which releases its texture set or archive.
  `changeActiveMesh(key)` / `changeActiveTexture(key)` (ONE instances only, otherwise they throw) switch the instance to
  another mesh or `.TXD` entry; the key's archive has to be the instance's, and what is not loaded yet is loaded.
  `switchMeshPart(previous, desired)` replaces one `STH2005::ModelPart` of the active mesh's recipe with another (the parts are
  named in `ResourceKeys.h`), so the combinations need no keys of their own: the mesh with that part switched is built on
  first use (`STH2005::ONE::switchPart`, named `<mesh>|<previous dff>><desired dff>`), found again when the recipe exists, and
  the pose is dropped like in `changeActiveMesh`.
  `playSAMotion(key)` / `playONEMotion(key)` (SA / ONE instances only) make the instance loop a motion; `update(deltaTime)` advances it.
  The instance's `draw` binds its shader, calls the mesh's `use()` (lazy GPU upload), binds textures and draws.
  `ONEInstance`s additionally rotate 90 degrees about +Y (`RenderWareYawDegrees` in `ONEInstance.cpp`), a
  value derived from Shadow alone and applied to every RenderWare model.
- **SAMesh**: node hierarchy (`parent` index, -1 = root, parents stored before children), rest `localTransform` per
  node, submeshes that own their `BasicMesh` directly plus a `textureIndex` (the material's texture id, `SANoTexture`
  if none). A mesh is named `<rel>@0x<offset>` (`SAModelKey`), imported on demand from the SADX disc, and has a `users` count: at 0
  `releaseSAMesh` removes it (and the bindings sets keep for it). The relocated `.rel` it came from is kept in `relModules`
  (a `RelModule`: the bytes plus a count of live meshes imported from it) so the next model of that module is not read and
  relocated again; the bytes are freed when the last of those meshes is removed. **SAInstance**: `saMeshId`, `shaderId`, `textureSetId` and
  `textureBindingId` (both `UninitializedPoolId` = untextured) plus `nodePose` (one matrix per node). It holds one user of its
  mesh and of its set. Nodes also keep their rest position, rotation (BAMS), scale and flags; `composeNinjaTransform`
  (`SAMesh.h`, rotation order a hypothesis) builds both the rest transform and animated poses.
- **SAMotion** (`SAAnimation.h`, `saMotions`, a `PoolMap` named `<rel>@0x<offset>`): a Ninja motion, frame count plus
  position/rotation/scale keys per model node. Loaded from an `SAMotionKey` (`.rel`, offset, node count from SA Tools) through
  the `relModules` cache by `Loading::playSAMotion`, which checks the node count against the model; `users` counts the
  instances playing it. `SAInstance` holds `currentSAAnimation` and `animationFrame`; `update` advances at 30 frames/s
  (hypothesis), loops, interpolates keys linearly (hypothesis, BAMS included) with rest values for unkeyed channels and
  fills `nodePose`.
- **RenderWareMesh**: `frames` (parent index, rest `localTransform`, HAnim `boneId`, `name` from a `.BON`), `atomics`
  (a frame index plus submeshes, each a `BasicMesh` and a `textureName`; a skinned atomic also has `skinBoneFrames` and
  `inverseBindMatrices`). `use()` uploads submeshes.
  **ONEInstance**: `archiveId`, `shaderId`, `meshIndex`, `textureDictionaryIndex`, `bindingIndex` (indices into the
  archive, `STH2005::NoIndex` = untextured), `framePose` overrides the rest
  transforms; `draw` computes frame world matrices and draws every atomic: a skinned one with `skinned` set and
  `world[skin bone frame] * inverse bind matrix` per bone uploaded to `Renderer::boneBuffer` (`MaxSkinBones` = 256), an
  unskinned one with `model` = its frame's world matrix. `motions` are the playing motions (`playMotion(key)`, one per skeleton,
  decoded on first use into `STH2005::ONE::motions`); `update(deltaTime)` advances them at 60 frames/s (hypothesis), loops over
  the motion's last frame and fills `framePose`: every frame at its rest transform, the frames named like a motion's bones at
  `translate(T) * rotation(R) * scale(S)` sampled with cubic Hermite (tangent = slope per frame, hypothesis). Euler order
  `Rz*Ry*Rx` (hypothesis, `ONEEulerYZX` switches to `Ry*Rz*Rx`). Bones without a frame are skipped; the first bone of an `attachedPart` motion (`STH2005::MotionKey::attachedPart`, the hand motions
  `lte_idle` / `rte_idle`) is skipped too, since it is the main skeleton's wrist bone, posed by the main motion.
- **TextureSet** (`textureSets`, a `PoolMap`, SADX only): the textures of one imported `.gvm` as Texture pool ids in file
  order (`ordered`, what a Ninja `textureIndex` indexes). Named by its path (`SATextureSetKey`, `sonic.gvm`) and imported
  on demand by `addSAInstance` if not loaded. `users` counts the instances using the set; at 0 `TextureSet::destroy`
  removes its textures (`glDeleteTextures`) and bindings. **TextureBinding** (`textureBindings`): an SAMesh's submeshes
  resolved against a set once, when the first instance of that mesh and set is created, then found again by later
  instances (the set keeps `saBindings`, one per mesh): a `textureIndex` past the set's end throws. The same struct is the
  per-submesh table of a Shadow binding. If creating an instance fails halfway, what that call loaded is removed again.
- **STH2005::ONE** (`oneArchives`, a `PoolMap` named by disc path, Shadow only): the archive's bytes and entries plus what
  was decoded from it: `meshes` (models built on first use from the recipe of an `STH2005::ModelKey` by `useMesh`, by name in `meshByName`; meshes keep their recipe (`meshParts`); a key whose name exists with different parts throws), `textureDictionaries`
  (`.TXD` entries, decoded on first use by `useTextureDictionary`, each as ids plus a name lookup) and `bindings` (per
  mesh and dictionary pair, built once by `useBinding`: each submesh's `textureName` looked up in the dictionary, a
  missing name throws). `users` counts the instances using it (`addONEInstance` adds one, the instance's `destroy`
  releases); at 0 `STH2005::ONE::destroy` frees the meshes and removes the dictionaries' textures. `addONEInstance` finds or
  reads the archive from its `STH2005::ArchiveKey` path. Names of meshes and dictionaries are unique inside an archive, and the
  archive is part of the identity (Shadow's `.ONE` archives repeat TXD file names, e.g. `TEXTURE.TXD`). An instance uses
  one archive; whether the original game lets a model use another archive's textures is unknown.
- Split rule for both: shared skeleton/rest data in the resource, animated pose in the instance.

## Importers
All importers take bytes already in memory (or a path for `.sa1mdl`), throw `std::runtime_error` on anything outside
what they support, and carry the evidence for each layout in a comment (decoded from a file, external source, or
hypothesis). None of them contains a model, texture, bone or archive-entry name: those come from the resource keys in `ResourceKeys.h`.
- **`GCDisc`**: opens `.iso`, reads the file table, `readFile(path)` reads one file on request.
- **`Rel`**: applies a `.rel`'s relocations against itself; pointers then equal file offsets.
- **`SAMdl`**: `importSAModel(name, bytes, rootOffset, bigEndian)`. One `SANode` per NJS_OBJECT (depth first),
  one `SASubmesh` per meshset, one vertex per poly corner with position and UV (255 = 1.0). A submesh keeps
  its material's `texture id & 0xFFFF` as `textureIndex`; the `TextureSet` is applied when an instance is created, not by the importer; `rootOffset` comes from an `SAModelKey`. Normals, vertex colors, material
  diffuse/specular and attributes are not read. Limits: Ninja Basic only, non-DX struct sizes, no NPoly meshsets.
  Hypotheses (marked in `SAMdl.cpp` / `SAMesh.h`): rotation order, triangle winding, quad/strip corner order, texture id = index into the `TextureSet`'s file order.
- **`SAMotion`**: `readSAMotion(bytes, offset, nodeCount, bigEndian)` reads an NJS_MOTION (position, rotation, scale
  channels, linear only; anything else throws). Layout from SA Tools, confirmed on Sonic Idle.
- **`Gvm`**: `importGvm(setName, bytes)` decodes a GVM archive into the Texture pool (linear filter) and a new `TextureSet`
  (archive order); returns nothing. Limits: header flags `0xF` only, CMPR and RGB5A3 only, mip level 0 only, sizes in whole tiles.
- **`Txd`**: `importTxd(bytes)` decodes a RenderWare texture dictionary of GameCube textures into the Texture pool by
  name and returns the ids (in order and by name) for the caller to keep, with the file's own filter and addressing. Limits: C4 with an RGB565/RGB5A3 palette only, one mip level, same U and V addressing.
- **`GXColor.h`**: RGB565 and RGB5A3 to RGBA8, shared by `Gvm` and `Txd`.
- **`Dff`**: `readDff(bytes)` reads frames (with HAnim node id), non-native geometry (positions, normals, first UV set,
  one morph target), the material list (color baked into the vertex colors, base texture name) and atomics, into a
  `RenderWareMesh` with one submesh per used material, and the Skin plugin (per-vertex bone indices/weights, inverse bind
  matrices; skin bone i is HAnim hierarchy node i, mapped to the frame with that node id). Skipped: BinMesh, prelit
  colors, MatFX (environment map, UV transform), UV animation, material filter/addressing.
- **`STH2005::ONE` / `Prs`**: `.ONE` archive ("One Ver 0.60") with PRS-compressed entries, `readFile(name)`; also the
  pooled resource described above.
- **`Bon`**: `.BON` skeleton: bone id (matches the DFF HAnim id), name, parent, rest translation/rotation/scale.
- **`STH2005::Model`**: `STH2005::importModel(name, archive, parts)` (adds the mesh to `archive.meshes`, returns its index). `parts[0]` (DFF + BON) is the main model; frames take
  their names from the BON. Each later part with a BON replaces the main model's bone of the same name as its root bone;
  a part without a BON attaches its atomic's frame to the main model bone named in `STH2005::ModelPart::bone`.
  Hypothesis (marked in the file): the game places parts this way. Skin bone frames are remapped with the part's frames.
- **`STH2005::Mtp`**: `readMtp(bytes)` lists a `.MTP`'s motions (name, offset, size, an unknown field); `readMtpMotion(bytes,
  name, bon)` decodes one into an `STH2005::Motion`: per bone of the `.BON` its translation, rotation and scale tracks (keys:
  frame, tangent, value as half floats). Layout observed on `SHADOW.MTP`: 152 of 158 motions fit (9 tracks for the first
  node, 12 for the others, three of those unknown and ignored); `sh_CB`, `sh_CC_a`, `sh_atc`, `sh_atc_eda`, `sh_idle_f` and
  `sh_slow_skate` throw.

## Conventions seen in the code
- `struct` everywhere, public members, `this->` on member access.
- Function parameters end in `_` (`position_`, `pId_`); references are prefixed `r`
  (`rCamera_`, `rInstance`).
- Types and namespaces PascalCase; functions and variables camelCase.
- Origin is carried by namespaces, not name prefixes: `STH2005::ONE`, `Ninja::importGvm`, `GameCube::openGCDisc`,
  `Renderware::readDff` (file names keep the origin only where the folder is flat, e.g. `STH2005Keys.h`, `STH2005Motion.h`).
- `#pragma once`; small utilities as header-only `inline` functions.
- Explicit lifetime methods (`init`/`create`/`destroy`) instead of RAII.
- OpenGL 4.5 direct state access (`glCreateBuffers`, `glNamedBufferStorage`, `glBindTextureUnit`,
  `glVertexArray*`), except `Texture::create`, which uses bind-to-edit.
- Errors: `throw std::runtime_error`, with `std::format` messages.
- Comments: one `/* ... */` block before each logical block with non-obvious behavior; evidence labeled as decoded
  from a file, external, or hypothesis.
- Indentation mixes tabs and 4 spaces; braces are mostly on their own line.

## Incomplete or inconsistent (as of this snapshot)
- No lighting or alpha blending. Material diffuse colors of SA models are ignored; vertex colors are white. Animation is limited to the idle motions wired in `main.cpp`: SADX at 30 frames/s and Shadow at 60 are hypotheses, and the Euler rotation order of Shadow's motions is unverified (see `ONEInstance.cpp`).
- Shadow's MatFX environment maps and UV transforms are not implemented (the environment textures `pl_bl01n`,
  `pl_hnshd0` are loaded but unused); `.UVA` files and the weapon hands/effects are not imported. Only the idle motions are wired (`sh_idle`, `lte_idle`, `rte_idle`); the other hand models (gun, bar, sword) and their motions are not, and the 6 `sh_*` motions listed under `STH2005::Mtp` are rejected.
- SADX welds (vertices shared between limbs) are not imported, so gaps can show at Sonic's joints while he animates.
- Textures filter/wrap: `Txd` applies the TXD's value (nearest) while the DFF materials say linear; unresolved.
- `initGlad` in `Utils.h` is unused; `Renderer::init` loads GLAD itself without registering the debug callback.
- `BoneAnimation.h` is empty; SA instances (an SAMotion) and ONE instances (an STH2005::Motion) set a pose.
- imgui sources are compiled but no backend is built or used; nlohmann/json is not referenced by CMake.
- `PoolMap::getIdByName` only checks for a missing name in Debug builds.
- `STH2005::ONE` owns GPU data (its meshes and textures) and includes `GPU_Globals.h`.
- Shadow resources are counted per archive only: meshes, `.TXD`s and `switchMeshPart` variants stay until the archive is freed.
