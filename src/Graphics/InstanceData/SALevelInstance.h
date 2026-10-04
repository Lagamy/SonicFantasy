#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "Camera.h"
#include "PoolId.h"

namespace Graphics
{
/*
One piece of a SADX land table: its SAInstance and the bounding sphere (center and radius, in level space) that decides when it
is loaded and drawn.
*/
struct SALevelPiece
{
	PoolId saInstanceId;
	glm::vec3 center;
	float radius;
};

/*
Per-level state of an SADX land table: one SAInstance for each visible piece, all sharing the level's shader and
TextureSet. The pieces are SAMeshes of their own (named like any SAMesh), so what a piece needs is loaded and freed
through the SAInstance machinery. destroy removes the SAInstances, which gives back their users.
draw streams the pieces by the camera (see LevelStreaming.h). The pieces' CPU data stays loaded; only the GPU buffers come and go.
*/
struct SALevelInstance
{
	std::vector<SALevelPiece> pieces;

	void draw(const glm::mat4& rBase_, const Camera& rCamera_);
	void destroy();
};
}
