#include "Gvm.h"
#include "BinaryRead.h"
#include "GXColor.h"
#include "GPU_API.h"
#include <array>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>

namespace Ninja
{
/* GVR data format numbers (external, Puyo Tools; GX_TF_RGB5A3 and GX_TF_CMPR of the GameCube). */
constexpr uint8_t GVRFormatRGB5A3 = 0x05;
constexpr uint8_t GVRFormatCMPR = 0x0E;

constexpr uint32_t GVMEntrySize = 38;

/*
CMPR is 8x8 pixel tiles (left to right, then top to bottom), each made of four 4x4 blocks in the order top left, top right,
bottom left, bottom right. A block is two big-endian RGB565 colors, then 4 bytes, one per row from the top, with 2 bit
palette indices and the left pixel in the high bits (external, YAGCD / Dolphin). Like DXT1: if the first color is the
larger value the other two palette entries are 1/3 and 2/3 blends, otherwise the average and transparent black.
*/
static void decodeCMPR(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, std::vector<unsigned char>& rPixels_)
{
	uint32_t cursor = offset_;
	for(uint32_t tileY = 0; tileY < height_; tileY += 8)
	{
		for(uint32_t tileX = 0; tileX < width_; tileX += 8)
		{
			for(uint32_t block = 0; block < 4; block++)
			{
				uint32_t blockX = tileX + (block & 1) * 4;
				uint32_t blockY = tileY + (block >> 1) * 4;

				uint16_t value0 = readBE<uint16_t>(rBytes_, cursor);
				uint16_t value1 = readBE<uint16_t>(rBytes_, cursor + 2);
				std::array<std::array<uint8_t, 4>, 4> palette;
				palette[0] = GameCube::colorRGB565(value0);
				palette[1] = GameCube::colorRGB565(value1);
				for(uint32_t c = 0; c < 3; c++)
				{
					if(value0 > value1)
					{
						palette[2][c] = (2 * palette[0][c] + palette[1][c]) / 3;
						palette[3][c] = (palette[0][c] + 2 * palette[1][c]) / 3;
					}
					else
					{
						palette[2][c] = (palette[0][c] + palette[1][c]) / 2;
						palette[3][c] = 0;
					}
				}
				palette[2][3] = 255;
				palette[3][3] = value0 > value1 ? 255 : 0;

				for(uint32_t y = 0; y < 4; y++)
				{
					uint8_t row = readLE<uint8_t>(rBytes_, cursor + 4 + y);
					for(uint32_t x = 0; x < 4; x++)
					{
						const std::array<uint8_t, 4>& rColor = palette[(row >> (6 - 2 * x)) & 3];
						std::memcpy(rPixels_.data() + ((blockY + y) * width_ + blockX + x) * 4, rColor.data(), 4);
					}
				}
				cursor += 8;
			}
		}
	}
}

/* RGB5A3 is 4x4 pixel tiles (left to right, then top to bottom), each row by row with a big-endian u16 per pixel. */
static void decodeRGB5A3(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, std::vector<unsigned char>& rPixels_)
{
	uint32_t cursor = offset_;
	for(uint32_t tileY = 0; tileY < height_; tileY += 4)
	{
		for(uint32_t tileX = 0; tileX < width_; tileX += 4)
		{
			for(uint32_t y = 0; y < 4; y++)
			{
				for(uint32_t x = 0; x < 4; x++)
				{
					std::array<uint8_t, 4> color = GameCube::colorRGB5A3(readBE<uint16_t>(rBytes_, cursor));
					std::memcpy(rPixels_.data() + ((tileY + y) * width_ + tileX + x) * 4, color.data(), 4);
					cursor += 2;
				}
			}
		}
	}
}

/*
GVR texture, starting at its "GVRT" magic: u32 little-endian size of what follows, then big-endian u16 (unknown, 0),
u8 palette format in the high nibble and data flags in the low one (hypothesis: bit 0 is "has mipmaps", set on all
16 textures of sonic.gvm, whose sizes match a full mip chain), u8 data format, u16 width, u16 height, pixel data.
Returns the offset just past the texture. The decoded image has its first row at the top, so it is uploaded as is.
*/
static uint32_t readGVR(const std::vector<uint8_t>& rBytes_, uint32_t offset_, const std::string& rName_, PoolId& rTextureId_)
{
	if(offset_ + 4 <= rBytes_.size() && std::memcmp(rBytes_.data() + offset_, "GBIX", 4) == 0)
	{
		offset_ += 8 + readLE<uint32_t>(rBytes_, offset_ + 4);
	}
	if(offset_ + 4 > rBytes_.size() || std::memcmp(rBytes_.data() + offset_, "GVRT", 4) != 0)
	{
		throw std::runtime_error(std::format("Gvm: texture {} at 0x{:X} doesn't start with GVRT.", rName_, offset_));
	}

	uint32_t end = offset_ + 8 + readLE<uint32_t>(rBytes_, offset_ + 4);
	uint8_t format = readLE<uint8_t>(rBytes_, offset_ + 11);
	uint32_t width = readBE<uint16_t>(rBytes_, offset_ + 12);
	uint32_t height = readBE<uint16_t>(rBytes_, offset_ + 14);
	uint32_t pixels = offset_ + 16;

	uint32_t tile = format == GVRFormatCMPR ? 8 : 4;
	if((format != GVRFormatCMPR && format != GVRFormatRGB5A3) || width % tile != 0 || height % tile != 0)
	{
		throw std::runtime_error(std::format("Gvm: texture {} is {}x{} with data format 0x{:X}, only CMPR and RGB5A3 with whole tiles are supported.", rName_, width, height, format));
	}
	uint32_t levelSize = format == GVRFormatCMPR ? width * height / 2 : width * height * 2;
	if(end > rBytes_.size() || pixels + levelSize > end)
	{
		throw std::runtime_error(std::format("Gvm: texture {} has {} bytes of pixels, less than its {} byte first level.", rName_, end - pixels, levelSize));
	}

	rTextureId_ = Graphics::addTexture(rName_.c_str(), width, height, 4);
	Graphics::Texture& rTexture = Graphics::getTexture(rTextureId_);
	rTexture.filter = GL_LINEAR;
	if(format == GVRFormatCMPR)
	{
		decodeCMPR(rBytes_, pixels, width, height, rTexture.pixels);
	}
	else
	{
		decodeRGB5A3(rBytes_, pixels, width, height, rTexture.pixels);
	}
	rTexture.create();

	return end;
}

/*
GVM archive, decoded from sonic.gvm; no outside source for the header. "GVMH", u32 little-endian offset of the first
texture minus 8, then big-endian u16 flags (0xF: every optional entry field present), u16 texture count, and one 38 byte
entry per texture: u16 index, name (28 bytes), u16 format, u16 dimensions, u32 global index. The textures follow
back to back as GVR blocks. Other flag values change the entry size and are not supported.
*/
void importGvm(const char* setName_, const std::vector<uint8_t>& rBytes_)
{
	if(rBytes_.size() < 12 || std::memcmp(rBytes_.data(), "GVMH", 4) != 0)
	{
		throw std::runtime_error("Gvm: not a GVMH archive.");
	}
	uint32_t cursor = 8 + readLE<uint32_t>(rBytes_, 4);
	uint16_t flags = readBE<uint16_t>(rBytes_, 8);
	uint16_t count = readBE<uint16_t>(rBytes_, 10);
	if(flags != 0xF)
	{
		throw std::runtime_error(std::format("Gvm: archive has flags 0x{:X}, only 0xF is supported.", flags));
	}

	PoolId setId = Graphics::addTextureSet(setName_);
	for(uint16_t i = 0; i < count; i++)
	{
		uint32_t entry = 12 + i * GVMEntrySize;
		if(entry + GVMEntrySize > rBytes_.size())
		{
			throw std::runtime_error(std::format("Gvm: entry {} is past the end of the file.", i));
		}
		const char* pName = reinterpret_cast<const char*>(rBytes_.data() + entry + 2);
		std::string name(pName, strnlen(pName, 28));
		PoolId textureId;
		cursor = readGVR(rBytes_, cursor, name, textureId);

		Graphics::TextureSet& rSet = Graphics::getTextureSet(setId);
		rSet.ordered.push_back(textureId);
	}
}
}
