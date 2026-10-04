#include "Rel.h"
#include "BinaryRead.h"
#include "SaComp.h"
#include <bit>
#include <cstring>
#include <format>
#include <stdexcept>

namespace GameCube
{
/*
REL layout (external, YAGCD / Dolphin's REL loader; confirmed on ModelsMiles.rel from the SADX GC disc), big-endian.
Header: +0x00 u32 module id, +0x0C u32 section count, +0x10 u32 section table offset,
+0x28 u32 import table offset, +0x2C u32 import table size.
Section entry (8 bytes): u32 file offset (bit 0 = executable, 0 = no data in file), u32 size.
Import entry (8 bytes): u32 module id the relocations point into, u32 offset of its relocation list.
Relocation (8 bytes): u16 offset from the previous relocation, u8 type, u8 target section, u32 addend.
*/
constexpr uint8_t RelocAddr32 = 1;
constexpr uint8_t RelocDolphinSection = 202;
constexpr uint8_t RelocDolphinEnd = 203;

static void writeBE32(std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t value_)
{
	if(static_cast<uint64_t>(offset_) + 4 > rBytes_.size())
	{
		throw std::runtime_error(std::format("Rel: relocation at 0x{:X} is past the end of the file.", offset_));
	}
	uint32_t swapped = std::byteswap(value_);
	std::memcpy(rBytes_.data() + offset_, &swapped, 4);
}

/*
Only R_PPC_ADDR32 relocations into the module itself are applied: those are the data pointers.
A SaCompGC compressed module is decompressed first (see SaComp.cpp).
Others (code relocations, pointers into main.dol, module 0) are skipped and keep their file value.
SECTION selects the section being patched and resets the running offset; NOP only advances it; END stops.
On ModelsMiles.rel every relocation is a self-module ADDR32 (8178 of them).
*/
std::vector<uint8_t> relocateRel(std::vector<uint8_t> bytes_)
{
	if(isSaComp(bytes_))
	{
		bytes_ = decompressSaComp(bytes_);
	}

	uint32_t moduleId = readBE<uint32_t>(bytes_, 0x00);
	uint32_t sectionCount = readBE<uint32_t>(bytes_, 0x0C);
	uint32_t sectionTable = readBE<uint32_t>(bytes_, 0x10);
	uint32_t importTable = readBE<uint32_t>(bytes_, 0x28);
	uint32_t importSize = readBE<uint32_t>(bytes_, 0x2C);

	std::vector<uint32_t> sectionOffsets(sectionCount);
	for(uint32_t s = 0; s < sectionCount; s++)
	{
		sectionOffsets[s] = readBE<uint32_t>(bytes_, sectionTable + s * 8) & ~1u;
	}

	for(uint32_t i = 0; i < importSize / 8; i++)
	{
		uint32_t targetModule = readBE<uint32_t>(bytes_, importTable + i * 8);
		uint32_t cursor = readBE<uint32_t>(bytes_, importTable + i * 8 + 4);
		uint32_t patchSection = 0;
		uint32_t patchOffset = 0;

		while(true)
		{
			uint16_t delta = readBE<uint16_t>(bytes_, cursor);
			uint8_t type = readBE<uint8_t>(bytes_, cursor + 2);
			uint8_t section = readBE<uint8_t>(bytes_, cursor + 3);
			uint32_t addend = readBE<uint32_t>(bytes_, cursor + 4);
			cursor += 8;

			if(type == RelocDolphinEnd)
			{
				break;
			}
			patchOffset += delta;
			if(type == RelocDolphinSection)
			{
				patchSection = section;
				patchOffset = 0;
				continue;
			}
			if(type != RelocAddr32 || targetModule != moduleId)
			{
				continue;
			}
			if(patchSection >= sectionCount || section >= sectionCount)
			{
				throw std::runtime_error(std::format("Rel: relocation references section {} / {}, but the module has {}.", patchSection, section, sectionCount));
			}
			writeBE32(bytes_, sectionOffsets[patchSection] + patchOffset, sectionOffsets[section] + addend);
		}
	}

	return bytes_;
}
}
