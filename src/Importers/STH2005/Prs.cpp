#include "Prs.h"
#include <format>
#include <stdexcept>

namespace STH2005
{
struct PrsReader
{
	const std::vector<uint8_t>& rSrc;
	size_t cursor = 0;
	uint8_t control = 0;
	int bitsLeft = 0;

	uint8_t byte()
	{
		if(this->cursor >= this->rSrc.size())
		{
			throw std::runtime_error("PRS: stream ends before its end marker.");
		}
		return this->rSrc[this->cursor++];
	}

	uint8_t bit()
	{
		if(this->bitsLeft == 0)
		{
			this->control = this->byte();
			this->bitsLeft = 8;
		}
		uint8_t value = this->control & 1;
		this->control >>= 1;
		this->bitsLeft--;
		return value;
	}
};

/*
PRS (external, Sega LZ77; confirmed by decompressing every file in Shadow's character/shadow.one to its listed size).
Control bits come LSB first from a control byte, refilled every 8 bits, interleaved with data bytes:
  1          literal: copy the next byte.
  0 0 b1 b0  short copy: size = (b1 * 2 + b0) + 2, offset = next byte - 256.
  0 1        long copy: u16 LE v. v == 0 ends the stream. offset = (v >> 3) - 8192;
             size = v & 7, and if 0 then size = next byte + 1, else size + 2.
Copies read from already-written output at a negative offset and may overlap what they write.
*/
std::vector<uint8_t> decompressPrs(const std::vector<uint8_t>& rSrc_)
{
	PrsReader reader{rSrc_};
	std::vector<uint8_t> out;

	while(true)
	{
		if(reader.bit())
		{
			out.push_back(reader.byte());
			continue;
		}

		int64_t offset;
		uint32_t size;
		if(reader.bit())
		{
			uint16_t value = reader.byte();
			value |= static_cast<uint16_t>(reader.byte()) << 8;
			if(value == 0)
			{
				break;
			}
			offset = static_cast<int64_t>(value >> 3) - 8192;
			size = value & 7;
			size = (size == 0) ? reader.byte() + 1 : size + 2;
		}
		else
		{
			size = reader.bit() << 1;
			size |= reader.bit();
			size += 2;
			offset = static_cast<int64_t>(reader.byte()) - 256;
		}

		if(static_cast<int64_t>(out.size()) + offset < 0)
		{
			throw std::runtime_error(std::format("PRS: copy reaches {} bytes before the start of the output.", -(static_cast<int64_t>(out.size()) + offset)));
		}
		for(uint32_t i = 0; i < size; i++)
		{
			out.push_back(out[out.size() + offset]);
		}
	}

	return out;
}
}
