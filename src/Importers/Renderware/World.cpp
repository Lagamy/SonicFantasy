#include "World.h"
#include "RwChunk.h"
#include "RwMaterial.h"
#include "BinaryRead.h"
#include <cstring>
#include <format>
#include <stdexcept>
#include <unordered_map>

namespace Renderware
{
constexpr uint32_t ChunkAtomicSector = 0x09;
constexpr uint32_t ChunkPlaneSector = 0x0A;
constexpr uint32_t ChunkWorld = 0x0B;
constexpr uint32_t ChunkBinMesh = 0x050E;
constexpr uint32_t ChunkNativeData = 0x0510;

constexpr uint32_t PlatformGameCube = 6;

/* GX vertex attribute ids (external, libogc gx.h: GX_VA_POS, GX_VA_CLR0, GX_VA_TEX0, GX_VA_TEX1) and index types (GX_INDEX8, GX_INDEX16). */
constexpr uint8_t AttributePosition = 9;
constexpr uint8_t AttributeColor0 = 11;
constexpr uint8_t AttributeTexCoord0 = 13;
constexpr uint8_t AttributeTexCoord1 = 14;
constexpr uint8_t IndexTypeU8 = 2;
constexpr uint8_t IndexTypeU16 = 3;

/* GX display list command for a triangle strip (external, libogc gx.h: GX_TRIANGLESTRIP). */
constexpr uint8_t CommandTriangleStrip = 0x98;

/*
Whether the sectors' vertex colors (color 0, the baked lighting: observed in stg0100, where they vary from black to white
and only a handful are white) are multiplied into the vertex color. With false the vertex color is the material's alone, so
the texture is drawn without the baked light. Hypothesis, unverified: the colors hold nothing but lighting (they may also
carry tints or ambient occlusion), and the textures have no light painted into them.
*/
constexpr bool UseBakedColors = true;

/*
Hypothesis, unverified: which side of a triangle of a strip is the front is unknown. Strip triangles alternate
winding (the odd ones are swapped), and the renderer culls clockwise faces (glFrontFace(GL_CCW)). If the level renders
inside out, flip this.
*/
constexpr bool FlipWinding = false;

/*
World layout (external, gtamods RpWorld; confirmed on Shadow the Hedgehog's stage files, see below). A .RG1 file is a
chunk of type 0x29 (a name; skipped), the World chunk (0x0B) and an empty chunk (0x2A). The World holds a Struct (u32 root
is an atomic sector, f32 x3 inverse origin, u32 triangle count, vertex count, plane sector count, atomic sector count,
collision sector size, format flags, then the bounding box, 64 bytes), a MaterialList, the root sector and an Extension. A
PlaneSector is a Struct (24 bytes) followed by its two children, PlaneSector or AtomicSector chunks; the atomic sectors are
the leaves. An AtomicSector is a Struct (44 bytes: i32 material window base, i32 triangle count, i32 vertex count, bounding
box minimum and maximum as f32 x3 each, two words) and an Extension.
Format flags 0x01000000 (native) mean the vertices are not in the Struct but in the Extension (observed on all 3092 sectors
with geometry in the 871 stage archives of the disc, every .RG1; the plain layout is only in the collision .BSP files).
The Extension then holds two chunks: BinMesh (0x50E: u32 flags, u32 mesh count, u32 total index count, then per mesh u32
index count and u32 material index; the counts are not the display lists' counts, which are not used) and NativeData (0x510)
whose Struct is the sector's data, in GameCube order:
0x00 u32 little-endian platform (6), 0x04 u32 little-endian header size H, 0x08 u32 little-endian payload size, then
big-endian from 0x0C: one unknown word, one unknown word (1 or 0), u32 attribute count N, N x {u32 array offset, u8 GX
attribute id, u8 stride, u8 GX index type, u8 0}, then one {u32 display list offset, u32 display list size} per BinMesh
mesh, then the display lists (starting at H + 0xC), then the attribute arrays, whose offsets are from the display lists'
start. The attributes seen are position (id 9: f32 x3), color 0 (id 11: RGBA8), texture coordinate 0 (id 13: f32 x2) and
sometimes texture coordinate 1 (id 14: f32 x2); the index type says whether the display lists hold an 8 bit (2) or 16 bit
(3) index per attribute. A display list is a sequence of commands, 0x98 (triangle strip) followed by u16 vertex count and
that many vertices (one index per attribute, in the attribute order), padded with zero bytes. Meshes and BinMesh entries
pair in order. The arrays are padded to 32 bytes, so their lengths are not the element counts.
The whole layout was checked on every sector: display lists end exactly where their size says, indices are inside their
arrays, material indices are inside the world's list and the referenced positions are inside the sector's bounding box.
Hypothesis, unverified: the color of a vertex is color 0 times the material's color; texture coordinate 1 (a second
set, likely a lightmap) is read past and ignored.
*/
struct SectorAttribute
{
	uint8_t id;
	uint8_t stride;
	uint8_t indexType;
	uint32_t offset;
	uint32_t count;
};

static uint32_t readIndex(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint8_t indexType_)
{
	return indexType_ == IndexTypeU16 ? readBE<uint16_t>(rBytes_, offset_) : readBE<uint8_t>(rBytes_, offset_);
}

/*
Builds the sector's submeshes from its native data: rBytes_ is the whole file, data the NativeData Struct and binMesh the
BinMesh chunk. One BasicMesh per mesh of the BinMesh; a vertex is one distinct combination of attribute indices.
*/
static std::vector<Graphics::RenderWareSubmesh> readNativeSector(const std::vector<uint8_t>& rBytes_, const Chunk& rData_, const Chunk& rBinMesh_, const std::vector<RwMaterial>& rMaterials_)
{
	uint32_t base = rData_.data;
	if(rData_.size < 0x18 || readLE<uint32_t>(rBytes_, base) != PlatformGameCube)
	{
		throw std::runtime_error(std::format("World: native data at 0x{:X} is not for platform {} (GameCube).", base, PlatformGameCube));
	}
	uint32_t headerSize = readLE<uint32_t>(rBytes_, base + 4);
	uint32_t attributeCount = readBE<uint32_t>(rBytes_, base + 0x14);
	uint32_t meshCount = readLE<uint32_t>(rBytes_, rBinMesh_.data + 4);
	if(attributeCount == 0 || attributeCount > 4 || meshCount == 0 || meshCount > 4096 || 12 + static_cast<uint64_t>(meshCount) * 8 > rBinMesh_.size)
	{
		throw std::runtime_error(std::format("World: sector at 0x{:X} has {} attributes and {} meshes.", base, attributeCount, meshCount));
	}

	uint32_t table = 0x18 + attributeCount * 8;
	uint32_t listsStart = table + meshCount * 8;
	if(headerSize + 0xC != listsStart)
	{
		throw std::runtime_error(std::format("World: sector at 0x{:X} has header size 0x{:X}, expected 0x{:X}.", base, headerSize, listsStart - 0xC));
	}

	std::vector<SectorAttribute> attributes;
	for(uint32_t a = 0; a < attributeCount; a++)
	{
		uint32_t entry = base + 0x18 + a * 8;
		SectorAttribute attribute;
		attribute.offset = readBE<uint32_t>(rBytes_, entry);
		attribute.id = readBE<uint8_t>(rBytes_, entry + 4);
		attribute.stride = readBE<uint8_t>(rBytes_, entry + 5);
		attribute.indexType = readBE<uint8_t>(rBytes_, entry + 6);
		attribute.count = 0;
		bool known = (attribute.id == AttributePosition && attribute.stride == 12) || (attribute.id == AttributeColor0 && attribute.stride == 4) ||
			((attribute.id == AttributeTexCoord0 || attribute.id == AttributeTexCoord1) && attribute.stride == 8);
		if(!known || (attribute.indexType != IndexTypeU8 && attribute.indexType != IndexTypeU16))
		{
			throw std::runtime_error(std::format("World: sector at 0x{:X} has attribute {} with stride {} and index type {}, which is not supported.", base, attribute.id, attribute.stride, attribute.indexType));
		}
		attributes.push_back(attribute);
	}
	/* An array runs to the start of the next one, the last to the end of the data. */
	for(uint32_t a = 0; a < attributeCount; a++)
	{
		uint32_t end = (a + 1 < attributeCount) ? attributes[a + 1].offset : rData_.size - listsStart;
		if(end < attributes[a].offset || listsStart + end > rData_.size)
		{
			throw std::runtime_error(std::format("World: sector at 0x{:X} has attribute arrays outside its data.", base));
		}
		attributes[a].count = (end - attributes[a].offset) / attributes[a].stride;
	}

	std::vector<Graphics::RenderWareSubmesh> submeshes;
	for(uint32_t m = 0; m < meshCount; m++)
	{
		uint32_t material = readLE<uint32_t>(rBytes_, rBinMesh_.data + 12 + m * 8 + 4);
		if(material >= rMaterials_.size())
		{
			throw std::runtime_error(std::format("World: mesh {} of the sector at 0x{:X} uses material {}, but the world has {}.", m, base, material, rMaterials_.size()));
		}
		uint32_t listOffset = readBE<uint32_t>(rBytes_, base + table + m * 8);
		uint32_t listSize = readBE<uint32_t>(rBytes_, base + table + m * 8 + 4);
		if(static_cast<uint64_t>(listsStart) + listOffset + listSize > rData_.size)
		{
			throw std::runtime_error(std::format("World: display list of mesh {} of the sector at 0x{:X} is outside its data.", m, base));
		}

		Graphics::RenderWareSubmesh submesh;
		submesh.textureName = rMaterials_[material].textureName;
		std::unordered_map<uint64_t, uint32_t> vertexByIndices;

		uint32_t cursor = base + listsStart + listOffset;
		uint32_t end = cursor + listSize;
		while(cursor < end)
		{
			uint8_t command = readBE<uint8_t>(rBytes_, cursor++);
			if(command == 0)
			{
				continue;
			}
			if(command != CommandTriangleStrip)
			{
				throw std::runtime_error(std::format("World: display list command 0x{:X} at 0x{:X} is not supported.", command, cursor - 1));
			}
			uint32_t vertexCount = readBE<uint16_t>(rBytes_, cursor);
			cursor += 2;

			std::vector<uint32_t> strip;
			for(uint32_t v = 0; v < vertexCount; v++)
			{
				uint64_t key = 0;
				uint32_t positionIndex = 0;
				uint32_t colorIndex = 0;
				uint32_t uvIndex = 0;
				for(size_t a = 0; a < attributes.size(); a++)
				{
					uint32_t index = readIndex(rBytes_, cursor, attributes[a].indexType);
					cursor += attributes[a].indexType == IndexTypeU16 ? 2 : 1;
					if(index >= attributes[a].count)
					{
						throw std::runtime_error(std::format("World: display list at 0x{:X} uses index {} of attribute {}, which has {}.", cursor, index, attributes[a].id, attributes[a].count));
					}
					key = (key << 16) | ((attributes[a].id == AttributeColor0 && !UseBakedColors) ? 0 : index);
					if(attributes[a].id == AttributePosition) positionIndex = index;
					if(attributes[a].id == AttributeColor0) colorIndex = index;
					if(attributes[a].id == AttributeTexCoord0) uvIndex = index;
				}

				auto found = vertexByIndices.find(key);
				if(found == vertexByIndices.end())
				{
					Graphics::Vertex vertex{};
					for(const SectorAttribute& rAttribute : attributes)
					{
						uint32_t array = base + listsStart + rAttribute.offset;
						if(rAttribute.id == AttributePosition)
						{
							uint32_t at = array + positionIndex * 12;
							vertex.position = glm::vec3(readBE<float>(rBytes_, at), readBE<float>(rBytes_, at + 4), readBE<float>(rBytes_, at + 8));
						}
						if(rAttribute.id == AttributeTexCoord0)
						{
							uint32_t at = array + uvIndex * 8;
							vertex.uv = glm::vec2(readBE<float>(rBytes_, at), readBE<float>(rBytes_, at + 4));
						}
					}
					vertex.color = rMaterials_[material].color;
					for(const SectorAttribute& rAttribute : attributes)
					{
						if(rAttribute.id != AttributeColor0 || !UseBakedColors) continue;
						uint32_t at = base + listsStart + rAttribute.offset + colorIndex * 4;
						vertex.color *= glm::vec4(readBE<uint8_t>(rBytes_, at), readBE<uint8_t>(rBytes_, at + 1), readBE<uint8_t>(rBytes_, at + 2), readBE<uint8_t>(rBytes_, at + 3)) / 255.0f;
					}
					found = vertexByIndices.emplace(key, static_cast<uint32_t>(submesh.mesh.verticies.size())).first;
					submesh.mesh.verticies.push_back(vertex);
				}
				strip.push_back(found->second);
			}

			/* Alternate triangles of a strip are swapped so that all face the same way. A triangle with a repeated vertex (a strip join) is dropped. */
			for(uint32_t i = 0; i + 2 < strip.size(); i++)
			{
				uint32_t a = strip[i];
				uint32_t b = strip[i + 1];
				uint32_t c = strip[i + 2];
				const glm::vec3& rA = submesh.mesh.verticies[a].position;
				const glm::vec3& rB = submesh.mesh.verticies[b].position;
				const glm::vec3& rC = submesh.mesh.verticies[c].position;
				if(rA == rB || rB == rC || rA == rC) continue;
				bool swapped = (i % 2 == 1) != FlipWinding;
				submesh.mesh.indicies.push_back(a);
				submesh.mesh.indicies.push_back(swapped ? c : b);
				submesh.mesh.indicies.push_back(swapped ? b : c);
			}
		}
		if(cursor != end)
		{
			throw std::runtime_error(std::format("World: display list of mesh {} of the sector at 0x{:X} ends past its size.", m, base));
		}

		if(!submesh.mesh.indicies.empty())
		{
			submeshes.push_back(std::move(submesh));
		}
	}
	return submeshes;
}

static void readSector(const std::vector<uint8_t>& rBytes_, const Chunk& rSector_, const std::vector<RwMaterial>& rMaterials_, Graphics::RenderWareWorld& rWorld_)
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
				readSector(rBytes_, child, rMaterials_, rWorld_);
			}
			cursor = child.end;
		}
		return;
	}

	Chunk header = expectChunk(rBytes_, rSector_.data, ChunkStruct);
	int32_t vertexCount = readLE<int32_t>(rBytes_, header.data + 8);
	if(vertexCount == 0)
	{
		return;
	}
	if(header.size != 44)
	{
		throw std::runtime_error(std::format("World: sector at 0x{:X} holds its data in its Struct ({} bytes), which is not supported (only native sectors).", rSector_.data, header.size));
	}

	Chunk extension = expectChunk(rBytes_, header.end, ChunkExtension);
	bool haveBinMesh = false;
	bool haveNative = false;
	Chunk binMesh{};
	Chunk native{};
	uint32_t cursor = extension.data;
	while(cursor < extension.end)
	{
		Chunk child = readChunk(rBytes_, cursor);
		if(child.type == ChunkBinMesh) { binMesh = child; haveBinMesh = true; }
		if(child.type == ChunkNativeData) { native = child; haveNative = true; }
		cursor = child.end;
	}
	if(!haveBinMesh || !haveNative)
	{
		throw std::runtime_error(std::format("World: sector at 0x{:X} has {} vertices, but no BinMesh and native data.", rSector_.data, vertexCount));
	}

	Graphics::RenderWareWorldSector sector;
	sector.boxMin = glm::vec3(readLE<float>(rBytes_, header.data + 12), readLE<float>(rBytes_, header.data + 16), readLE<float>(rBytes_, header.data + 20));
	sector.boxMax = glm::vec3(readLE<float>(rBytes_, header.data + 24), readLE<float>(rBytes_, header.data + 28), readLE<float>(rBytes_, header.data + 32));
	Chunk nativeStruct = expectChunk(rBytes_, native.data, ChunkStruct);
	sector.submeshes = readNativeSector(rBytes_, nativeStruct, binMesh, rMaterials_);
	if(!sector.submeshes.empty())
	{
		rWorld_.sectors.push_back(std::move(sector));
	}
}

Graphics::RenderWareWorld readWorld(const std::vector<uint8_t>& rBytes_)
{
	uint32_t cursor = 0;
	Chunk world{};
	bool found = false;
	while(cursor + 12 <= rBytes_.size())
	{
		Chunk chunk = readChunk(rBytes_, cursor);
		if(chunk.type == ChunkWorld) { world = chunk; found = true; break; }
		cursor = chunk.end;
	}
	if(!found)
	{
		throw std::runtime_error("World: no World chunk found.");
	}

	Chunk header = expectChunk(rBytes_, world.data, ChunkStruct);
	uint32_t flags = readLE<uint32_t>(rBytes_, header.data + 0x24);
	if(!(flags & 0x01000000))
	{
		throw std::runtime_error(std::format("World: format flags 0x{:X} are not those of a native world.", flags));
	}

	std::vector<RwMaterial> materials;
	Graphics::RenderWareWorld result;
	cursor = header.end;
	while(cursor < world.end)
	{
		Chunk child = readChunk(rBytes_, cursor);
		if(child.type == ChunkMaterialList)
		{
			materials = readMaterialList(rBytes_, child);
		}
		else if(child.type == ChunkPlaneSector || child.type == ChunkAtomicSector)
		{
			readSector(rBytes_, child, materials, result);
		}
		cursor = child.end;
	}
	return result;
}
}
