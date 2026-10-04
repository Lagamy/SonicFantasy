#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "BasicMesh.h"
#include "Camera.h"
#include "PoolId.h"

namespace Graphics
{
/* One mesh of a level piece and the Texture pool id of its material's texture (UninitializedPoolId = untextured). */
struct ONELevelSubmesh
{
	BasicMesh mesh;
	PoolId textureId;
};

/*
One piece of a Shadow level: a sector of one of its worlds. center and radius are the bounding sphere of the sector's box,
in the world's own space (see ONEInstance.h, RenderWareYawDegrees), which decides when the piece is loaded and drawn.
*/
struct ONELevelPiece
{
	glm::vec3 center;
	float radius;
	std::vector<ONELevelSubmesh> submeshes;
};

/*
Per-level state of a Shadow stage: the pieces of all its worlds (every sector of every .RG1 of its section archives), the
shader they are drawn with and the archive the textures are from. The instance holds one user of that archive
(Graphics::oneArchives), released by destroy together with the pieces' GPU buffers.
The pieces own their data (they were built for this level only); draw streams them by the camera (see LevelStreaming.h),
uploading a piece's meshes when it comes within the render distance and freeing them when it is far away.
*/
struct ONELevelInstance
{
	PoolId shaderId;
	PoolId archiveId;
	std::vector<ONELevelPiece> pieces;

	void draw(const glm::mat4& rBase_, const Camera& rCamera_);
	void destroy();
};
}
