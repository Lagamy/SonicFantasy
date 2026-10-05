#include "SAMdl.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "BinaryRead.h"
#include "CollisionMesh.h"
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

namespace Ninja
{
/*
.sa1mdl header (external, SA Tools ModelFile.cs): bytes 0-6 are the magic "SA1MDL", byte 7 the version,
0x08 the offset of the root NJS_OBJECT. All pointers in the file are offsets from the file start.
The file always uses the non-DX struct sizes (ATTACH 0x28, MESHSET 0x18), even for SADX models.
*/
constexpr uint64_t SA1MdlMagic = 0x4C444D314153;
constexpr uint64_t SA1MdlMagicMask = 0xFFFFFFFFFFFFFF;
constexpr uint8_t SA1MdlMaxVersion = 3;

/* NJS_OBJECT eval flags read here (external, SA Tools Enums.cs ObjectFlags); the transform ones are in SAMesh.h. */
constexpr uint32_t ObjectNoDisplay = 0x08;
constexpr uint32_t ObjectNoChildren = 0x10;

constexpr uint16_t PolyTypeTriangles = 0;
constexpr uint16_t PolyTypeQuads = 1;
constexpr uint16_t PolyTypeNPoly = 2;
constexpr uint16_t PolyTypeStrips = 3;

/*
Hypothesis, unverified: which side of a triangle is the front is unknown.
The renderer culls clockwise faces (glFrontFace(GL_CCW)). If models render inside out, flip this.
*/
constexpr bool FlipWinding = false;

/*
Bytes holding Ninja structs, with pointers stored as offsets into rBytes.
.sa1mdl files are little-endian; GameCube .rel modules (after relocateRel) are big-endian.
*/
struct SAModelSource
{
	const std::vector<uint8_t>& rBytes;
	bool bigEndian;
};

template <typename T>
static T read(const SAModelSource& rSrc_, uint32_t offset_)
{
	return rSrc_.bigEndian ? readBE<T>(rSrc_.rBytes, offset_) : readLE<T>(rSrc_.rBytes, offset_);
}

static glm::vec3 readVec3(const SAModelSource& rSrc_, uint32_t offset_)
{
	return glm::vec3(
		read<float>(rSrc_, offset_),
		read<float>(rSrc_, offset_ + 4),
		read<float>(rSrc_, offset_ + 8)
	);
}

static void addTriangle(Graphics::BasicMesh& rMesh_, uint32_t a_, uint32_t b_, uint32_t c_)
{
	rMesh_.indicies.push_back(a_);
	if(FlipWinding)
	{
		rMesh_.indicies.push_back(c_);
		rMesh_.indicies.push_back(b_);
	}
	else
	{
		rMesh_.indicies.push_back(b_);
		rMesh_.indicies.push_back(c_);
	}
}

/*
Every poly corner becomes its own vertex (no welding), because UVs and vertex colors are stored
per corner in the format. Returns the index of the new vertex in the mesh.
*/
static uint32_t addCorner(Graphics::BasicMesh& rMesh_, const std::vector<glm::vec3>& rPositions_, uint16_t positionIndex_, glm::vec2 uv_)
{
	if(positionIndex_ >= rPositions_.size())
	{
		throw std::runtime_error(std::format("SA1Mdl: poly references vertex {} but the attach has {}.", positionIndex_, rPositions_.size()));
	}
	rMesh_.verticies.push_back(Graphics::Vertex{rPositions_[positionIndex_], glm::vec3(0.0f), uv_, glm::vec4(1.0f)});
	return static_cast<uint32_t>(rMesh_.verticies.size() - 1);
}

/*
UV array of a meshset: one NJS_TEX (s16 u, s16 v) per poly corner, in the order the corners appear in the polys
(strip headers excluded). 255 is 1.0 (external, SA Tools; every UV of Sonic's model is within 0-255). A null array gives 0, 0.
*/
static glm::vec2 readCornerUv(const SAModelSource& rSrc_, uint32_t uvOffset_, uint32_t index_)
{
	if(uvOffset_ == 0)
	{
		return glm::vec2(0.0f);
	}
	return glm::vec2(
		read<int16_t>(rSrc_, uvOffset_ + index_ * 4) / 255.0f,
		read<int16_t>(rSrc_, uvOffset_ + index_ * 4 + 2) / 255.0f
	);
}

/*
NJS_MESHSET (0x18): u16 type (bits 14-15) + material id (bits 0-13), u16 poly count, u32 polys offset,
u32 poly attributes, u32 normals, u32 vertex colors, u32 UVs offset (see readCornerUv).
Polys: triangles 3 x u16, quads 4 x u16, strips u16 header (count = & 0x7FFF, reversed = & 0x8000) then count x u16.
An NPoly meshset (type 2) has the same layout as a strip and is read the same way. Observed on Kiki (_Main.rel 0x333A90, the meshsets at 0x33161C and 0x330ED4): each has one poly
of 6 indices, whose six points lie in one plane and go left, right, left, right ...: read as a strip they cover the convex hexagon they outline, read in order as a loop they
cross themselves (the SA Tools reader handles the two types together, external).
Hypothesis, unverified: quads are strip-ordered (0,1,2)(2,1,3); strips alternate winding every triangle
and the reversed bit flips the starting winding.
Only the UVs are read of the optional arrays (attributes, normals and vertex colors are null in Sonic's model).
*/
static Graphics::SASubmesh readMeshset(const SAModelSource& rSrc_, uint32_t meshsetOffset_, const std::vector<glm::vec3>& rPositions_)
{
	Graphics::SASubmesh submesh;
	Graphics::BasicMesh& rMesh = submesh.mesh;

	uint16_t polyType = read<uint16_t>(rSrc_, meshsetOffset_) >> 14;
	uint16_t polyCount = read<uint16_t>(rSrc_, meshsetOffset_ + 0x02);
	uint32_t cursor = read<uint32_t>(rSrc_, meshsetOffset_ + 0x04);
	uint32_t uvOffset = read<uint32_t>(rSrc_, meshsetOffset_ + 0x14);
	uint32_t uvIndex = 0;

	auto addPolyCorner = [&](uint32_t offset_)
	{
		return addCorner(rMesh, rPositions_, read<uint16_t>(rSrc_, offset_), readCornerUv(rSrc_, uvOffset, uvIndex++));
	};

	for(uint16_t p = 0; p < polyCount; p++)
	{
		if(polyType == PolyTypeTriangles)
		{
			uint32_t a = addPolyCorner(cursor);
			uint32_t b = addPolyCorner(cursor + 2);
			uint32_t c = addPolyCorner(cursor + 4);
			addTriangle(rMesh, a, b, c);
			cursor += 6;
		}
		else if(polyType == PolyTypeQuads)
		{
			uint32_t a = addPolyCorner(cursor);
			uint32_t b = addPolyCorner(cursor + 2);
			uint32_t c = addPolyCorner(cursor + 4);
			uint32_t d = addPolyCorner(cursor + 6);
			addTriangle(rMesh, a, b, c);
			addTriangle(rMesh, c, b, d);
			cursor += 8;
		}
		else if(polyType == PolyTypeStrips || polyType == PolyTypeNPoly)
		{
			uint16_t header = read<uint16_t>(rSrc_, cursor);
			uint16_t count = header & 0x7FFF;
			bool reversed = (header & 0x8000) != 0;
			cursor += 2;

			uint32_t first = static_cast<uint32_t>(rMesh.verticies.size());
			for(uint16_t i = 0; i < count; i++)
			{
				addPolyCorner(cursor);
				cursor += 2;
			}

			for(uint32_t i = 0; i + 2 < count; i++)
			{
				if(((i % 2) == 0) != reversed)
				{
					addTriangle(rMesh, first + i, first + i + 1, first + i + 2);
				}
				else
				{
					addTriangle(rMesh, first + i + 1, first + i, first + i + 2);
				}
			}
		}
	}

	return submesh;
}

/*
ATTACH (0x28): u32 vertices offset, u32 normals offset, i32 vertex count, u32 meshsets offset,
u32 materials offset, u16 meshset count, u16 material count, bounding sphere.
NJS_MATERIAL (0x14): diffuse and specular as 4 bytes B, G, R, A, f32 exponent, u32 whose low 16 bits are the texture id,
u32 attributes. A meshset uses the material of its id (hypothesis, unverified: texture ids index the TextureSet's file order).
The attributes' bit 15 (0x8000) clamps V and bit 16 (0x10000) clamps U, a clamped axis stays on the edge texel and an
unclamped one repeats the texture. Observed on the land tables of _Stg01 and _Stg04 (3635 textured meshsets): with the bit set
the axis' UVs are within one texture (V 100%, U 99%), with it clear they are not (0%), so the unclamped ones tile. The meaning of
the other bits is unknown and they are ignored, except bit 17 (0x20000) and bit 18 (0x40000), read as flip V and flip U: an axis that is not clamped then mirrors the texture at
every repeat (hypothesis: Ninja's NJD_FLAG_FLIP_V / FLIP_U as remembered, not decoded from a file; seen set, with no clamp bit, on the coil of the spring "Rotatable Spring" (0x94262400), whose UVs run
from 2 to 12 times the texture in U and -1 to 1 in V). The diffuse color is ignored too (0xB2B2B2 on all of
Sonic's materials), since the lighting that uses them is not implemented.
*/
static void readAttach(const SAModelSource& rSrc_, uint32_t attachOffset_, Graphics::SANode& rNode_)
{
	uint32_t verticesOffset = read<uint32_t>(rSrc_, attachOffset_);
	int32_t vertexCount = read<int32_t>(rSrc_, attachOffset_ + 0x08);
	uint32_t meshsetsOffset = read<uint32_t>(rSrc_, attachOffset_ + 0x0C);
	uint32_t materialsOffset = read<uint32_t>(rSrc_, attachOffset_ + 0x10);
	uint16_t meshsetCount = read<uint16_t>(rSrc_, attachOffset_ + 0x14);
	uint16_t materialCount = read<uint16_t>(rSrc_, attachOffset_ + 0x16);

	std::vector<glm::vec3> positions;
	positions.reserve(vertexCount);
	for(int32_t i = 0; i < vertexCount; i++)
	{
		positions.push_back(readVec3(rSrc_, verticesOffset + i * 12));
	}

	for(uint16_t m = 0; m < meshsetCount; m++)
	{
		uint32_t meshsetOffset = meshsetsOffset + m * 0x18;
		Graphics::SASubmesh submesh = readMeshset(rSrc_, meshsetOffset, positions);
		if(submesh.mesh.indicies.empty())
		{
			continue;
		}

		/*
		An attach with no materials at all (observed: "Pier Collision", _Stg01.rel 0x213AE0, whose attach has material count 0 and whose meshset names material 0) is a model made
		for collision: its meshsets are kept untextured, so it can be seen as a shape. A meshset that names a material past a non-empty list still throws.
		*/
		uint16_t materialId = read<uint16_t>(rSrc_, meshsetOffset) & 0x3FFF;
		if(materialCount == 0)
		{
			submesh.textureIndex = Graphics::SANoTexture;
		}
		else
		{
			if(materialId >= materialCount)
			{
				throw std::runtime_error(std::format("SA1Mdl: meshset at 0x{:X} uses material {} but the attach has {}.", meshsetOffset, materialId, materialCount));
			}
			submesh.textureIndex = read<uint32_t>(rSrc_, materialsOffset + materialId * 0x14 + 0x0C) & 0xFFFF;
			uint32_t attributes = read<uint32_t>(rSrc_, materialsOffset + materialId * 0x14 + 0x10);
			submesh.clampV = (attributes & 0x8000) != 0;
			submesh.clampU = (attributes & 0x10000) != 0;
			submesh.flipV = (attributes & 0x20000) != 0;
			submesh.flipU = (attributes & 0x40000) != 0;
			submesh.doubleSided = (attributes & 0x800000) != 0;
		}

		submesh.mesh.create();
		rNode_.submeshes.push_back(submesh);
	}
}

/*
Walks an NJS_OBJECT sibling list, depth first (each node's children before its next sibling),
so parents always land in SAMesh::nodes before their children.
NJS_OBJECT (0x34): u32 flags, u32 attach offset, position (0x08), rotation as 3 x i32 BAMS (0x14), scale (0x20),
u32 child (0x2C), u32 sibling (0x30). The transform is composed by Graphics::composeNinjaTransform.
*/
static void readObjects(const SAModelSource& rSrc_, uint32_t objectOffset_, int32_t parent_, Graphics::SAMesh& rSAMesh_)
{
	while(objectOffset_ != 0)
	{
		uint32_t flags = read<uint32_t>(rSrc_, objectOffset_);
		uint32_t attachOffset = read<uint32_t>(rSrc_, objectOffset_ + 0x04);

		Graphics::SANode node;
		node.parent = parent_;
		node.restPosition = readVec3(rSrc_, objectOffset_ + 0x08);
		node.restRotation = glm::ivec3(
			read<int32_t>(rSrc_, objectOffset_ + 0x14),
			read<int32_t>(rSrc_, objectOffset_ + 0x18),
			read<int32_t>(rSrc_, objectOffset_ + 0x1C)
		);
		node.restScale = readVec3(rSrc_, objectOffset_ + 0x20);
		node.flags = flags;
		node.localTransform = Graphics::composeNinjaTransform(node.restPosition, glm::vec3(node.restRotation), node.restScale, flags);
		if(attachOffset != 0 && !(flags & ObjectNoDisplay))
		{
			readAttach(rSrc_, attachOffset, node);
		}
		rSAMesh_.nodes.push_back(node);
		int32_t index = static_cast<int32_t>(rSAMesh_.nodes.size() - 1);

		uint32_t childOffset = read<uint32_t>(rSrc_, objectOffset_ + 0x2C);
		if(childOffset != 0 && !(flags & ObjectNoChildren))
		{
			readObjects(rSrc_, childOffset, index, rSAMesh_);
		}

		objectOffset_ = read<uint32_t>(rSrc_, objectOffset_ + 0x30);
	}
}

/*
Collision counterpart of readAttach: reads the attach's vertices and meshsets with readMeshset and appends the triangles to
rMesh_, each vertex moved to level space by rWorld_. Nothing is uploaded to the GPU and materials are not read.
*/
static void appendAttachCollision(const SAModelSource& rSrc_, uint32_t attachOffset_, const glm::mat4& rWorld_, Physics::CollisionMesh& rMesh_)
{
	uint32_t verticesOffset = read<uint32_t>(rSrc_, attachOffset_);
	int32_t vertexCount = read<int32_t>(rSrc_, attachOffset_ + 0x08);
	uint32_t meshsetsOffset = read<uint32_t>(rSrc_, attachOffset_ + 0x0C);
	uint16_t meshsetCount = read<uint16_t>(rSrc_, attachOffset_ + 0x14);

	std::vector<glm::vec3> positions;
	positions.reserve(vertexCount);
	for(int32_t i = 0; i < vertexCount; i++)
	{
		positions.push_back(readVec3(rSrc_, verticesOffset + i * 12));
	}

	for(uint16_t m = 0; m < meshsetCount; m++)
	{
		Graphics::SASubmesh submesh = readMeshset(rSrc_, meshsetsOffset + m * 0x18, positions);
		uint32_t first = static_cast<uint32_t>(rMesh_.vertices.size());
		for(const Graphics::Vertex& rVertex : submesh.mesh.verticies)
		{
			rMesh_.vertices.push_back(glm::vec3(rWorld_ * glm::vec4(rVertex.position, 1.0f)));
		}
		for(uint32_t index : submesh.mesh.indicies)
		{
			rMesh_.indices.push_back(first + index);
		}
	}
}

/*
Walks an NJS_OBJECT sibling list like readObjects, with each node's world transform (its parent's world * its local
transform, composeNinjaTransform), and appends the triangles of every attach.
Hypothesis, unverified: the NoDisplay flag is ignored here, since a collision-only model may set it.
*/
static void appendObjectsCollision(const SAModelSource& rSrc_, uint32_t objectOffset_, const glm::mat4& rParentWorld_, Physics::CollisionMesh& rMesh_)
{
	while(objectOffset_ != 0)
	{
		uint32_t flags = read<uint32_t>(rSrc_, objectOffset_);
		uint32_t attachOffset = read<uint32_t>(rSrc_, objectOffset_ + 0x04);
		glm::vec3 rotation(
			read<int32_t>(rSrc_, objectOffset_ + 0x14),
			read<int32_t>(rSrc_, objectOffset_ + 0x18),
			read<int32_t>(rSrc_, objectOffset_ + 0x1C)
		);
		glm::mat4 world = rParentWorld_ * Graphics::composeNinjaTransform(readVec3(rSrc_, objectOffset_ + 0x08), rotation, readVec3(rSrc_, objectOffset_ + 0x20), flags);
		if(attachOffset != 0)
		{
			appendAttachCollision(rSrc_, attachOffset, world, rMesh_);
		}

		uint32_t childOffset = read<uint32_t>(rSrc_, objectOffset_ + 0x2C);
		if(childOffset != 0 && !(flags & ObjectNoChildren))
		{
			appendObjectsCollision(rSrc_, childOffset, world, rMesh_);
		}

		objectOffset_ = read<uint32_t>(rSrc_, objectOffset_ + 0x30);
	}
}

void appendSACollision(Physics::CollisionMesh& rMesh_, const std::vector<uint8_t>& rBytes_, uint32_t rootOffset_, uint32_t flags_, const glm::vec3& rCenter_, float radius_, bool bigEndian_)
{
	uint32_t firstIndex = static_cast<uint32_t>(rMesh_.indices.size());
	appendObjectsCollision(SAModelSource{rBytes_, bigEndian_}, rootOffset_, glm::mat4(1.0f), rMesh_);
	uint32_t indexCount = static_cast<uint32_t>(rMesh_.indices.size()) - firstIndex;
	rMesh_.pieces.push_back(Physics::CollisionPiece{flags_, rCenter_, radius_, firstIndex / 3, indexCount / 3});
}

PoolId importSAMdl(const char* name_, const char* path_)
{
	std::ifstream stream(path_, std::ios::binary);
	if(!stream.is_open())
	{
		throw std::runtime_error(std::format("SA1Mdl: failed to open {}", path_));
	}
	std::vector<uint8_t> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

	if((readLE<uint64_t>(file, 0) & SA1MdlMagicMask) != SA1MdlMagic)
	{
		throw std::runtime_error(std::format("SA1Mdl: {} is not an SA1MDL file.", path_));
	}
	if(file[7] > SA1MdlMaxVersion)
	{
		throw std::runtime_error(std::format("SA1Mdl: {} has unsupported version {}.", path_, file[7]));
	}

	return importSAModel(name_, file, readLE<uint32_t>(file, 0x08), false);
}

PoolId importSAModel(const char* name_, const std::vector<uint8_t>& rBytes_, uint32_t rootOffset_, bool bigEndian_)
{
	PoolId saMeshId = Graphics::saMeshes.add(name_);
	Graphics::SAMesh& rSAMesh = Graphics::getSAMesh(saMeshId);
	readObjects(SAModelSource{rBytes_, bigEndian_}, rootOffset_, -1, rSAMesh);

	return saMeshId;
}
}
