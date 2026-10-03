#include "SAMotion.h"
#include "BinaryRead.h"
#include <bit>
#include <format>
#include <stdexcept>

namespace Ninja
{
/* NJS_MOTION type flags (external, SA Tools); channels are stored in the order of their bits. */
constexpr uint16_t MotionPosition = 0x1;
constexpr uint16_t MotionRotation = 0x2;
constexpr uint16_t MotionScale = 0x4;
constexpr uint16_t MotionInterpolationMask = 0xC0;

template <typename T>
static T read(const std::vector<uint8_t>& rBytes_, uint32_t offset_, bool bigEndian_)
{
	return bigEndian_ ? readBE<T>(rBytes_, offset_) : readLE<T>(rBytes_, offset_);
}

/*
A channel's keys: 16 bytes each, u32 frame then 3 floats, or 3 x i32 BAMS for rotation.
*/
static std::vector<Graphics::SAKeyframe> readKeys(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t count_, bool rotation_, bool bigEndian_)
{
	std::vector<Graphics::SAKeyframe> keys;
	for(uint32_t k = 0; k < count_; k++)
	{
		uint32_t key = offset_ + k * 16;
		glm::vec3 value;
		for(int c = 0; c < 3; c++)
		{
			value[c] = rotation_ ? static_cast<float>(read<int32_t>(rBytes_, key + 4 + c * 4, bigEndian_)) : read<float>(rBytes_, key + 4 + c * 4, bigEndian_);
		}
		keys.push_back(Graphics::SAKeyframe{read<uint32_t>(rBytes_, key, bigEndian_), value});
	}
	return keys;
}

/*
NJS_MOTION (external, SA Tools; confirmed on "Sonic Idle", ModelsMiles.rel @ 0x21B654: 73 nodes, 39 frames, type 0x3,
inp_fn 0x2): u32 mdata pointer, u32 frame count, u16 type flags, u16 inp_fn (low 4 bits: channel count; 0x40 spline,
0x80 user interpolation). mdata is one entry per node of the model: n channel pointers then n u32 key counts
(n = channel count); a null pointer or a 0 count means the node has no keys on that channel.
*/
Graphics::SAMotion readSAMotion(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t nodeCount_, bool bigEndian_)
{
	uint32_t mdata = read<uint32_t>(rBytes_, offset_, bigEndian_);
	uint32_t frameCount = read<uint32_t>(rBytes_, offset_ + 4, bigEndian_);
	uint16_t type = read<uint16_t>(rBytes_, offset_ + 8, bigEndian_);
	uint16_t interpolation = read<uint16_t>(rBytes_, offset_ + 10, bigEndian_);
	uint32_t channelCount = interpolation & 0xF;

	if(type & ~(MotionPosition | MotionRotation | MotionScale))
	{
		throw std::runtime_error(std::format("SAMotion: motion at 0x{:X} has type 0x{:X}, only position, rotation and scale are supported.", offset_, type));
	}
	if(interpolation & MotionInterpolationMask)
	{
		throw std::runtime_error(std::format("SAMotion: motion at 0x{:X} uses interpolation 0x{:X}, only linear is supported.", offset_, interpolation & MotionInterpolationMask));
	}
	if(static_cast<uint32_t>(std::popcount(type)) != channelCount)
	{
		throw std::runtime_error(std::format("SAMotion: motion at 0x{:X} has type 0x{:X} but {} channels.", offset_, type, channelCount));
	}

	Graphics::SAMotion motion;
	motion.frameCount = frameCount;
	motion.nodes.resize(nodeCount_);
	for(uint32_t n = 0; n < nodeCount_; n++)
	{
		uint32_t entry = mdata + n * channelCount * 8;
		uint32_t channel = 0;
		for(uint16_t bit : {MotionPosition, MotionRotation, MotionScale})
		{
			if(!(type & bit)) continue;
			uint32_t keys = read<uint32_t>(rBytes_, entry + channel * 4, bigEndian_);
			uint32_t count = read<uint32_t>(rBytes_, entry + (channelCount + channel) * 4, bigEndian_);
			channel++;
			if(keys == 0 || count == 0) continue;

			Graphics::SAMotionNode& rNode = motion.nodes[n];
			std::vector<Graphics::SAKeyframe>& rChannel = bit == MotionPosition ? rNode.positions : (bit == MotionRotation ? rNode.rotations : rNode.scales);
			rChannel = readKeys(rBytes_, keys, count, bit == MotionRotation, bigEndian_);
		}
	}
	return motion;
}
}
