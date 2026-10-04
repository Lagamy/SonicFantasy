#pragma once
#include <cstdint>
#include <vector>
#include "CollisionMesh.h"

namespace Renderware
{
/*
Reads a Shadow collision world (a stage archive's COLI .BSP, a RenderWare World with plain sector data) into a
Physics::CollisionMesh: one CollisionPiece per sector that has triangles (flags 0, its bounding box as the sphere), with
each triangle's raw attribute and material index in triangleAttributes / triangleMaterials. Throws on a sector that is not
plain data (a native .RG1 world) or whose layout does not add up. See CollisionWorld.cpp for the layout.
*/
Physics::CollisionMesh readCollisionWorld(const std::vector<uint8_t>& rBytes_);
}
