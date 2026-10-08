#include "RwMaterial.h"
#include "RwChunk.h"
#include <cstring>
#include <format>
#include <stdexcept>

namespace Renderware
{
constexpr uint32_t ChunkUvAnimPlugin = 0x135;

/*
Material (external, gtamods; fields confirmed on Shadow's DFFs and on the worlds of its stages, .RG1): Struct (u32 flags, u32 color stored as bytes R, G, B, A,
u32 unused, u32 textured, f32 ambient, specular, diffuse), then, if textured, a Texture chunk: Struct (u32 filter and
addressing), String texture name, String mask name, Extension.
Only the color, the base texture name and the UV animation name are read. The material Extension is read for the UV animation
plugin 0x135 (decoded from the 28 materials of the effect models of common/CommonEffect.one, all alike: chunk Struct of 4 + 32 bytes = u32 slot mask (1 in
all), then the 32 byte name of each set slot, e.g. "RenderWareShader7", the name of a .UVA animation). Only that shape is read; an extension or plugin of another shape
leaves the name empty, because this runs for every DFF and world material and only the effect models are known. The MatFX plugin
0x120 (effect type 5 = UV transform in those materials, external RenderWare ids) is not read: a name implies the UV transform.
*/
static RwMaterial readMaterial(const std::vector<uint8_t>& rBytes_, const Chunk& rMaterial_)
{
	Chunk data = expectChunk(rBytes_, rMaterial_.data, ChunkStruct);
	uint32_t color = readLE<uint32_t>(rBytes_, data.data + 4);
	bool textured = readLE<uint32_t>(rBytes_, data.data + 12) != 0;

	uint32_t cursor = data.end;
	RwMaterial material;
	material.color = glm::vec4(
		(color & 0xFF) / 255.0f,
		((color >> 8) & 0xFF) / 255.0f,
		((color >> 16) & 0xFF) / 255.0f,
		(color >> 24) / 255.0f
	);

	if(textured)
	{
		Chunk texture = expectChunk(rBytes_, cursor, ChunkTexture);
		Chunk filter = expectChunk(rBytes_, texture.data, ChunkStruct);
		Chunk name = expectChunk(rBytes_, filter.end, ChunkString);
		const char* pName = reinterpret_cast<const char*>(rBytes_.data() + name.data);
		material.textureName = std::string(pName, strnlen(pName, name.size));
		cursor = texture.end;
	}

	if(cursor + 12 <= rMaterial_.end && readLE<uint32_t>(rBytes_, cursor) == ChunkExtension)
	{
		Chunk extension = readChunk(rBytes_, cursor);
		uint32_t pluginCursor = extension.data;
		while(pluginCursor + 12 <= extension.end)
		{
			Chunk plugin = readChunk(rBytes_, pluginCursor);
			if(plugin.type == ChunkUvAnimPlugin && plugin.size == 12 + 4 + 32 && readLE<uint32_t>(rBytes_, plugin.data) == ChunkStruct)
			{
				Chunk names = readChunk(rBytes_, plugin.data);
				if(names.size == 4 + 32 && readLE<uint32_t>(rBytes_, names.data) == 1)
				{
					const char* pAnim = reinterpret_cast<const char*>(rBytes_.data() + names.data + 4);
					material.uvAnimName = std::string(pAnim, strnlen(pAnim, 32));
				}
			}
			pluginCursor = plugin.end;
		}
	}
	return material;
}

/*
MaterialList: Struct (u32 count, then count x i32), then one Material chunk for every entry of -1. An entry >= 0 is the
index of an earlier material that this one reuses (external, gtamods; every entry is -1 in Shadow's DFFs).
*/
std::vector<RwMaterial> readMaterialList(const std::vector<uint8_t>& rBytes_, const Chunk& rList_)
{
	Chunk data = expectChunk(rBytes_, rList_.data, ChunkStruct);
	uint32_t count = readLE<uint32_t>(rBytes_, data.data);

	std::vector<RwMaterial> materials;
	uint32_t cursor = data.end;
	for(uint32_t m = 0; m < count; m++)
	{
		int32_t index = readLE<int32_t>(rBytes_, data.data + 4 + m * 4);
		if(index < 0)
		{
			Chunk material = expectChunk(rBytes_, cursor, ChunkMaterial);
			materials.push_back(readMaterial(rBytes_, material));
			cursor = material.end;
		}
		else if(static_cast<uint32_t>(index) < m)
		{
			materials.push_back(materials[index]);
		}
		else
		{
			throw std::runtime_error(std::format("RwMaterial: material {} reuses material {}, not stored before it.", m, index));
		}
	}
	return materials;
}
}
