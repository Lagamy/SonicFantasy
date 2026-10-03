#pragma once
#include <array>
#include <cstdint>

namespace GameCube
{
/*
GameCube 16 bit color formats to RGBA8, each component scaled to the full 0-255 range.
RGB565 has no alpha. RGB5A3 (external, YAGCD): top bit set is opaque RGB555, clear is RGB444 with a 3 bit alpha.
*/
inline uint8_t expandBits(uint32_t value_, uint32_t max_)
{
	return static_cast<uint8_t>(value_ * 255 / max_);
}

inline std::array<uint8_t, 4> colorRGB565(uint16_t value_)
{
	return {expandBits(value_ >> 11, 31), expandBits((value_ >> 5) & 0x3F, 63), expandBits(value_ & 0x1F, 31), 255};
}

inline std::array<uint8_t, 4> colorRGB5A3(uint16_t value_)
{
	if(value_ & 0x8000)
	{
		return {expandBits((value_ >> 10) & 0x1F, 31), expandBits((value_ >> 5) & 0x1F, 31), expandBits(value_ & 0x1F, 31), 255};
	}
	return {expandBits((value_ >> 8) & 0xF, 15), expandBits((value_ >> 4) & 0xF, 15), expandBits(value_ & 0xF, 15), expandBits((value_ >> 12) & 0x7, 7)};
}
}
