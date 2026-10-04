#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "RenderWareMesh.h"

namespace Graphics
{
/*
One leaf (atomic sector) of a RenderWare world's BSP tree: its bounding box in world space and its meshes, one submesh per
material of the world, like a RenderWareAtomic's. Sectors are the pieces a level is culled and streamed by.
*/
struct RenderWareWorldSector
{
	glm::vec3 boxMin;
	glm::vec3 boxMax;
	std::vector<RenderWareSubmesh> submeshes;
};

/* The visible geometry of a RenderWare world (a stage's .RG1), without the BSP tree above its sectors. */
struct RenderWareWorld
{
	std::vector<RenderWareWorldSector> sectors;
};
}
