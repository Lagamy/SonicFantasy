#pragma once
#include <string>
#include <vector>
#include "CollisionMesh.h"
#include "ModelFile.h"
#include "Texture.h"

/*
Reads a glTF 2.0 file (.gltf with its buffers, or .glb) with cgltf, for a level built from glTF files (docs/ENGINE_USAGE.md, engine "gltf"). Every node that has a mesh is visited, with its world transform applied to the
vertices, so the result is in the file's own space: glTF's (right handed, Y up, metres), with no conversion to the games' space. Only triangle lists are read; a primitive of another mode is printed to stderr and
skipped. Both functions throw std::runtime_error naming the file.
*/
namespace External
{
/*
A model for the level's visual: one submesh per primitive, in VertexLayout::standard() (position, normal, uv, color; no skinning). A missing normal is the face normal of the triangles the vertex is in, a missing
uv is (0,0), color is COLOR_0 times the material's baseColorFactor (white without). A submesh's texture is the name of the material's base colour texture, "" without one. uv is not flipped: glTF's v = 0 is the first
row, as for the stored textures. textures gets the images the materials use (decoded to RGBA8, not uploaded) and textureNames their names, side by side; an image's name is its glTF name, else "<modelName>_<index>".
The filter is linear unless the sampler's magFilter is nearest, the wrap repeat unless the sampler clamps or mirrors it (glTF's default is repeat).
*/
ModelData readGLTFModel(const std::string& rPath_, const std::string& rModelName_, std::vector<Graphics::Texture>& rTextures_, std::vector<std::string>& rTextureNames_);

/*
The triangles of the file as collision: all vertices in the file's space, one piece per primitive (flags 0, bounding sphere from its vertices). The file may hold several meshes; call it for each file and append the
results (CollisionMesh::append). The collision system treats triangles as two sided, so the winding does not matter.
*/
Physics::CollisionMesh readGLTFCollision(const std::string& rPath_);
}
