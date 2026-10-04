#include "Txd.h"
#include "RwChunk.h"
#include "BinaryRead.h"
#include "GXColor.h"
#include "GXTexture.h"
#include "GPU_API.h"
#include <array>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

namespace Renderware
{
constexpr uint32_t ChunkTextureNative = 0x15;
constexpr uint32_t ChunkTextureDictionary = 0x16;

constexpr uint32_t PlatformGameCube = 6;

/* GX palette format numbers (external, libogc gx.h: GX_TL_RGB565, GX_TL_RGB5A3). */
constexpr uint8_t PaletteRGB565 = 1;
constexpr uint8_t PaletteRGB5A3 = 2;

/* RenderWare filter and addressing ids (external, gtamods). */
constexpr uint32_t FilterNearest = 1;
constexpr uint32_t FilterLinear = 2;
constexpr uint32_t AddressWrap = 1;
constexpr uint32_t AddressMirror = 2;
constexpr uint32_t AddressClamp = 3;

/*
Texture Native Struct for platform 6, big-endian inside (little-endian chunk headers around it). Decoded from the seven
textures of SHADOW.TXD and the 3000+ textures of the stage dictionaries (stgNNNN_texCM.one, every stage); no outside source
for this layout. Offsets from the start of the Struct data:
0x00 u32 platform (6), 0x04 u32 filter and addressing (filter in the low byte, U addressing in bits 8-11,
V addressing in bits 12-15; external, the same packing as the PC format), 0x08-0x17 four u32 (unknown: 0, 1, 1, 0 in all),
0x18 name (32 bytes), 0x38 mask name (32 bytes, empty), 0x58 u32 raster format (unused here: 0x4204, 0x4304 for the
texture with alpha), 0x5C u16 width, u16 height, 0x60 u8 (unknown: 4 in all), u8 mip level count (hypothesis: all
have 1 and a data size of exactly one level), u8 GX texture format, u8 palette format, 0x64 u32 has alpha (unused:
alpha comes from the palette format or the format itself), then the palette if the format has one (C4: 16 x u16 at 0x68,
C8: 256 x u16 at 0x68), then u32 pixel data size and the pixel data. So the data size is at 0x88 (C4), 0x268 (C8) or 0x68
(formats without a palette: RGB565, RGB5A3, CMPR). Formats seen: C4 with an RGB565 or RGB5A3 palette, C8 with the same,
RGB565, RGB5A3, CMPR. The width and height can be any value: the data is padded to whole tiles (seen 34x34, 4x4, 2x2).
The decoded image has its first row at the top, like RenderWare's UVs expect, so it is uploaded as is.
*/
static std::pair<std::string, PoolId> readTextureNative(const std::vector<uint8_t>& rBytes_, const Chunk& rNative_)
{
	Chunk data = expectChunk(rBytes_, rNative_.data, ChunkStruct);
	uint32_t base = data.data;
	if(data.size < 0x6C)
	{
		throw std::runtime_error(std::format("Txd: texture at 0x{:X} has only {} bytes, less than its header.", rNative_.data, data.size));
	}
	if(readBE<uint32_t>(rBytes_, base) != PlatformGameCube)
	{
		throw std::runtime_error(std::format("Txd: texture at 0x{:X} is for platform {}, only {} (GameCube) is supported.", rNative_.data, readBE<uint32_t>(rBytes_, base), PlatformGameCube));
	}

	const char* pName = reinterpret_cast<const char*>(rBytes_.data() + base + 0x18);
	std::string name(pName, strnlen(pName, 32));

	uint32_t width = readBE<uint16_t>(rBytes_, base + 0x5C);
	uint32_t height = readBE<uint16_t>(rBytes_, base + 0x5E);
	uint8_t mipCount = readLE<uint8_t>(rBytes_, base + 0x61);
	uint8_t format = readLE<uint8_t>(rBytes_, base + 0x62);
	uint8_t paletteFormat = readLE<uint8_t>(rBytes_, base + 0x63);

	uint32_t paletteSize = 0;
	if(format == GameCube::GXFormatC4) paletteSize = 16;
	if(format == GameCube::GXFormatC8) paletteSize = 256;
	uint32_t expectedSize = GameCube::gxDataSize(format, width, height);
	if(expectedSize == 0 || (paletteSize != 0 && paletteFormat != PaletteRGB565 && paletteFormat != PaletteRGB5A3))
	{
		throw std::runtime_error(std::format("Txd: texture {} has GX format {} with palette format {}, only C4 and C8 with an RGB565 or RGB5A3 palette, RGB565, RGB5A3 and CMPR are supported.", name, format, paletteFormat));
	}
	uint32_t sizeOffset = 0x68 + paletteSize * 2;
	uint32_t dataSize = readBE<uint32_t>(rBytes_, base + sizeOffset);
	if(mipCount != 1 || width == 0 || height == 0 || dataSize != expectedSize || data.size != sizeOffset + 4 + dataSize)
	{
		throw std::runtime_error(std::format("Txd: texture {} ({}x{}, format {}, {} mip levels, {} data bytes in a {} byte struct) is not a single level texture of the size its format needs ({} bytes).", name, width, height, format, mipCount, dataSize, data.size, expectedSize));
	}

	uint32_t filterAddressing = readBE<uint32_t>(rBytes_, base + 0x04);
	uint32_t filter = filterAddressing & 0xFF;
	uint32_t addressU = (filterAddressing >> 8) & 0xF;
	uint32_t addressV = (filterAddressing >> 12) & 0xF;
	if(addressU != addressV)
	{
		throw std::runtime_error(std::format("Txd: texture {} has different U ({}) and V ({}) addressing, which Texture can't represent.", name, addressU, addressV));
	}

	PoolId textureId = Graphics::addTexture(name.c_str(), width, height, 4);
	Graphics::Texture& rTexture = Graphics::getTexture(textureId);

	switch(filter)
	{
	case FilterNearest: rTexture.filter = GL_NEAREST; break;
	case FilterLinear: rTexture.filter = GL_LINEAR; break;
	default: throw std::runtime_error(std::format("Txd: texture {} has unsupported filter mode {}.", name, filter));
	}
	switch(addressU)
	{
	case AddressWrap: rTexture.wrap = GL_REPEAT; break;
	case AddressMirror: rTexture.wrap = GL_MIRRORED_REPEAT; break;
	case AddressClamp: rTexture.wrap = GL_CLAMP_TO_EDGE; break;
	default: throw std::runtime_error(std::format("Txd: texture {} has unsupported addressing mode {}.", name, addressU));
	}

	std::vector<std::array<uint8_t, 4>> palette(paletteSize);
	for(uint32_t i = 0; i < paletteSize; i++)
	{
		uint16_t value = readBE<uint16_t>(rBytes_, base + 0x68 + i * 2);
		palette[i] = paletteFormat == PaletteRGB565 ? GameCube::colorRGB565(value) : GameCube::colorRGB5A3(value);
	}

	uint32_t pixels = base + sizeOffset + 4;
	switch(format)
	{
	case GameCube::GXFormatC4: GameCube::decodeC4(rBytes_, pixels, width, height, palette, rTexture.pixels); break;
	case GameCube::GXFormatC8: GameCube::decodeC8(rBytes_, pixels, width, height, palette, rTexture.pixels); break;
	case GameCube::GXFormatRGB565: GameCube::decodeRGB565(rBytes_, pixels, width, height, rTexture.pixels); break;
	case GameCube::GXFormatRGB5A3: GameCube::decodeRGB5A3(rBytes_, pixels, width, height, rTexture.pixels); break;
	case GameCube::GXFormatCMPR: GameCube::decodeCMPR(rBytes_, pixels, width, height, rTexture.pixels); break;
	}

	rTexture.create();
	return {name, textureId};
}

/*
Texture dictionary: a Struct (u16 texture count, u16 device id), then one Texture Native chunk per texture,
then an Extension (ignored). External, gtamods; confirmed on SHADOW.TXD.
*/
TxdTextures importTxd(const std::vector<uint8_t>& rBytes_)
{
	Chunk dictionary = expectChunk(rBytes_, 0, ChunkTextureDictionary);
	Chunk header = expectChunk(rBytes_, dictionary.data, ChunkStruct);
	uint16_t count = readLE<uint16_t>(rBytes_, header.data);

	TxdTextures textures;
	uint32_t cursor = header.end;
	for(uint16_t t = 0; t < count; t++)
	{
		Chunk native = expectChunk(rBytes_, cursor, ChunkTextureNative);
		std::pair<std::string, PoolId> texture = readTextureNative(rBytes_, native);
		textures.ids.push_back(texture.second);
		textures.byName.insert(texture);
		cursor = native.end;
	}
	return textures;
}
}
