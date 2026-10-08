#include "Uva.h"
#include "RwChunk.h"
#include "BinaryRead.h"
#include <cstring>
#include <format>
#include <stdexcept>

namespace Renderware
{
constexpr uint32_t ChunkUvAnimDictionary = 0x2B;
constexpr uint32_t ChunkAnimAnimation = 0x1B;

constexpr uint32_t AnimVersion = 0x100;
constexpr uint32_t AnimTypeUv = 0x1C1;
constexpr uint32_t AnimHeaderSize = 20;
constexpr uint32_t CustomDataSize = 64;
constexpr uint32_t KeySize = 32;

/*
UV animation dictionary (.uva), little-endian. Chunk 0x2B: Struct (u32 animation count), then one chunk 0x1B per animation.
Layout decoded from the 27 .UVA files of common/CommonEffect.one (disc, checked: the chunk size is always 20 + 64 + 32 x keys + 4):
Animation data: u32 version 0x100, u32 type 0x1C1 (UV animation, external RtAnim ids), u32 key count, u32 flags (0 in all), f32 duration (= the last
key's time); 64 bytes of custom data: u32 0, name (32 bytes, "RenderWareShader7"; the material names it), 28 bytes of zeros (all files; probably the
UV channel map, unused here); then the keys, 32 bytes each: u32 (0 or a stale pointer, unused), f32 time, f32 theta, f32 scale x, f32 scale y,
f32 skew, f32 offset u, f32 offset v; then a u32 (0, 1 or key count - 2, unused).
The order theta, scale, skew, offset is the RenderWare RpUVAnim key layout as remembered (hypothesis); the files fit it: only the last two floats ever scroll
(EFFCHRBALLA: offset v 0, 1.5, 3.0 at 0, 0.333, 0.667 s; EFFBOMB: u 1, v -5), and scale starts at (1, 1).
How a key becomes a texture transform, and whether playback loops, is not read here.
*/
std::vector<UvAnimation> readUvaDictionary(const std::vector<uint8_t>& rBytes_)
{
	Chunk dictionary = expectChunk(rBytes_, 0, ChunkUvAnimDictionary);
	Chunk header = expectChunk(rBytes_, dictionary.data, ChunkStruct);
	uint32_t count = readLE<uint32_t>(rBytes_, header.data);

	std::vector<UvAnimation> animations;
	uint32_t cursor = header.end;
	for(uint32_t a = 0; a < count; a++)
	{
		Chunk chunk = expectChunk(rBytes_, cursor, ChunkAnimAnimation);
		uint32_t keyCount = readLE<uint32_t>(rBytes_, chunk.data + 8);
		if(readLE<uint32_t>(rBytes_, chunk.data) != AnimVersion || readLE<uint32_t>(rBytes_, chunk.data + 4) != AnimTypeUv
			|| chunk.size != AnimHeaderSize + CustomDataSize + KeySize * keyCount + 4)
		{
			throw std::runtime_error(std::format("Uva: animation {} at 0x{:X} is not a UV animation of the known layout.", a, cursor));
		}

		UvAnimation animation;
		const char* pName = reinterpret_cast<const char*>(rBytes_.data() + chunk.data + AnimHeaderSize + 4);
		animation.name = std::string(pName, strnlen(pName, 32));
		animation.duration = readLE<float>(rBytes_, chunk.data + 16);
		uint32_t key = chunk.data + AnimHeaderSize + CustomDataSize;
		for(uint32_t k = 0; k < keyCount; k++)
		{
			uint32_t at = key + k * KeySize + 4;
			animation.keys.push_back(UvAnimKey{
				readLE<float>(rBytes_, at),
				readLE<float>(rBytes_, at + 4),
				glm::vec2(readLE<float>(rBytes_, at + 8), readLE<float>(rBytes_, at + 12)),
				readLE<float>(rBytes_, at + 16),
				glm::vec2(readLE<float>(rBytes_, at + 20), readLE<float>(rBytes_, at + 24))
			});
		}
		animations.push_back(std::move(animation));
		cursor = chunk.end;
	}
	return animations;
}
}
