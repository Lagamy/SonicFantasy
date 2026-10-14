#pragma once
#include <string>
#include <cstdint>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
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

/*
What writeGLTF writes. A scene is flat tables referenced by index: nodes (children and mesh by index, rootNodes are the scene's), meshes of primitives, materials, images. Positions are written as given: no
conversion, no scale and no winding change, so a reader gets back the importer's own numbers (docs/ARCHITECTURE.md, glTF writer). Optional arrays (normals, uvs, colors, the four source fields below) are
left out when empty, and when present must have one entry per position.
*/
enum class FieldEncoding
{
	/* Attributes _SRC, _SRCFLAG, _SRCIDX (uint32 scalar) and _ORIG (vec3 float). */
	Custom,
	/* TEXCOORD_1 = (SRC, SRCFLAG), TEXCOORD_2 = (ORIG.x, ORIG.y), TEXCOORD_3 = (ORIG.z, SRCIDX), float32; the integers are exact up to 2^24 - 1 and larger ones throw. TEXCOORD_0 stays the uv. */
	TexCoord
};

struct GLTFPrimitive
{
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec2> uvs;
	std::vector<glm::vec4> colors;
	std::vector<uint32_t> indices;
	int material = -1;
	/* Per vertex: what it comes from (SRC with SRCFLAG saying what SRC means), the vertex's index in the source model, and its position before any edit. */
	std::vector<uint32_t> src;
	std::vector<uint32_t> srcFlag;
	std::vector<uint32_t> srcIdx;
	std::vector<glm::vec3> orig;
};

struct GLTFMesh
{
	std::string name;
	std::vector<GLTFPrimitive> primitives;
};

struct GLTFMaterial
{
	std::string name;
	glm::vec4 baseColor = glm::vec4(1.0f);
	/* Index in GLTFScene::images, -1 for none. */
	int image = -1;
};

struct GLTFNode
{
	std::string name;
	std::vector<int> children;
	int mesh = -1;
	glm::vec3 translation = glm::vec3(0.0f);
	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 scale = glm::vec3(1.0f);
	/* Written as the node's "extras" object, every value a string. */
	std::vector<std::pair<std::string, std::string>> extras;
};

struct GLTFScene
{
	std::vector<GLTFNode> nodes;
	std::vector<int> rootNodes;
	std::vector<GLTFMesh> meshes;
	std::vector<GLTFMaterial> materials;
	/* Image uris, relative to the .gltf; the PNGs are written by the caller (writeTexture). */
	std::vector<std::string> images;
};

/*
Writes the scene as rPath_ (.gltf) and, next to it, the same name with .bin. Every primitive is a triangle list with uint32 indices; positions carry min and max. Throws std::runtime_error naming the file for
an array of the wrong size, an index past the vertices, a bad node, mesh, material or image index, or a field too large for the encoding.
*/
void writeGLTF(const std::string& rPath_, const GLTFScene& rScene_, FieldEncoding encoding_);
}
