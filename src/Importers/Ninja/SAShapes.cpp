#include "SAShapes.h"
#include "BinaryRead.h"

namespace Ninja
{
/*
Layout of an entry (external, from the SADX collision init in the Ghidra pseudocode, see CollisionShape.h), big-endian:
+0x00 u8 kind, +0x01 u8 form, +0x02 u8 push, +0x03 u8 damage, +0x04 u32 attr, +0x08 center (3 x f32), +0x14 a, +0x18 b, +0x1C c,
+0x20 d (f32), +0x24 angle x, y, z (s32, BAMS).
Checked on ~150 tables of _Main.rel: centers and sizes read as sensible values and angles as multiples of 16384 (a quarter turn).
*/
constexpr uint32_t ShapeSize = 0x30;

std::vector<Physics::CollisionShape> readSACollisionShapes(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t count_)
{
	std::vector<Physics::CollisionShape> shapes;
	shapes.reserve(count_);
	for(uint32_t i = 0; i < count_; i++)
	{
		uint32_t at = offset_ + i * ShapeSize;
		Physics::CollisionShape shape;
		shape.kind = readBE<uint8_t>(rBytes_, at);
		shape.form = readBE<uint8_t>(rBytes_, at + 1);
		shape.push = readBE<uint8_t>(rBytes_, at + 2);
		shape.damage = readBE<uint8_t>(rBytes_, at + 3);
		shape.attr = readBE<uint32_t>(rBytes_, at + 4);
		shape.center = glm::vec3(readBE<float>(rBytes_, at + 8), readBE<float>(rBytes_, at + 12), readBE<float>(rBytes_, at + 16));
		shape.a = readBE<float>(rBytes_, at + 0x14);
		shape.b = readBE<float>(rBytes_, at + 0x18);
		shape.c = readBE<float>(rBytes_, at + 0x1C);
		shape.d = readBE<float>(rBytes_, at + 0x20);
		shape.angle = glm::ivec3(readBE<int32_t>(rBytes_, at + 0x24), readBE<int32_t>(rBytes_, at + 0x28), readBE<int32_t>(rBytes_, at + 0x2C));
		shapes.push_back(shape);
	}
	return shapes;
}
}
