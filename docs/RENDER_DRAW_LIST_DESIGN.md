# Unified draw list and instancing (design, not built)

Written 2026-10-04 from measurements on Emerald Coast (SADX) and Westopolis / stg0100 (Shadow). Nothing here is implemented. The decision was all or nothing: a renderer where only one of the four draw paths is smarter than the others is worse than either, so no partial version (the Shadow-only texture sort was tried and reverted).

## Why it might be wanted
Measured with the counters of `RenderStats.h` (commented out in `main.cpp`; see "Performance counters"):
- Release, up to ~1900 draws and 54k triangles a frame: CPU render 0.02 to 0.5 ms, GPU under 0.4 ms. The renderer is not a bottleneck at these scene sizes (RX 580).
- Every submesh has its own VAO: VAO changes = draws. Sorting cannot fix that; only sharing buffers or instancing does.
- Texture binds: about 2/3 changes in entities (Emerald Coast), 90 to 99% in the Shadow level drawn in file order (a stable sort by texture cut them to about a third of the calls).
- Program changes are about 0.
- Build it only when a larger scene is CPU-bound in Release. Until then this is the plan, not the work.

## Today: four draw paths
`SAInstance::draw`, `ONEInstance::draw` (entities, a matrix per node or frame, skinned atomics with a bone upload), `SALevelInstance::draw` (a `SAInstance` per piece) and `ONELevelInstance::draw` (one matrix, submeshes per piece). Each binds its own state and issues `glDrawElements` per submesh.

## Design
One path for everything: **collect, sort, submit**.

1. **Collect.** Every drawable adds draw items to a per-frame vector instead of drawing: `{shader, mesh (VAO), texture, wrap, model matrix or matrix range, skinned bone range}`. Entities (both engines) and level pieces use the same item. The level streaming (`levelPieceAction`) and the entity culling (spatial index, frustum, distance) stay as the producers; only what they do with a visible thing changes.
2. **Sort** the items by (shader, texture, mesh) with a key packed in a `uint64_t`. Blending is not sorted today and stays so; translucent items would later get their own pass sorted back to front.
3. **Submit** by walking the sorted items with state caching (program, texture, VAO bound only on change). A run of items with the same mesh and texture becomes one `glDrawElementsInstanced`, the matrices read from a per-frame buffer by `gl_InstanceID` (an SSBO, like the bone buffer at binding 1) instead of the `model` uniform.

## What it costs
- Meshes: an `SAMesh` node and a `RenderWareMesh` atomic are one draw item per submesh; the node's world matrix is part of the item. A multi-node SADX model is several items, each with its own matrix, so it instances only when other entities share the same mesh (the matrices differ per node, but the run key is the mesh and node).
- Skinned Shadow models: the bone matrices are per instance, so a skinned item carries a range in a per-frame bone buffer and draws alone (instance count 1) until bone data is indexed by instance. Characters are few; this is where the first version stops short, as a stated limit of the draw item, not as a second path.
- Shaders: `SA` and `STH2005` read `model` from the instance buffer instead of the uniform (`modelLocation` goes away), `skinned` and `hasTexture` become per item state.
- Texture wrap: SA submeshes set the wrap on the texture object before drawing (`Texture::appliedWrapS/T`); a draw item has to carry the wrap and a texture with disagreeing wraps splits the run, or the wrap becomes a sampler object (better: a few samplers, bound with the texture).
- Pose building (already lazy, `buildPose` in draw) moves to the collect step.
- Level pieces keep their streaming (GPU buffers created and freed by distance); an unloaded mesh must not be in any item.
- Memory: per-frame vectors kept between frames, as the draw functions already do with `static` scratch vectors.

## Order of work if it is wanted
1. A draw item and the sorted submit with state caching only, all four paths producing items (no instancing yet): the same pictures with fewer binds. Check with the counters.
2. Matrices in a per-frame buffer; shaders read them by instance index (still one instance per draw).
3. Merge runs of equal (mesh, texture, wrap) into instanced draws.
4. Later, with the sorted list as the base: a translucent pass, GPU culling and multi-draw indirect, or a persistent bind tree (the idea recorded in the project notes) if per-leaf buffers become worth their migration cost.

## Not decided
- Whether meshes should share one big vertex buffer (fewer VAO binds even without instancing) or stay one VAO per submesh.
- Sampler objects for texture wrap.
- Whether the collision lines, text and ImGui passes stay separate (they are today).

## Performance counters (commented out)
`src/Graphics/RenderStats.h` holds the counters; the call sites in `BasicMesh::draw`, `SAInstance::draw`, `ONEInstance::draw`, `ONELevelInstance::draw` and `Entity::draw` still increment them (a few integer adds). The report in `main.cpp` (`statsOn`, the `GL_TIME_ELAPSED` queries, the timers and the once-a-second `[stats]` line to stderr) is commented out: uncomment the marked lines in `main()` and run with `SF_STATS=1`. It prints fps, the milliseconds of gameplay, entity update, render submission, debug UI and swap, the GPU time of the render, draws, triangles, entities and level submeshes, and calls / changes of program, VAO and texture binds, uniform and bone uploads.
