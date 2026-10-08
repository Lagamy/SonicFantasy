#pragma once
#include <string>
#include "CollisionMesh.h"

/*
Our own on-disk collision format for levels that are not from a disc (docs/ARCHITECTURE.md, "External assets"). A collision is two files next to each other: <name>.collision.json (the header) and <name>.collision.bin
(the bytes). Header: "version" (1), "name" (equal to the file name), "vertexCount", "triangleCount", "hasTriangleValues" (bool) and "pieces" (each: "flags", "center", "radius", "firstTriangle", "triangleCount",
the fields of Physics::CollisionPiece). The .bin is little-endian: vertexCount x 3 float32 (level space), triangleCount x 3 u32 indices, and when hasTriangleValues is set triangleCount u32 (CollisionMesh::triangleAttributes).
CollisionMesh::triangleMaterials is not stored (only Shadow's .BSP has it). Reading checks everything against the file sizes and throws std::runtime_error naming the file.
*/
namespace External
{
/* The .bin path that belongs to a .collision.json path. */
std::string collisionBinPath(const std::string& rJsonPath_);

/* Reads <name>.collision.json and its .bin. */
Physics::CollisionMesh readCollision(const std::string& rJsonPath_);

/* Writes both files. name_ is stored in the header and must equal the file name without ".collision.json". */
void writeCollision(const std::string& rJsonPath_, const std::string& rName_, const Physics::CollisionMesh& rMesh_);
}
