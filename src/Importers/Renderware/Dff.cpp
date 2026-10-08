#include "Dff.h"
#include "RwChunk.h"
#include "RwMaterial.h"
#include "BinaryRead.h"
#include "GPU_Globals.h"
#include <algorithm>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

namespace Renderware
{
/*
RenderWare binary stream (external, gtamods wiki; confirmed on Shadow's SHADOW_BODY.DFF), little-endian.
Every chunk starts with u32 type, u32 size of its data, u32 library version stamp. Container chunks hold
child chunks; their fixed fields live in a leading Struct child.
*/
constexpr uint32_t ChunkFrameList = 0x0E;
constexpr uint32_t ChunkGeometry = 0x0F;
constexpr uint32_t ChunkClump = 0x10;
constexpr uint32_t ChunkAtomic = 0x14;
constexpr uint32_t ChunkGeometryList = 0x1A;
constexpr uint32_t ChunkHAnim = 0x11E;
constexpr uint32_t ChunkSkin = 0x116;

/* Geometry format flags (external, gtamods). */
constexpr uint32_t GeometryTextured = 0x04;
constexpr uint32_t GeometryPrelit = 0x08;

/*
A prelit color whose red, green and blue are all below this (of 255) is not used: the vertex stays white. Hypothesis (a guess of the limit; not checked against the game): such a color is
black because the object is meant to be lit by the game's lights, which this engine does not have yet (observed: DOOMSEYE.DFF's body is prelit (0, 0, 0) and its eye (255, 255, 255), and with
the colors multiplied the body came out black; FRUIT's are (13, 0, 17) and (3, 0, 0)), while colors like HINTRING00's (0, 127, 127) are baked tints.
*/
constexpr uint8_t PrelitBlackLimit = 32;
constexpr uint32_t GeometryTextured2 = 0x80;
constexpr uint32_t GeometryNative = 0x01000000;

/*
Hypothesis, unverified: which side of a triangle is the front is unknown.
The renderer culls clockwise faces (glFrontFace(GL_CCW)). If the model renders inside out, flip this.
*/
constexpr bool FlipWinding = false;

/*
FrameList Struct: u32 frame count, then 56 bytes per frame: 3x3 rotation as right, up, at vectors
(9 floats), position (3 floats), i32 parent (-1 = root), u32 flags.
Then one Extension chunk per frame, in frame order. The only one read is HAnim (external, gtamods:
u32 version, i32 node id, u32 node count), whose node id becomes boneId; frames without it get -1.
The frame whose HAnim has a node count > 0 holds the hierarchy (external, gtamods; confirmed on SHADOW_BODY.DFF, 30 nodes,
and SHADOW_HAND_L.DFF, 7): u32 flags, u32 key frame size, then per node i32 node id, i32 node index, u32 flags.
Its node ids, in order, go to rHierarchy_: skin bone i is hierarchy node i.
Hypothesis, unverified: right/up/at are the matrix columns (RenderWare uses row vectors, so its rows
become our columns).
*/
static void readFrameList(const std::vector<uint8_t>& rBytes_, const Chunk& rFrameList_, Graphics::RenderWareMesh& rMesh_, std::vector<int32_t>& rHierarchy_)
{
	Chunk data = expectChunk(rBytes_, rFrameList_.data, ChunkStruct);
	uint32_t count = readLE<uint32_t>(rBytes_, data.data);

	for(uint32_t i = 0; i < count; i++)
	{
		uint32_t frame = data.data + 4 + i * 56;
		float m[12];
		for(int k = 0; k < 12; k++)
		{
			m[k] = readLE<float>(rBytes_, frame + k * 4);
		}
		int32_t parent = readLE<int32_t>(rBytes_, frame + 48);
		if(parent >= static_cast<int32_t>(i))
		{
			throw std::runtime_error(std::format("Dff: frame {} has parent {}, not stored before it.", i, parent));
		}

		glm::mat4 local(
			glm::vec4(m[0], m[1], m[2], 0.0f),
			glm::vec4(m[3], m[4], m[5], 0.0f),
			glm::vec4(m[6], m[7], m[8], 0.0f),
			glm::vec4(m[9], m[10], m[11], 1.0f)
		);
		rMesh_.frames.push_back(Graphics::RenderWareFrame{parent, local, -1, ""});
	}

	uint32_t cursor = data.end;
	for(uint32_t i = 0; i < count; i++)
	{
		Chunk extension = expectChunk(rBytes_, cursor, ChunkExtension);
		uint32_t pluginCursor = extension.data;
		while(pluginCursor < extension.end)
		{
			Chunk plugin = readChunk(rBytes_, pluginCursor);
			if(plugin.type == ChunkHAnim)
			{
				rMesh_.frames[i].boneId = readLE<int32_t>(rBytes_, plugin.data + 4);
				uint32_t nodeCount = readLE<uint32_t>(rBytes_, plugin.data + 8);
				if(nodeCount > 0)
				{
					if(20 + static_cast<uint64_t>(nodeCount) * 12 > plugin.size)
					{
						throw std::runtime_error(std::format("Dff: HAnim hierarchy of {} nodes doesn't fit in its {} bytes.", nodeCount, plugin.size));
					}
					rHierarchy_.clear();
					for(uint32_t n = 0; n < nodeCount; n++)
					{
						rHierarchy_.push_back(readLE<int32_t>(rBytes_, plugin.data + 20 + n * 12));
					}
				}
			}
			pluginCursor = plugin.end;
		}
		cursor = extension.end;
	}
}

/*
A geometry's submeshes and, when it has a Skin plugin, its inverse bind matrices (one per skin bone; empty if unskinned).
*/
struct DffGeometry
{
	std::vector<Graphics::RenderWareSubmesh> submeshes;
	std::vector<glm::mat4> inverseBindMatrices;
};

/*
Skin plugin in the geometry's Extension (external, gtamods "Skin PLG"; confirmed on SHADOW_BODY.DFF: 30 bones, 26 used,
2 weights, 1382 vertices, and SHADOW_HAND_L.DFF): u8 bone count, u8 used bone count, u8 max weights per vertex, u8 pad,
u8 used bones[used count], u8[4] bone indices per vertex, f32[4] weights per vertex, then per bone (preceded by
u32 0xDEADDEAD when the used count is 0) a 4x4 inverse bind matrix of 16 floats. Trailing fields (12 bytes on Shadow's
files) are not read. The matrix rows (right, up, at, position) become columns, like the frame matrices (hypothesis, the
same one as there). Fills rVertices_' bone indices and weights and returns the matrices.
*/
static std::vector<glm::mat4> readSkin(const std::vector<uint8_t>& rBytes_, const Chunk& rSkin_, std::vector<Graphics::Vertex>& rVertices_)
{
	uint32_t vertexCount = static_cast<uint32_t>(rVertices_.size());
	uint8_t boneCount = readLE<uint8_t>(rBytes_, rSkin_.data);
	uint8_t usedCount = readLE<uint8_t>(rBytes_, rSkin_.data + 1);
	uint32_t indexOffset = rSkin_.data + 4 + usedCount;
	uint32_t weightOffset = indexOffset + vertexCount * 4;
	uint32_t matrixOffset = weightOffset + vertexCount * 16;
	uint32_t matrixStride = usedCount == 0 ? 68 : 64;
	if(static_cast<uint64_t>(matrixOffset) + boneCount * matrixStride > rSkin_.end)
	{
		throw std::runtime_error(std::format("Dff: skin of {} bones and {} vertices doesn't fit in its {} bytes.", boneCount, vertexCount, rSkin_.size));
	}

	for(uint32_t v = 0; v < vertexCount; v++)
	{
		for(uint32_t k = 0; k < 4; k++)
		{
			uint8_t bone = readLE<uint8_t>(rBytes_, indexOffset + v * 4 + k);
			float weight = readLE<float>(rBytes_, weightOffset + v * 16 + k * 4);
			if(weight != 0.0f && bone >= boneCount)
			{
				throw std::runtime_error(std::format("Dff: vertex {} uses skin bone {}, but the skin has {}.", v, bone, boneCount));
			}
			rVertices_[v].boneIndices[k] = bone;
			rVertices_[v].boneWeights[k] = weight;
		}
	}

	std::vector<glm::mat4> matrices;
	for(uint32_t b = 0; b < boneCount; b++)
	{
		uint32_t matrix = matrixOffset + b * matrixStride + (usedCount == 0 ? 4 : 0);
		float m[16];
		for(int k = 0; k < 16; k++)
		{
			m[k] = readLE<float>(rBytes_, matrix + k * 4);
		}
		matrices.push_back(glm::mat4(
			glm::vec4(m[0], m[1], m[2], 0.0f),
			glm::vec4(m[4], m[5], m[6], 0.0f),
			glm::vec4(m[8], m[9], m[10], 0.0f),
			glm::vec4(m[12], m[13], m[14], 1.0f)
		));
	}
	return matrices;
}

/*
Geometry Struct (RW 3.4+, no surface lighting floats): u32 format flags (bits 16-23: UV set count,
bit 24: native), u32 triangle count, u32 vertex count, u32 morph target count. Then, for non-native geometry:
prelit colors (vertex count x 4, if prelit), UV sets (count x vertex count x 8),
triangles (count x 8: u16 vertex2, u16 vertex1, u16 material id, u16 vertex3),
and per morph target: bounding sphere (16), u32 has positions, u32 has normals, positions (x 12), normals (x 12).
Only morph target 0 is used. Returns one submesh per used material id, each with the full vertex array, and the
skin's inverse bind matrices if the Extension has a Skin plugin (whose bone indices and weights go into the vertices).
The material list (a child chunk after the Struct) gives each submesh its textureName, and the vertex color of every vertex is its prelit color (white if the
geometry has none or it is almost black, see PrelitBlackLimit; bytes r, g, b, a) times the material's color, which the shader multiplies with the texture. Hypothesis (RenderWare's unlit result, not checked against the game):
observed that stage objects have prelit colors that tint gray textures (HINTRING00 (0, 127, 127, 255), HINTRING01 gray 178, FRUIT dark with alpha 127), and that without them they looked uncolored.
*/
static DffGeometry readGeometry(const std::vector<uint8_t>& rBytes_, const Chunk& rGeometry_)
{
	Chunk data = expectChunk(rBytes_, rGeometry_.data, ChunkStruct);
	uint32_t format = readLE<uint32_t>(rBytes_, data.data);
	uint32_t triangleCount = readLE<uint32_t>(rBytes_, data.data + 4);
	uint32_t vertexCount = readLE<uint32_t>(rBytes_, data.data + 8);
	uint32_t morphCount = readLE<uint32_t>(rBytes_, data.data + 12);

	if(format & GeometryNative)
	{
		throw std::runtime_error(std::format("Dff: geometry at 0x{:X} is platform-native, which is not supported.", rGeometry_.data));
	}
	if(morphCount == 0)
	{
		throw std::runtime_error(std::format("Dff: geometry at 0x{:X} has no morph target.", rGeometry_.data));
	}

	uint32_t uvSetCount = (format >> 16) & 0xFF;
	if(uvSetCount == 0)
	{
		uvSetCount = (format & GeometryTextured2) ? 2 : ((format & GeometryTextured) ? 1 : 0);
	}

	uint32_t cursor = data.data + 16;
	uint32_t prelitOffset = cursor;
	if(format & GeometryPrelit)
	{
		cursor += vertexCount * 4;
	}
	uint32_t uvOffset = cursor;
	cursor += uvSetCount * vertexCount * 8;
	uint32_t triangleOffset = cursor;
	cursor += triangleCount * 8;

	bool hasPositions = readLE<uint32_t>(rBytes_, cursor + 16) != 0;
	bool hasNormals = readLE<uint32_t>(rBytes_, cursor + 20) != 0;
	cursor += 24;
	if(!hasPositions)
	{
		throw std::runtime_error(std::format("Dff: geometry at 0x{:X} has no vertex positions.", rGeometry_.data));
	}
	uint32_t positionOffset = cursor;
	uint32_t normalOffset = cursor + vertexCount * 12;

	std::vector<Graphics::Vertex> vertices(vertexCount);
	for(uint32_t v = 0; v < vertexCount; v++)
	{
		Graphics::Vertex& rVertex = vertices[v];
		rVertex.position = glm::vec3(
			readLE<float>(rBytes_, positionOffset + v * 12),
			readLE<float>(rBytes_, positionOffset + v * 12 + 4),
			readLE<float>(rBytes_, positionOffset + v * 12 + 8)
		);
		rVertex.normal = hasNormals ? glm::vec3(
			readLE<float>(rBytes_, normalOffset + v * 12),
			readLE<float>(rBytes_, normalOffset + v * 12 + 4),
			readLE<float>(rBytes_, normalOffset + v * 12 + 8)
		) : glm::vec3(0.0f);
		rVertex.uv = uvSetCount > 0 ? glm::vec2(
			readLE<float>(rBytes_, uvOffset + v * 8),
			readLE<float>(rBytes_, uvOffset + v * 8 + 4)
		) : glm::vec2(0.0f);
		rVertex.color = glm::vec4(1.0f);
		if(format & GeometryPrelit)
		{
			uint8_t red = readLE<uint8_t>(rBytes_, prelitOffset + v * 4);
			uint8_t green = readLE<uint8_t>(rBytes_, prelitOffset + v * 4 + 1);
			uint8_t blue = readLE<uint8_t>(rBytes_, prelitOffset + v * 4 + 2);
			uint8_t alpha = readLE<uint8_t>(rBytes_, prelitOffset + v * 4 + 3);
			bool black = std::max({red, green, blue}) < PrelitBlackLimit;
			rVertex.color = glm::vec4(black ? 1.0f : red / 255.0f, black ? 1.0f : green / 255.0f, black ? 1.0f : blue / 255.0f, alpha / 255.0f);
		}
	}

	std::vector<RwMaterial> materials;
	DffGeometry geometry;
	uint32_t childCursor = data.end;
	while(childCursor < rGeometry_.end)
	{
		Chunk child = readChunk(rBytes_, childCursor);
		if(child.type == ChunkMaterialList)
		{
			materials = readMaterialList(rBytes_, child);
		}
		else if(child.type == ChunkExtension)
		{
			uint32_t pluginCursor = child.data;
			while(pluginCursor < child.end)
			{
				Chunk plugin = readChunk(rBytes_, pluginCursor);
				if(plugin.type == ChunkSkin)
				{
					geometry.inverseBindMatrices = readSkin(rBytes_, plugin, vertices);
				}
				pluginCursor = plugin.end;
			}
		}
		childCursor = child.end;
	}

	std::vector<Graphics::RenderWareSubmesh> submeshes;
	for(uint32_t t = 0; t < triangleCount; t++)
	{
		uint32_t triangle = triangleOffset + t * 8;
		uint16_t vertex2 = readLE<uint16_t>(rBytes_, triangle);
		uint16_t vertex1 = readLE<uint16_t>(rBytes_, triangle + 2);
		uint16_t material = readLE<uint16_t>(rBytes_, triangle + 4);
		uint16_t vertex3 = readLE<uint16_t>(rBytes_, triangle + 6);
		if(vertex1 >= vertexCount || vertex2 >= vertexCount || vertex3 >= vertexCount)
		{
			throw std::runtime_error(std::format("Dff: triangle {} references a vertex past {}.", t, vertexCount));
		}

		if(material >= materials.size())
		{
			throw std::runtime_error(std::format("Dff: triangle {} uses material {}, but the geometry has {}.", t, material, materials.size()));
		}
		if(material >= submeshes.size())
		{
			submeshes.resize(material + 1);
		}
		std::vector<uint32_t>& rIndices = submeshes[material].mesh.indicies;
		rIndices.push_back(vertex1);
		rIndices.push_back(FlipWinding ? vertex3 : vertex2);
		rIndices.push_back(FlipWinding ? vertex2 : vertex3);
	}

	for(size_t m = 0; m < submeshes.size(); m++)
	{
		Graphics::RenderWareSubmesh& rSubmesh = submeshes[m];
		if(rSubmesh.mesh.indicies.empty())
		{
			continue;
		}
		rSubmesh.mesh.verticies = vertices;
		for(Graphics::Vertex& rVertex : rSubmesh.mesh.verticies)
		{
			rVertex.color *= materials[m].color;
		}
		rSubmesh.textureName = materials[m].textureName;
		rSubmesh.uvAnimName = materials[m].uvAnimName;
		geometry.submeshes.push_back(rSubmesh);
	}
	return geometry;
}

/*
The frame of each skin bone: skin bone i is HAnim hierarchy node i, whose node id is the boneId of one frame.
Throws if the skin and the hierarchy have different counts or a node id has no frame.
*/
static std::vector<int32_t> skinBoneFrames(const Graphics::RenderWareMesh& rMesh_, const std::vector<int32_t>& rHierarchy_, size_t boneCount_)
{
	if(rHierarchy_.size() != boneCount_)
	{
		throw std::runtime_error(std::format("Dff: skin has {} bones, but the HAnim hierarchy has {} nodes.", boneCount_, rHierarchy_.size()));
	}
	std::vector<int32_t> frames;
	for(int32_t nodeId : rHierarchy_)
	{
		int32_t found = -1;
		for(size_t f = 0; f < rMesh_.frames.size(); f++)
		{
			if(rMesh_.frames[f].boneId == nodeId) found = static_cast<int32_t>(f);
		}
		if(found < 0)
		{
			throw std::runtime_error(std::format("Dff: HAnim node {} has no frame.", nodeId));
		}
		frames.push_back(found);
	}
	return frames;
}

/*
Clump: Struct (u32 atomic count, ...), FrameList, GeometryList (Struct with u32 count, then Geometry chunks),
then one Atomic per drawable (Struct: u32 frame index, u32 geometry index, u32 flags, u32 unused),
then extensions. Chunks of other types between them (lights, cameras) are skipped.
*/
Graphics::RenderWareMesh readDff(const std::vector<uint8_t>& rBytes_)
{
	Chunk clump = expectChunk(rBytes_, 0, ChunkClump);

	Graphics::RenderWareMesh mesh;
	std::vector<DffGeometry> geometries;
	std::vector<int32_t> hierarchy;

	uint32_t cursor = clump.data;
	while(cursor < clump.end)
	{
		Chunk chunk = readChunk(rBytes_, cursor);
		if(chunk.type == ChunkFrameList)
		{
			readFrameList(rBytes_, chunk, mesh, hierarchy);
		}
		else if(chunk.type == ChunkGeometryList)
		{
			Chunk listData = expectChunk(rBytes_, chunk.data, ChunkStruct);
			uint32_t count = readLE<uint32_t>(rBytes_, listData.data);
			uint32_t geometryCursor = listData.end;
			for(uint32_t g = 0; g < count; g++)
			{
				Chunk geometry = expectChunk(rBytes_, geometryCursor, ChunkGeometry);
				geometries.push_back(readGeometry(rBytes_, geometry));
				geometryCursor = geometry.end;
			}
		}
		else if(chunk.type == ChunkAtomic)
		{
			Chunk atomicData = expectChunk(rBytes_, chunk.data, ChunkStruct);
			uint32_t frame = readLE<uint32_t>(rBytes_, atomicData.data);
			uint32_t geometry = readLE<uint32_t>(rBytes_, atomicData.data + 4);
			if(frame >= mesh.frames.size() || geometry >= geometries.size())
			{
				throw std::runtime_error(std::format("Dff: atomic references frame {} / geometry {}, but the file has {} / {}.", frame, geometry, mesh.frames.size(), geometries.size()));
			}
			const DffGeometry& rGeometry = geometries[geometry];
			Graphics::RenderWareAtomic atomic{static_cast<int32_t>(frame), rGeometry.submeshes};
			if(!rGeometry.inverseBindMatrices.empty())
			{
				atomic.skinBoneFrames = skinBoneFrames(mesh, hierarchy, rGeometry.inverseBindMatrices.size());
				atomic.inverseBindMatrices = rGeometry.inverseBindMatrices;
			}
			mesh.atomics.push_back(std::move(atomic));
		}
		cursor = chunk.end;
	}

	return mesh;
}
}
