#pragma once
#include "PoolId.h"
#include "PoolMap.h"
#include "Entity.h"
#include "Camera.h"
#include "RenderTarget.h"
#include <glad/glad.h>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <set>
#include <string>
#include <unordered_set>

namespace Graphics
{
/*
The pictures of the prefabs for the PrefabPanel: get(prefab) is a texture of Size x Size pixels, drawn once while its tile is on screen; endFrame deletes the pictures of the tiles that were not asked for in a frame, and they are drawn again when the tile comes back.
A prefab with a model is drawn as a real entity made by spawnPrefab (API.h) in a pool of its own (entities, not Globals::Game::entities: the entity has only its model, texture and motion, see spawnPrefab, and is removed again after the
draw), by a camera of its own that looks at the middle of the entity's bounding sphere from a fixed direction (up and to the front) from the distance at which the sphere fills the view, so the picture is as big as the sphere (which has a margin, see BoundingSphere) allows.
A prefab without a model, or one that could not be made, is a cube of the same framing, dark blue if it only has collision, dark red if it only has behaviour, dark purple if it has both and dark grey if it has neither.
Everything is drawn into one shared RenderTarget and copied into a texture of its own. At most PerFrame pictures are made per frame (beginFrame resets the count), because making one can load a model and its textures from the disc; get returns 0 for a prefab
whose turn has not come, and the panel tries again next frame. Only the tiles the panel shows ask. Drawing leaves the window's framebuffer and viewport bound. Editor::prefabPanel owns it.
*/
struct PrefabThumbnails
{
	static constexpr uint32_t Size = 128;
	static constexpr int PerFrame = 4;

	/* Makes the target, the camera and the cube meshes. After the renderer (Renderer::init adds the Default shader) and the window. */
	void init();

	/* Starts a frame's quota. */
	void beginFrame();

	/* The prefab's texture (display space, bottom row first like a render target's: flip the UVs), or 0 if it is not made yet. */
	GLuint get(PoolId prefabId_, bool prefetch_ = false);

	/* A picture of a model of the discs with no prefab (the AssetPanel's model tiles; source_ as AssetThumbnails::get takes it, see renderModel in the .cpp): the same framing, a new texture the caller owns and deletes with glDeleteTextures; 0 if it can't be drawn. Counts against nothing: the caller keeps its own quota. */
	GLuint renderModel(int game_, const std::string& source_, bool textured_);

	/* Deletes the pictures of the prefabs get was not called for since beginFrame (their tiles are further off screen than the panel's margin, in a closed folder or in another tab). prefetch_ marks a tile off screen but inside the margin (PrefabPanel): it keeps its picture and makes one only while at most half of PerFrame is used, so the tiles on screen are served first; they are drawn again when the tile comes back. */
	void endFrame();

	/* Deletes the pictures of the External prefabs and of slots whose prefab is gone: their model or texture changed, or the slot is a new prefab's (PrefabPanel calls it when prefabRevision changes). */
	void forgetExternal();

	void destroy();

private:
	/* The cubes, one per colour: blue, red, purple, grey. */
	static constexpr int CubeCount = 4;

	PoolMap<Entity> entities = ("ThumbnailEntities");
	RenderTarget target;
	Camera camera;
	std::array<PoolId, CubeCount> cubeMeshes;
	std::unordered_map<uint32_t, GLuint> cache;
	std::unordered_set<uint32_t> usedThisFrame;
	int madeThisFrame = 0;
	std::set<std::string> kept;

	void makeCube(int index_, const char* name_, const glm::vec3& rColor_);
	GLuint render(PoolId prefabId_);
	GLuint draw(PoolId entityId_, int cube_);
	const char* keep(const std::string& text_);
};
}
