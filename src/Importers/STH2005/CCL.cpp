#include "CCL.h"
#include "BinaryRead.h"
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace STH2005
{
/*
.CCL layout (big-endian; the structure is external: the game's own loader in the Ghidra decompile of the retail main.dol, FUN_8040e61c and its
relocation FUN_8040e818, and tools/dumpCCL.py parses all 1289 entries of the Shadow disc with the same rules):
0x00 u32 file size, 0x04 u32 A (size of section 1), 0x08 u32 B (size of section 2), 0x0C u32 1 (entries of section 3), 0x10 u32 0 (entries of
section 4), 0x14 u32 0x3142 (the game reads byte 0x17 == 'B' as big-endian), 0x18 and 0x1C u32 0. From 0x20: section 1 (A bytes), section 2
(B bytes), section 3 (8 bytes, {0, 0}: a group at the start of section 1 with the name at the start of the names), then the names.
Section 1: N records of 16 bytes {u32 type, u32 pointer, u32 flags, u32 id}, a 16 byte struct of zeros, then the data blocks in record order up
to A. A pointer is an offset from the start of section 1. Section 2 is the stream the game relocates those pointers with (one byte per record,
'A' for the first and 'D' for each next one: 1 word, then 4 words); N is its count of bytes, up to the zero padding.
*/
constexpr uint32_t SectionsOffset = 0x20;
constexpr uint32_t RecordSize = 16;

/* Size in floats of a record's block, by its type. The type is the game's Range kind: its constructors of Sphere, Box, Capsule and Cylinder pass 0, 1, 2 and 3 and copy 4, 10, 9 and 9 floats; a Plane is a Box subclass with kind 4 (external, from the decompile). */
static uint32_t blockFloats(uint32_t type_)
{
	switch(type_)
	{
	case 0: return 4;
	case 1: return 10;
	case 2: return 9;
	case 3: return 9;
	case 4: return 10;
	}
	throw std::runtime_error(std::format("STH2005::readCCL: unknown record type {}.", type_));
}

/* Radians to BAMS (65536 = a full turn, as Physics::CollisionShape stores angles). */
static int32_t radiansToBams(float radians_)
{
	return static_cast<int32_t>(std::lround(radians_ * (65536.0f / glm::two_pi<float>())));
}

std::vector<Physics::CollisionShape> readCCL(const std::vector<uint8_t>& rBytes_)
{
	if(readBE<uint32_t>(rBytes_, 0) != rBytes_.size() || readBE<uint32_t>(rBytes_, 0x0C) != 1 || readBE<uint32_t>(rBytes_, 0x10) != 0 || readBE<uint32_t>(rBytes_, 0x14) != 0x3142)
	{
		throw std::runtime_error("STH2005::readCCL: not a .CCL (size word or header constants differ).");
	}
	uint32_t sectionSize = readBE<uint32_t>(rBytes_, 4);
	uint32_t streamSize = readBE<uint32_t>(rBytes_, 8);
	if(SectionsOffset + sectionSize + streamSize > rBytes_.size())
	{
		throw std::runtime_error("STH2005::readCCL: the sections run past the end of the file.");
	}

	/* The record count is the number of pointers the relocation stream lists: its bytes with a word count (top bits 01, 10 or 11, see the layout above). */
	uint32_t count = 0;
	for(uint32_t i = 0; i < streamSize;)
	{
		uint8_t b = rBytes_[SectionsOffset + sectionSize + i];
		i += (b & 0xC0) == 0x80 ? 2 : (b & 0xC0) == 0xC0 ? 4 : 1;
		if((b & 0xC0) != 0)
		{
			count++;
		}
	}
	if(count == 0 || RecordSize * (count + 1) > sectionSize)
	{
		throw std::runtime_error("STH2005::readCCL: no records.");
	}

	std::vector<Physics::CollisionShape> shapes;
	shapes.reserve(count);
	uint32_t block = RecordSize * (count + 1);
	for(uint32_t i = 0; i < count; i++)
	{
		uint32_t record = SectionsOffset + i * RecordSize;
		uint32_t type = readBE<uint32_t>(rBytes_, record);
		uint32_t floatCount = blockFloats(type);
		if(readBE<uint32_t>(rBytes_, record + 4) != block || block + floatCount * 4 > sectionSize)
		{
			throw std::runtime_error(std::format("STH2005::readCCL: record {} has its block at the wrong place.", i));
		}
		float f[10];
		for(uint32_t k = 0; k < floatCount; k++)
		{
			f[k] = readBE<float>(rBytes_, SectionsOffset + block + k * 4);
		}
		block += floatCount * 4;

		/*
		Mapping to Physics::CollisionShape. Which floats are which comes from the game's Range classes (external, the decompile: the sphere copies 4
		floats into its fields, the box's bounding radius is the length of its 3 sizes, a capsule's is f3 + f4, a cylinder's sqrt(f3^2 + f4^2)),
		and the angles from their values (hypothesis: radians; their axis order is unknown and the game picks it per shape):
		type 0 Sphere: center, radius (form 0).
		types 1 Box and 4 Plane (a box whose second size is always 0): center, 3 half sizes, 3 angles, a zero (form 3).
		type 2 Capsule: center, f3, f4, 3 angles, a zero. Hypothesis: f4 the radius and f3 half the length of the segment along the shape's z
		axis (the game scales f4 with x and f3 with z). Form 6 (the lines view draws it as a capsule along z); a = radius, b = half length.
		type 3 Cylinder: same fields. Form 11 (a = radius, b = half length), a form of this importer's own so that the shape keeps its z axis (SADX's cylinders, forms 1 and 2, stand along y).
		The flags go to attr; the record's id, kind, push and damage have no counterpart here and stay 0 (the flags' and id's meaning is unknown).
		*/
		Physics::CollisionShape shape{};
		shape.attr = readBE<uint32_t>(rBytes_, record + 8);
		shape.center = glm::vec3(f[0], f[1], f[2]);
		if(type == 0)
		{
			shape.form = 0;
			shape.a = f[3];
		}
		else if(type == 1 || type == 4)
		{
			shape.form = 3;
			shape.a = f[3];
			shape.b = f[4];
			shape.c = f[5];
			shape.angle = glm::ivec3(radiansToBams(f[6]), radiansToBams(f[7]), radiansToBams(f[8]));
		}
		else
		{
			shape.form = type == 2 ? 6 : 11;
			shape.a = f[4];
			shape.b = f[3];
			shape.angle = glm::ivec3(radiansToBams(f[5]), radiansToBams(f[6]), radiansToBams(f[7]));
		}
		shapes.push_back(shape);
	}
	if(block != sectionSize)
	{
		throw std::runtime_error("STH2005::readCCL: the blocks do not end at the end of section 1.");
	}
	return shapes;
}
}
