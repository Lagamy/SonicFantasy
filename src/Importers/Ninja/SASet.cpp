#include "SASet.h"
#include "BinaryRead.h"

namespace Ninja
{
/*
SET file layout (GameCube SADX, big-endian; decoded from Set0100S.bin: the size is exactly 0x20 + 32 x count on it, 788 records, and the positions are in the
land table's range; the structure is external, SA Tools' SETItem): 0x00 u32 record count, the rest of the 0x20 byte header zeros. Then 32 byte records:
u16 type, s16 rotation x, y, z (BAMS), f32 position x, y, z, f32 scale x, y, z.
Only the low 12 bits of the u16 are the type (the object list has under a hundred entries); the top 4 are kept as flags: Set0100S has 54 records with 0x8000 on types that are
otherwise ordinary (observed: 0x8000 | 32 is a YASI0, a palm tree, like the records of type 32 without it); what the bit means is unknown.
*/
constexpr uint32_t HeaderSize = 0x20;
constexpr uint32_t RecordSize = 32;

std::vector<SAPlacement> readSAPlacements(const std::vector<uint8_t>& rBytes_)
{
	uint32_t count = readBE<uint32_t>(rBytes_, 0);
	if(static_cast<uint64_t>(HeaderSize) + static_cast<uint64_t>(count) * RecordSize != rBytes_.size())
	{
		throw std::runtime_error(std::format("Ninja::readSAPlacements: {} records do not add up to the file size {}.", count, rBytes_.size()));
	}

	std::vector<SAPlacement> placements;
	placements.reserve(count);
	for(uint32_t i = 0; i < count; i++)
	{
		uint32_t record = HeaderSize + i * RecordSize;
		SAPlacement placement;
		uint16_t typeAndFlags = readBE<uint16_t>(rBytes_, record);
		placement.type = typeAndFlags & 0x0FFF;
		placement.flags = typeAndFlags >> 12;
		placement.rotation = glm::ivec3(readBE<int16_t>(rBytes_, record + 2), readBE<int16_t>(rBytes_, record + 4), readBE<int16_t>(rBytes_, record + 6));
		placement.position = glm::vec3(readBE<float>(rBytes_, record + 8), readBE<float>(rBytes_, record + 12), readBE<float>(rBytes_, record + 16));
		placement.scale = glm::vec3(readBE<float>(rBytes_, record + 20), readBE<float>(rBytes_, record + 24), readBE<float>(rBytes_, record + 28));
		placements.push_back(placement);
	}
	return placements;
}
}
