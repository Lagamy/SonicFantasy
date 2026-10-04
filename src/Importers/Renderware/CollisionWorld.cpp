#include "CollisionWorld.h"
#include "RwChunk.h"
#include "BinaryRead.h"
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>

namespace Renderware
{
constexpr uint32_t ChunkAtomicSector = 0x09;
constexpr uint32_t ChunkPlaneSector = 0x0A;
constexpr uint32_t ChunkWorld = 0x0B;
constexpr uint32_t ChunkUserData = 0x011F;

/* World format flag bits that add per-vertex arrays to a plain sector (external, RenderWare rpworld.h; the three combinations in the 614 collision worlds are 0x40000041, 0x40000069, 0x40010045). */
constexpr uint32_t WorldFlagNative = 0x01000000;
constexpr uint32_t WorldFlagPrelit = 0x08;
constexpr uint32_t WorldFlagNormals = 0x10;
constexpr uint32_t WorldTexCoordSetsMask = 0x00FF0000;
constexpr uint32_t WorldTexCoordSetsShift = 16;

constexpr uint32_t AtomicSectorStructSize = 44;
constexpr uint32_t UserDataTypeInt = 1;

/*
Layout decoded from the 614 collision .BSP worlds of the stage archives (all of them parse; the sizes below add up on every sector).
World: Struct (flags at +0x24), MaterialList, then the root sector: a plane sector (Struct of 24 bytes, then its two child
sectors) or an atomic sector, then an empty Extension.
Atomic sector Struct (header of 44 bytes, then the data): i32 material window base (0 in every sector), i32 triangle count, i32 vertex count,
bounding box min (3 x f32), max (3 x f32), two words. Data, each one an array over all the vertices (not interleaved per vertex):
positions (3 x f32, level space), then normals (4 bytes each, packed; external, RenderWare) if the world has them, prelit colors
(4 bytes) if it has them, texture coordinates (2 x f32) per set; then the triangles, 4 x u16 each: three vertex indices and the
material index. Triangle winding is unknown. Checked on all 764 sectors of the 614 collision worlds: with the positions read as one
array, every sector's vertices span exactly its bounding box. (Reading them interleaved, with a stride of the sum of the arrays, passes the size
check and the three worlds with prelit colors or texture coordinates (stg0100's COLI_14, 16, 18) then give huge garbage triangles.)
The sector's Extension holds a BinMesh (0x50E), a collision tree (0x11D, probably RenderWare's collision plugin, not read) and
UserData (0x11F, external: RenderWare user data plugin): u32 array count, then per array a u32 name length (with the null), the
name, a u32 type (1 = u32, hypothesis from this data) and a u32 element count, then the elements. The array named "attribute" holds one
u32 per triangle; a sector without triangles has no such array. What the attribute bits mean is unknown; they are kept raw. The files
named STG0201_COLI_NN_.BSP hold attributes that look like text (e.g. 0x730065); they are imported like any other.
*/
static void readSector(const std::vector<uint8_t>& rBytes_, const Chunk& rSector_, uint32_t flags_, Physics::CollisionMesh& rMesh_)
{
	if(rSector_.type == ChunkPlaneSector)
	{
		Chunk header = expectChunk(rBytes_, rSector_.data, ChunkStruct);
		uint32_t cursor = header.end;
		while(cursor < rSector_.end)
		{
			Chunk child = readChunk(rBytes_, cursor);
			if(child.type == ChunkPlaneSector || child.type == ChunkAtomicSector)
			{
				readSector(rBytes_, child, flags_, rMesh_);
			}
			cursor = child.end;
		}
		return;
	}

	Chunk header = expectChunk(rBytes_, rSector_.data, ChunkStruct);
	uint32_t triangleCount = readLE<uint32_t>(rBytes_, header.data + 4);
	uint32_t vertexCount = readLE<uint32_t>(rBytes_, header.data + 8);
	if(triangleCount == 0)
	{
		return;
	}

	uint32_t vertexBytes = 12;
	if(flags_ & WorldFlagNormals) vertexBytes += 4;
	if(flags_ & WorldFlagPrelit) vertexBytes += 4;
	vertexBytes += ((flags_ & WorldTexCoordSetsMask) >> WorldTexCoordSetsShift) * 8;
	uint64_t expected = AtomicSectorStructSize + static_cast<uint64_t>(vertexCount) * vertexBytes + static_cast<uint64_t>(triangleCount) * 8;
	if(header.size != expected)
	{
		throw std::runtime_error(std::format("CollisionWorld: sector at 0x{:X} has a Struct of {} bytes, expected {} for {} vertices and {} triangles (format flags 0x{:X}).", rSector_.data, header.size, expected, vertexCount, triangleCount, flags_));
	}

	uint32_t firstVertex = static_cast<uint32_t>(rMesh_.vertices.size());
	uint32_t vertexData = header.data + AtomicSectorStructSize;
	for(uint32_t i = 0; i < vertexCount; i++)
	{
		uint32_t at = vertexData + i * 12;
		rMesh_.vertices.push_back(glm::vec3(readLE<float>(rBytes_, at), readLE<float>(rBytes_, at + 4), readLE<float>(rBytes_, at + 8)));
	}

	uint32_t firstTriangle = static_cast<uint32_t>(rMesh_.indices.size() / 3);
	uint32_t triangleData = vertexData + vertexCount * vertexBytes;
	for(uint32_t i = 0; i < triangleCount; i++)
	{
		uint32_t at = triangleData + i * 8;
		for(uint32_t corner = 0; corner < 3; corner++)
		{
			uint16_t index = readLE<uint16_t>(rBytes_, at + corner * 2);
			if(index >= vertexCount)
			{
				throw std::runtime_error(std::format("CollisionWorld: triangle {} of the sector at 0x{:X} uses vertex {} of {}.", i, rSector_.data, index, vertexCount));
			}
			rMesh_.indices.push_back(firstVertex + index);
		}
		rMesh_.triangleMaterials.push_back(readLE<uint16_t>(rBytes_, at + 6));
	}

	Chunk extension = expectChunk(rBytes_, header.end, ChunkExtension);
	bool haveAttributes = false;
	uint32_t cursor = extension.data;
	while(cursor < extension.end && !haveAttributes)
	{
		Chunk child = readChunk(rBytes_, cursor);
		cursor = child.end;
		if(child.type != ChunkUserData) continue;

		uint32_t arrayCount = readLE<uint32_t>(rBytes_, child.data);
		uint32_t at = child.data + 4;
		for(uint32_t a = 0; a < arrayCount; a++)
		{
			uint32_t nameLength = readLE<uint32_t>(rBytes_, at);
			if(nameLength == 0 || at + 4 + nameLength > child.end)
			{
				throw std::runtime_error(std::format("CollisionWorld: user data of the sector at 0x{:X} has a name outside its chunk.", rSector_.data));
			}
			std::string name(reinterpret_cast<const char*>(rBytes_.data() + at + 4), nameLength - 1);
			uint32_t type = readLE<uint32_t>(rBytes_, at + 4 + nameLength);
			uint32_t count = readLE<uint32_t>(rBytes_, at + 8 + nameLength);
			at += 12 + nameLength;
			if(type != UserDataTypeInt)
			{
				throw std::runtime_error(std::format("CollisionWorld: user data array {} of the sector at 0x{:X} has type {}, only 1 is known.", name, rSector_.data, type));
			}
			if(at + static_cast<uint64_t>(count) * 4 > child.end)
			{
				throw std::runtime_error(std::format("CollisionWorld: user data array {} of the sector at 0x{:X} runs past its chunk.", name, rSector_.data));
			}
			if(name == "attribute")
			{
				if(count != triangleCount)
				{
					throw std::runtime_error(std::format("CollisionWorld: sector at 0x{:X} has {} triangles but {} attributes.", rSector_.data, triangleCount, count));
				}
				for(uint32_t i = 0; i < count; i++)
				{
					rMesh_.triangleAttributes.push_back(readLE<uint32_t>(rBytes_, at + i * 4));
				}
				haveAttributes = true;
				break;
			}
			at += count * 4;
		}
	}
	if(!haveAttributes)
	{
		throw std::runtime_error(std::format("CollisionWorld: sector at 0x{:X} has {} triangles but no \"attribute\" user data.", rSector_.data, triangleCount));
	}

	glm::vec3 boxMin(readLE<float>(rBytes_, header.data + 12), readLE<float>(rBytes_, header.data + 16), readLE<float>(rBytes_, header.data + 20));
	glm::vec3 boxMax(readLE<float>(rBytes_, header.data + 24), readLE<float>(rBytes_, header.data + 28), readLE<float>(rBytes_, header.data + 32));
	glm::vec3 center = (boxMin + boxMax) * 0.5f;
	rMesh_.pieces.push_back(Physics::CollisionPiece{0, center, glm::length(boxMax - center), firstTriangle, triangleCount});
}

Physics::CollisionMesh readCollisionWorld(const std::vector<uint8_t>& rBytes_)
{
	Chunk world = expectChunk(rBytes_, 0, ChunkWorld);
	Chunk header = expectChunk(rBytes_, world.data, ChunkStruct);
	uint32_t flags = readLE<uint32_t>(rBytes_, header.data + 0x24);
	if(flags & WorldFlagNative)
	{
		throw std::runtime_error(std::format("CollisionWorld: format flags 0x{:X} are those of a native world, not a collision world.", flags));
	}

	Physics::CollisionMesh mesh;
	uint32_t cursor = header.end;
	while(cursor < world.end)
	{
		Chunk child = readChunk(rBytes_, cursor);
		if(child.type == ChunkPlaneSector || child.type == ChunkAtomicSector)
		{
			readSector(rBytes_, child, flags, mesh);
		}
		cursor = child.end;
	}
	return mesh;
}
}
