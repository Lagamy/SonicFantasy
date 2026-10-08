#include "Gvm.h"
#include "BinaryRead.h"
#include "GXTexture.h"
#include "GPU_API.h"
#include <array>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>

namespace Ninja
{
/* GVR data format numbers (external, Puyo Tools; the GX_TF_RGB5A3 and GX_TF_CMPR of the GameCube, GXTexture.h). */
constexpr uint8_t GVRFormatRGB5A3 = GameCube::GXFormatRGB5A3;
constexpr uint8_t GVRFormatCMPR = GameCube::GXFormatCMPR;

constexpr uint32_t GVMEntrySize = 38;

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
		GameCube::decodeCMPR(rBytes_, pixels, width, height, rTexture.pixels);
	}
	else
	{
		GameCube::decodeRGB5A3(rBytes_, pixels, width, height, rTexture.pixels);
	}
	rTexture.create();

	return end;
}

PoolId importGvr(const char* name_, const std::vector<uint8_t>& rBytes_)
{
	PoolId textureId;
	readGVR(rBytes_, 0, name_, textureId);
	return textureId;
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
		rSet.names.push_back(name);
	}
}
}
