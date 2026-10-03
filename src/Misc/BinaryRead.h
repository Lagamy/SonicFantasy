#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#include <format>
#include <stdexcept>
#include <type_traits>
#include <vector>

/*
Bounds-checked reads of plain values from a byte buffer.
readLE: value stored little-endian (PC, .ONE, RenderWare streams). readBE: big-endian (GameCube disc structures).
Assumes the host is little-endian, which holds for the Linux x86-64 target.
*/
template <typename T>
inline T readLE(const std::vector<uint8_t>& rBytes_, uint64_t offset_)
{
	if(offset_ + sizeof(T) > rBytes_.size())
	{
		throw std::runtime_error(std::format("BinaryRead: read of {} bytes at 0x{:X} is past the end of a {} byte buffer.", sizeof(T), offset_, rBytes_.size()));
	}
	T value;
	std::memcpy(&value, rBytes_.data() + offset_, sizeof(T));
	return value;
}

template <typename T>
inline T readBE(const std::vector<uint8_t>& rBytes_, uint64_t offset_)
{
	if constexpr (std::is_same_v<T, float>)
	{
		return std::bit_cast<float>(readBE<uint32_t>(rBytes_, offset_));
	}
	else
	{
		T value = readLE<T>(rBytes_, offset_);
		if constexpr (sizeof(T) > 1)
		{
			value = std::byteswap(value);
		}
		return value;
	}
}
