#include "SaComp.h"
#include "BinaryRead.h"
#include <cstring>
#include <format>
#include <stdexcept>

namespace GameCube
{
/*
SaCompGC layout of the SADX GameCube .rel files (_Stg*, _Adv*, _B_chaos6/7, _Minicart, _Shooting).
Source: external, the X-Hax/SACompGC decompressor, which is a cleaned-up decompilation of the game's own routine
(so it is the original algorithm, read here and rewritten, not independently derived). Checked by decompressing
_Stg01, _Stg02, _Adv00 and _Shooting: the output starts with a valid .rel header (observed).
A preamble of unknown use (0x20 bytes on the files checked) comes first, then at the 4-byte aligned tag "SaCompGC":
+0x08 u32 big-endian, low 28 bits = decompressed size, +0x0C u8 bits of a copy distance, +0x0D u8 bits of a copy
length, +0x10 the bit stream. On the files checked the widths are 11 and 4 for all of them.
The stream is big-endian u32 words consumed from the least significant bit up. A multi-bit value that straddles two
words has the bits of the first word as its high part. Each item starts with one bit: 1 = a literal byte (8 bits),
0 = a copy of (length bits + 2) bytes from (distance bits + 1) bytes back in the output, byte by byte (may overlap).
*/
static constexpr char SaCompTag[] = "SaCompGC";
static constexpr uint32_t SaCompTagSize = 8;
static constexpr uint32_t SaCompHeaderSize = 0x10;

struct BitReader
{
	const std::vector<uint8_t>* pBytes;
	uint64_t position;
	uint32_t current = 0;
	uint32_t available = 0;

	uint32_t read(uint32_t bitCount_)
	{
		if(bitCount_ == 0) return 0;
		if(bitCount_ > 32)
		{
			throw std::runtime_error("SaComp: more than 32 bits requested at once.");
		}
		if(this->available >= bitCount_)
		{
			uint32_t value = static_cast<uint32_t>(static_cast<uint64_t>(this->current) & ((1ull << bitCount_) - 1));
			this->current = bitCount_ == 32 ? 0 : this->current >> bitCount_;
			this->available -= bitCount_;
			return value;
		}
		uint32_t missing = bitCount_ - this->available;
		uint64_t value = static_cast<uint64_t>(this->current) << missing;
		uint32_t word = readBE<uint32_t>(*this->pBytes, this->position);
		this->position += 4;
		value |= static_cast<uint64_t>(word) & ((1ull << missing) - 1);
		this->available = 32 - missing;
		this->current = missing == 32 ? 0 : word >> missing;
		return static_cast<uint32_t>(value);
	}
};

/*
The tag is looked for in the first 0x100 bytes only (it is at 0x20 on the files checked), so plain modules are not scanned whole.
Returns -1 if there is none.
*/
static int64_t findTag(const std::vector<uint8_t>& rBytes_)
{
	for(uint64_t offset = 0; offset + SaCompTagSize <= rBytes_.size() && offset < 0x100; offset += 4)
	{
		if(std::memcmp(rBytes_.data() + offset, SaCompTag, SaCompTagSize) == 0) return static_cast<int64_t>(offset);
	}
	return -1;
}

bool isSaComp(const std::vector<uint8_t>& rBytes_)
{
	return findTag(rBytes_) >= 0;
}

std::vector<uint8_t> decompressSaComp(const std::vector<uint8_t>& rBytes_)
{
	int64_t found = findTag(rBytes_);
	if(found < 0)
	{
		throw std::runtime_error("SaComp: no SaCompGC tag found.");
	}
	uint64_t tag = static_cast<uint64_t>(found);
	uint32_t size = readBE<uint32_t>(rBytes_, tag + 8) & 0x0FFFFFFF;
	uint32_t distanceBits = readBE<uint8_t>(rBytes_, tag + 12);
	uint32_t lengthBits = readBE<uint8_t>(rBytes_, tag + 13);
	if(size == 0 || distanceBits > 32 || lengthBits > 32)
	{
		throw std::runtime_error(std::format("SaComp: bad header (size {}, distance bits {}, length bits {}).", size, distanceBits, lengthBits));
	}

	BitReader reader{&rBytes_, tag + SaCompHeaderSize};
	std::vector<uint8_t> out;
	out.reserve(size);
	while(out.size() < size)
	{
		if(reader.read(1))
		{
			out.push_back(static_cast<uint8_t>(reader.read(8)));
			continue;
		}
		uint64_t distance = static_cast<uint64_t>(reader.read(distanceBits)) + 1;
		uint64_t length = static_cast<uint64_t>(reader.read(lengthBits)) + 2;
		if(distance > out.size() || out.size() + length > size)
		{
			throw std::runtime_error(std::format("SaComp: copy of {} bytes from {} back at output 0x{:X} is outside the output (size 0x{:X}).", length, distance, out.size(), size));
		}
		for(uint64_t i = 0; i < length; i++)
		{
			out.push_back(out[out.size() - distance]);
		}
	}
	return out;
}
}
