#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace GameCube
{
/* GX texture format numbers (external, libogc gx.h: GX_TF_RGB565, GX_TF_RGB5A3, GX_TF_C4, GX_TF_C8, GX_TF_CMPR). */
constexpr uint8_t GXFormatRGB565 = 4;
constexpr uint8_t GXFormatRGB5A3 = 5;
constexpr uint8_t GXFormatC4 = 8;
constexpr uint8_t GXFormatC8 = 9;
constexpr uint8_t GXFormatCMPR = 14;

/*
Size in bytes of one level of a texture in a GX format, whose width and height are rounded up to whole tiles (4x4 for
RGB565 / RGB5A3, 8x4 for C8, 8x8 for C4 and CMPR). 0 for a format that is not supported.
*/
uint32_t gxDataSize(uint8_t format_, uint32_t width_, uint32_t height_);

/*
Decoders of one level of a GX texture to RGBA8, from rBytes_ at offset_. The data is stored in tiles; the image is
width_ x height_ (any size, the padding of the last tiles is skipped) and rPixels_ receives width_ * height_ * 4 bytes, first row at the top.
Tile layouts are external (YAGCD / Dolphin): RGB565 and RGB5A3 4x4 tiles of big-endian u16, C4 8x8 tiles of 4 bytes per
row (left pixel in the high nibble), C8 8x4 tiles of one byte per pixel, CMPR 8x8 tiles of four 4x4 blocks, each block two
RGB565 colors and 2 bit indices (like DXT1).
Hypothesis for C8 only: nothing checked it by looking at an image; its data sizes match on every C8 texture of Shadow's .txd files.
palette_ is the color table of a C4 (16 entries) or C8 (256 entries) texture.
*/
void decodeCMPR(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, std::vector<unsigned char>& rPixels_);
void decodeRGB5A3(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, std::vector<unsigned char>& rPixels_);
void decodeRGB565(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, std::vector<unsigned char>& rPixels_);
void decodeC4(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, const std::vector<std::array<uint8_t, 4>>& rPalette_, std::vector<unsigned char>& rPixels_);
void decodeC8(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t width_, uint32_t height_, const std::vector<std::array<uint8_t, 4>>& rPalette_, std::vector<unsigned char>& rPixels_);
}
