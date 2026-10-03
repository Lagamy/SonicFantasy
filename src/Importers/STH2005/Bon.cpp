#include "Bon.h"
#include "BinaryRead.h"
#include <cstring>
#include <format>
#include <stdexcept>

namespace STH2005
{
/*
.BON layout, big-endian. Decoded from SH.BON, LTE.BON and RTE.BON in character/shadow.one; no outside source.
Header (0x30): u32 0x04010000 (unknown), u32 0, u16 0x00FF (unknown), u16 node count,
f32 (unknown: 5.15 in SH.BON, sh_root's height; 0.852 in the hand files), name (32 bytes).
Then one 0x70-byte record per node, depth first:
  u32 id (equals the HAnim node id of the matching DFF frame), u32 flags (unknown), u32 (unknown), u32 0,
  translation (f32 x 3 + pad), rotation in radians (f32 x 3 + pad; axis order unknown), scale (f32 x 3 + pad),
  u32 0, u32 0, u32 first child offset, u32 next sibling offset (absolute byte offsets, 0 = none), name (32 bytes).
*/
constexpr uint32_t BonHeaderSize = 0x30;
constexpr uint32_t BonNodeSize = 0x70;

static std::string readName(const std::vector<uint8_t>& rBytes_, uint32_t offset_)
{
	const char* pName = reinterpret_cast<const char*>(rBytes_.data() + offset_);
	return std::string(pName, strnlen(pName, 32));
}

static glm::vec3 readVec3BE(const std::vector<uint8_t>& rBytes_, uint32_t offset_)
{
	return glm::vec3(readBE<float>(rBytes_, offset_), readBE<float>(rBytes_, offset_ + 4), readBE<float>(rBytes_, offset_ + 8));
}

static int32_t offsetToIndex(uint32_t offset_, uint32_t count_)
{
	if(offset_ < BonHeaderSize || (offset_ - BonHeaderSize) % BonNodeSize != 0 || (offset_ - BonHeaderSize) / BonNodeSize >= count_)
	{
		throw std::runtime_error(std::format("Bon: link 0x{:X} does not point at a node record.", offset_));
	}
	return static_cast<int32_t>((offset_ - BonHeaderSize) / BonNodeSize);
}

/*
Parents come from the links: a node's first child gets it as parent, and a node's next sibling shares its parent.
Records are depth first, so every link points forward and one pass over the records is enough.
*/
Bon readBon(const std::vector<uint8_t>& rBytes_)
{
	uint16_t count = readBE<uint16_t>(rBytes_, 0x0A);
	if(rBytes_.size() < BonHeaderSize + static_cast<size_t>(count) * BonNodeSize)
	{
		throw std::runtime_error(std::format("Bon: file of {} bytes is too short for {} nodes.", rBytes_.size(), count));
	}

	Bon bon;
	bon.name = readName(rBytes_, 0x10);
	bon.nodes.resize(count);
	for(BonNode& rNode : bon.nodes)
	{
		rNode.parent = -2;
	}
	if(count > 0)
	{
		bon.nodes[0].parent = -1;
	}

	for(uint32_t i = 0; i < count; i++)
	{
		uint32_t record = BonHeaderSize + i * BonNodeSize;
		BonNode& rNode = bon.nodes[i];
		if(rNode.parent == -2)
		{
			throw std::runtime_error(std::format("Bon: node {} is not linked from an earlier node.", i));
		}
		rNode.id = readBE<uint32_t>(rBytes_, record);
		rNode.translation = readVec3BE(rBytes_, record + 0x10);
		rNode.rotation = readVec3BE(rBytes_, record + 0x20);
		rNode.scale = readVec3BE(rBytes_, record + 0x30);
		rNode.name = readName(rBytes_, record + 0x50);

		uint32_t firstChild = readBE<uint32_t>(rBytes_, record + 0x48);
		uint32_t nextSibling = readBE<uint32_t>(rBytes_, record + 0x4C);
		if(firstChild != 0)
		{
			bon.nodes[offsetToIndex(firstChild, count)].parent = static_cast<int32_t>(i);
		}
		if(nextSibling != 0)
		{
			bon.nodes[offsetToIndex(nextSibling, count)].parent = rNode.parent;
		}
	}

	return bon;
}
}
