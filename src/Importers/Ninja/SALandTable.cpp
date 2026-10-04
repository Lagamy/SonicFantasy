#include "SALandTable.h"
#include "BinaryRead.h"
#include <cstring>
#include <format>
#include <stdexcept>

namespace Ninja
{
constexpr uint32_t LandTableCOLSize = 0x24;
constexpr uint32_t TexlistEntrySize = 12;

static std::string readCString(const std::vector<uint8_t>& rBytes_, uint32_t offset_)
{
	if(offset_ >= rBytes_.size())
	{
		throw std::runtime_error(std::format("SALandTable: string at 0x{:X} is past the end of the file.", offset_));
	}
	const char* pText = reinterpret_cast<const char*>(rBytes_.data() + offset_);
	return std::string(pText, strnlen(pText, rBytes_.size() - offset_));
}

/*
Layout decoded from the SADX GameCube disc (_Stg01.rel, the three Emerald Coast land tables, and every land table listed in
SA Tools' GC_SADX INIs parsed the same way), big-endian. Land table (0x24): u16 COL count, u16 animation count, u32 flags,
f32 (3000.0 on most tables, possibly a draw distance, hypothesis), u32 COL list offset (0x0C), u32 animation data (0x10),
u32 texture file name (0x14, null on the tables checked), u32 texlist (0x18), two unused words.
COL (0x24), checked by the list ending exactly where its land table begins: center (3 x f32), f32 radius, two words that
are 0, u32 root NJS_OBJECT offset (0x18), one word that is 0, u32 surface flags (0x20).
Texlist: u32 offset of an array of 12 byte entries (u32 offset of the name, two words that are 0), u32 entry count.
Animated parts (animation data) are not read.
*/
SALandTable readSALandTable(const std::vector<uint8_t>& rBytes_, uint32_t offset_)
{
	SALandTable landTable;

	uint16_t colCount = readBE<uint16_t>(rBytes_, offset_);
	uint32_t colList = readBE<uint32_t>(rBytes_, offset_ + 0x0C);
	uint32_t texlist = readBE<uint32_t>(rBytes_, offset_ + 0x18);

	for(uint16_t i = 0; i < colCount; i++)
	{
		uint32_t col = colList + i * LandTableCOLSize;
		uint32_t modelOffset = readBE<uint32_t>(rBytes_, col + 0x18);
		if(modelOffset == 0)
		{
			throw std::runtime_error(std::format("SALandTable: COL {} of the table at 0x{:X} has no model.", i, offset_));
		}
		glm::vec3 center(readBE<float>(rBytes_, col), readBE<float>(rBytes_, col + 4), readBE<float>(rBytes_, col + 8));
		landTable.cols.push_back(SALandTableCol{modelOffset, readBE<uint32_t>(rBytes_, col + 0x20), center, readBE<float>(rBytes_, col + 0x0C)});
	}

	if(texlist != 0)
	{
		uint32_t entries = readBE<uint32_t>(rBytes_, texlist);
		uint32_t count = readBE<uint32_t>(rBytes_, texlist + 4);
		for(uint32_t i = 0; i < count; i++)
		{
			landTable.textureNames.push_back(readCString(rBytes_, readBE<uint32_t>(rBytes_, entries + i * TexlistEntrySize)));
		}
	}

	return landTable;
}
}
