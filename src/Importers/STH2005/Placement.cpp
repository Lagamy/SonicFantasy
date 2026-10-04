#include "Placement.h"
#include "BinaryRead.h"
#include <cstring>

namespace STH2005
{
/*
Layout (little-endian; decoded from stg0100_cmn / _nrm / _hrd / _ds1.dat of the Shadow disc: in all four the sizes below add up to the file size
exactly and the records' parameter sizes add up to the parameter block's size):
0x00 "sky2", 0x04 u32 record count, 0x08 u32 size of the parameter block. From 0x0C: the records, 44 bytes each, then the parameter block
(the file ends with it).
Record: f32 position[3], f32 rotation[3] (degrees, by their values: 180.0 and 90.0 are common), u32 flags (high bit set, then a mask word), u32
flagsB, u16 type, u16 subType, u32 parameter size, u32 pointer (0 in every file; the game fills it in). A record's parameters are the next
`parameter size` bytes of the block, in record order.
Hypothesis (from the file names and the values): the 0x1000, 0x2000, 0x4000 and 0x8000 bits of the flags are the files cmn, nrm, hrd and ds1, a mission mask. The game
loads setid.bin and the files in FUN_800cb86c (external, the Ghidra decompile of main.dol).
*/
constexpr uint32_t HeaderSize = 12;
constexpr uint32_t RecordSize = 44;

std::vector<Placement> readPlacements(const std::vector<uint8_t>& rBytes_)
{
	if(rBytes_.size() < HeaderSize || std::memcmp(rBytes_.data(), "sky2", 4) != 0)
	{
		throw std::runtime_error("STH2005::readPlacements: not a placement file (no \"sky2\" magic).");
	}
	uint32_t count = readLE<uint32_t>(rBytes_, 4);
	uint32_t paramBytes = readLE<uint32_t>(rBytes_, 8);
	if(static_cast<uint64_t>(HeaderSize) + static_cast<uint64_t>(count) * RecordSize + paramBytes != rBytes_.size())
	{
		throw std::runtime_error(std::format("STH2005::readPlacements: {} records and {} parameter bytes do not add up to the file size {}.", count, paramBytes, rBytes_.size()));
	}

	std::vector<Placement> placements;
	placements.reserve(count);
	uint32_t paramCursor = HeaderSize + count * RecordSize;
	for(uint32_t i = 0; i < count; i++)
	{
		uint32_t record = HeaderSize + i * RecordSize;
		Placement placement;
		placement.position = glm::vec3(readLE<float>(rBytes_, record), readLE<float>(rBytes_, record + 4), readLE<float>(rBytes_, record + 8));
		placement.rotation = glm::vec3(readLE<float>(rBytes_, record + 12), readLE<float>(rBytes_, record + 16), readLE<float>(rBytes_, record + 20));
		placement.flags = readLE<uint32_t>(rBytes_, record + 24);
		placement.flagsB = readLE<uint32_t>(rBytes_, record + 28);
		placement.type = readLE<uint16_t>(rBytes_, record + 32);
		placement.subType = readLE<uint16_t>(rBytes_, record + 34);
		uint32_t paramSize = readLE<uint32_t>(rBytes_, record + 36);
		if(static_cast<uint64_t>(paramCursor) + paramSize > rBytes_.size())
		{
			throw std::runtime_error(std::format("STH2005::readPlacements: record {} has parameters past the end of the file.", i));
		}
		placement.params.assign(rBytes_.begin() + paramCursor, rBytes_.begin() + paramCursor + paramSize);
		paramCursor += paramSize;
		placements.push_back(std::move(placement));
	}
	if(paramCursor != rBytes_.size())
	{
		throw std::runtime_error("STH2005::readPlacements: the records' parameters do not fill the parameter block.");
	}
	return placements;
}
}
