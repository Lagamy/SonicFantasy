#pragma once
#include <cstdint>
#include <vector>
#include "PoolId.h"

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
}
