#pragma once
#include "BinaryRead.h"
#include <cstdint>
#include <format>
#include <stdexcept>
#include <vector>

namespace Renderware
{
constexpr uint32_t ChunkStruct = 0x01;
constexpr uint32_t ChunkExtension = 0x03;
constexpr uint32_t ChunkString = 0x02;
constexpr uint32_t ChunkTexture = 0x06;
constexpr uint32_t ChunkMaterial = 0x07;
constexpr uint32_t ChunkMaterialList = 0x08;

/*
Header of a RenderWare stream chunk (external, gtamods wiki): u32 type, u32 size of the data, u32 library version stamp.
data is the offset of the chunk's data, end the offset just past it.
The header is little-endian even in a GameCube .txd, whose texture data inside is big-endian.
*/
struct Chunk
{
	uint32_t type;
	uint32_t size;
	uint32_t data;
	uint32_t end;
};

inline Chunk readChunk(const std::vector<uint8_t>& rBytes_, uint32_t offset_)
{
	Chunk chunk;
	chunk.type = readLE<uint32_t>(rBytes_, offset_);
	chunk.size = readLE<uint32_t>(rBytes_, offset_ + 4);
	chunk.data = offset_ + 12;
	chunk.end = chunk.data + chunk.size;
	if(chunk.end > rBytes_.size() || chunk.end < chunk.data)
	{
		throw std::runtime_error(std::format("RenderWare: chunk 0x{:X} at 0x{:X} claims {} bytes, past the end of the file.", chunk.type, offset_, chunk.size));
	}
	return chunk;
}

inline Chunk expectChunk(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t type_)
{
	Chunk chunk = readChunk(rBytes_, offset_);
	if(chunk.type != type_)
	{
		throw std::runtime_error(std::format("RenderWare: expected chunk 0x{:X} at 0x{:X}, found 0x{:X}.", type_, offset_, chunk.type));
	}
	return chunk;
}
}
