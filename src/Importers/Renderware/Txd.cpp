#include "Txd.h"
#include "RwChunk.h"
#include "BinaryRead.h"
#include "GXColor.h"
#include "GPU_API.h"
#include <array>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

namespace Importers
{
constexpr uint32_t ChunkTextureNative = 0x15;
constexpr uint32_t ChunkTextureDictionary = 0x16;

constexpr uint32_t PlatformGameCube = 6;

/* GX texture format and palette format numbers (external, libogc gx.h: GX_TF_C4, GX_TL_RGB565, GX_TL_RGB5A3). */
constexpr uint8_t GXFormatC4 = 8;
constexpr uint8_t PaletteRGB565 = 1;
constexpr uint8_t PaletteRGB5A3 = 2;

/* RenderWare filter and addressing ids (external, gtamods). */
constexpr uint32_t FilterNearest = 1;
constexpr uint32_t FilterLinear = 2;
constexpr uint32_t AddressWrap = 1;
constexpr uint32_t AddressMirror = 2;
constexpr uint32_t AddressClamp = 3;

constexpr uint32_t TextureHeaderSize = 0x8C;

/*
Texture Native Struct for platform 6, big-endian inside (little-endian chunk headers around it). Decoded from the seven
textures of SHADOW.TXD; no outside source for this layout. Offsets from the start of the Struct data:
0x00 u32 platform (6), 0x04 u32 filter and addressing (filter in the low byte, U addressing in bits 8-11,
V addressing in bits 12-15; external, the same packing as the PC format), 0x08-0x17 four u32 (unknown: 0, 1, 1, 0 in all),
0x18 name (32 bytes), 0x38 mask name (32 bytes, empty), 0x58 u32 raster format (unused here: 0x4204, 0x4304 for the
texture with alpha), 0x5C u16 width, u16 height, 0x60 u8 (unknown: 4 in all), u8 mip level count (hypothesis: all
seven have 1 and a data size of exactly one level), u8 GX texture format, u8 palette format, 0x64 u32 has alpha (unused:
alpha comes from the palette format), 0x68 palette (16 x u16 for C4), 0x88 u32 pixel data size, 0x8C pixel data.
C4 pixel data is stored in 8x8 pixel tiles, left to right then top to bottom, each tile row by row with 4 bytes per row,
the left pixel in the high nibble. The decoded image has its first row at the top, like RenderWare's UVs expect, so
it is uploaded as is.
*/
static std::pair<std::string, PoolId> readTextureNative(const std::vector<uint8_t>& rBytes_, const Chunk& rNative_)
{
	Chunk data = expectChunk(rBytes_, rNative_.data, ChunkStruct);
	uint32_t base = data.data;
	if(data.size < TextureHeaderSize)
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
	uint32_t dataSize = readBE<uint32_t>(rBytes_, base + 0x88);

	if(format != GXFormatC4 || (paletteFormat != PaletteRGB565 && paletteFormat != PaletteRGB5A3))
	{
		throw std::runtime_error(std::format("Txd: texture {} has GX format {} with palette format {}, only C4 with an RGB565 or RGB5A3 palette is supported.", name, format, paletteFormat));
	}
	if(mipCount != 1 || width % 8 != 0 || height % 8 != 0 || dataSize != width * height / 2 || data.size != TextureHeaderSize + dataSize)
	{
		throw std::runtime_error(std::format("Txd: texture {} ({}x{}, {} mip levels, {} data bytes in a {} byte struct) is not a single level C4 texture of whole 8x8 tiles.", name, width, height, mipCount, dataSize, data.size));
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

	std::array<std::array<uint8_t, 4>, 16> palette;
	for(uint32_t i = 0; i < 16; i++)
	{
		uint16_t value = readBE<uint16_t>(rBytes_, base + 0x68 + i * 2);
		palette[i] = paletteFormat == PaletteRGB565 ? colorRGB565(value) : colorRGB5A3(value);
	}

	uint32_t cursor = base + TextureHeaderSize;
	for(uint32_t tileY = 0; tileY < height; tileY += 8)
	{
		for(uint32_t tileX = 0; tileX < width; tileX += 8)
		{
			for(uint32_t y = 0; y < 8; y++)
			{
				for(uint32_t x = 0; x < 8; x += 2)
				{
					uint8_t pair = readLE<uint8_t>(rBytes_, cursor++);
					for(uint32_t k = 0; k < 2; k++)
					{
						const std::array<uint8_t, 4>& rColor = palette[k == 0 ? pair >> 4 : pair & 0xF];
						std::memcpy(rTexture.pixels.data() + ((tileY + y) * width + tileX + x + k) * 4, rColor.data(), 4);
					}
				}
			}
		}
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
