#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "BasicMesh.h"
#include "Camera.h"
#include "PoolId.h"

namespace Graphics
{
/* One primitive of the level's glTF model: its mesh, the Texture pool id of its material's texture (UninitializedPoolId = untextured) and its bounding sphere in the model's space. */
struct GLTFLevelSubmesh
{
	BasicMesh mesh;
	PoolId textureId;
	glm::vec3 center;
	float radius;
};

/*
Per-level state of a level made from a glTF file: the submeshes, the shader they are drawn with and the textures the instance made for them (in Graphics::textures, removed by destroy). Drawn like the pieces of an
ONELevelInstance without its yaw: each submesh is streamed by the camera (see LevelStreaming.h; its GPU buffers are made when it comes within the render distance and given back when it is far away), a textured one has
its texture bound to unit 0 with hasTexture set.
*/
struct GLTFLevelInstance
{
	PoolId shaderId;
	std::vector<PoolId> textureIds;
	std::vector<GLTFLevelSubmesh> submeshes;

	void draw(const glm::mat4& rBase_, const Camera& rCamera_);
	void destroy();
};
}
