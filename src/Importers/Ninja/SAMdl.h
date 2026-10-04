#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "PoolId.h"
#include "CollisionMesh.h"

namespace Ninja
{
/*
Loads a .sa1mdl file (SA Tools export of a Ninja Basic model) into a new Graphics::SAMesh
and returns its id. Vertex positions, indices and UVs are imported, without textures.
Must be called after the renderer is initialized, since meshes are uploaded to the GPU here.
*/
PoolId importSAMdl(const char* name_, const char* path_);

/*
Loads a Ninja Basic model (non-DX struct sizes) rooted at the NJS_OBJECT at rootOffset_ into a new
Graphics::SAMesh. Pointers inside rBytes_ must be file offsets, as in a .sa1mdl or a relocated .rel.
Each submesh keeps its material's texture id (SASubmesh::textureIndex); the texture set is applied when an instance is created.
*/
PoolId importSAModel(const char* name_, const std::vector<uint8_t>& rBytes_, uint32_t rootOffset_, bool bigEndian_);

/*
Appends the triangles of the model rooted at the NJS_OBJECT at rootOffset_ (same bytes as importSAModel) to rMesh_ as one
Physics::CollisionPiece with the given surface flags and bounding sphere (a land table COL's). Vertices go to level space through
the node hierarchy. Only geometry is read: nothing is uploaded to the GPU and no SAMesh is created.
*/
void appendSACollision(Physics::CollisionMesh& rMesh_, const std::vector<uint8_t>& rBytes_, uint32_t rootOffset_, uint32_t flags_, const glm::vec3& rCenter_, float radius_, bool bigEndian_);
}
