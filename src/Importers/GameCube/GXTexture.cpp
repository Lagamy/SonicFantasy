#include "GXTexture.h"
#include "BinaryRead.h"
#include "GXColor.h"
#include <cstring>

namespace GameCube
{
static uint32_t roundUp(uint32_t value_, uint32_t multiple_)
{
	return (value_ + multiple_ - 1) / multiple_ * multiple_;
}

uint32_t gxDataSize(uint8_t format_, uint32_t width_, uint32_t height_)
{
	switch(format_)
	{
	case GXFormatRGB565:
	case GXFormatRGB5A3: return roundUp(width_, 4) * roundUp(height_, 4) * 2;
	case GXFormatC4:
	case GXFormatCMPR: return roundUp(width_, 8) * roundUp(height_, 8) / 2;
	case GXFormatC8: return roundUp(width_, 8) * roundUp(height_, 4);
	default: return 0;
	}
}

/* Writes a pixel if it is inside the image: the last tiles of an image that is not a multiple of the tile size are padding. */
static void putPixel(std::vector<unsigned char>& rPixels_, uint32_t width_, uint32_t height_, uint32_t x_, uint32_t y_, const std::array<uint8_t, 4>& rColor_)
{
	if(x_ >= width_ || y_ >= height_) return;
	std::memcpy(rPixels_.data() + (y_ * width_ + x_) * 4, rColor_.data(), 4);
}

/*
CMPR is 8x8 pixel tiles (left to right, then top to bottom), each made of four 4x4 blocks in the order top left, top right,
bottom left, bottom right. A block is two big-endian RGB565 colors, then 4 bytes, one per row from the top, with 2 bit
palette indices and the left pixel in the high bits (external, YAGCD / Dolphin). Like DXT1: if the first color is the
larger value the other two palette entries are 1/3 and 2/3 blends, otherwise the average and transparent black.
*/
void decodeCMPR(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, std::vector<unsigned char>& rPixels_)
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
				palette[0] = colorRGB565(value0);
				palette[1] = colorRGB565(value1);
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
						putPixel(rPixels_, width_, height_, blockX + x, blockY + y, palette[(row >> (6 - 2 * x)) & 3]);
					}
				}
				cursor += 8;
			}
		}
	}
}

/* RGB5A3 and RGB565 are 4x4 pixel tiles (left to right, then top to bottom), each row by row with a big-endian u16 per pixel. */
void decodeRGB5A3(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, std::vector<unsigned char>& rPixels_)
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
					putPixel(rPixels_, width_, height_, tileX + x, tileY + y, colorRGB5A3(readBE<uint16_t>(rBytes_, cursor)));
					cursor += 2;
				}
			}
		}
	}
}

void decodeRGB565(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, std::vector<unsigned char>& rPixels_)
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
					putPixel(rPixels_, width_, height_, tileX + x, tileY + y, colorRGB565(readBE<uint16_t>(rBytes_, cursor)));
					cursor += 2;
				}
			}
		}
	}
}

/* C4: 8x8 pixel tiles, each row 4 bytes of two palette indices (left pixel in the high nibble). */
void decodeC4(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, const std::vector<std::array<uint8_t, 4>>& rPalette_, std::vector<unsigned char>& rPixels_)
{
	uint32_t cursor = offset_;
	for(uint32_t tileY = 0; tileY < height_; tileY += 8)
	{
		for(uint32_t tileX = 0; tileX < width_; tileX += 8)
		{
			for(uint32_t y = 0; y < 8; y++)
			{
				for(uint32_t x = 0; x < 8; x += 2)
				{
					uint8_t pair = readLE<uint8_t>(rBytes_, cursor++);
					putPixel(rPixels_, width_, height_, tileX + x, tileY + y, rPalette_[pair >> 4]);
					putPixel(rPixels_, width_, height_, tileX + x + 1, tileY + y, rPalette_[pair & 0xF]);
				}
			}
		}
	}
}

/* C8: 8x4 pixel tiles (8 wide, 4 tall), one palette index per pixel, row by row. */
void decodeC8(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, const std::vector<std::array<uint8_t, 4>>& rPalette_, std::vector<unsigned char>& rPixels_)
{
	uint32_t cursor = offset_;
	for(uint32_t tileY = 0; tileY < height_; tileY += 4)
	{
		for(uint32_t tileX = 0; tileX < width_; tileX += 8)
		{
			for(uint32_t y = 0; y < 4; y++)
			{
				for(uint32_t x = 0; x < 8; x++)
				{
					putPixel(rPixels_, width_, height_, tileX + x, tileY + y, rPalette_[readLE<uint8_t>(rBytes_, cursor++)]);
				}
			}
		}
	}
}
}
