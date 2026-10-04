#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace Physics
{
/*
One collision shape of an object, as the game stores it: the raw fields of SADX's 0x30-byte collision entry (external: decoded by
reading the collision init of the Ghidra pseudocode in sadxDecomp, fn_1_1E63C, and checked by dumping ~150 tables; hypothesis-level).
Nothing is interpreted here beyond the layout; what the values mean is only what the init code implies:
- form: the shape kind. 0 looks like a sphere (radius a), 1 and 2 like cylinders (a, b), 3 to 5 and 7 like boxes (a, b, c), 6 uses a + b.
  Higher values (8 to 10) appear in tables and are unknown. Values from 11 on are not SADX's: readCCL (Shadow's .CCL) uses 6 for a capsule and 11 for a cylinder, a = radius and b = half length, along the shape's z axis (hypothesis); 6 is also an SADX form of unknown meaning.
- kind, push, damage, attr: raw flags (what collides with what, push, damage, attributes); meanings unknown.
- center is relative to the object; angle is the shape's rotation in BAMS (65536 = a full turn), the order of the axes unknown.
*/
struct CollisionShape
{
	uint8_t kind;
	uint8_t form;
	uint8_t push;
	uint8_t damage;
	uint32_t attr;
	glm::vec3 center;
	float a;
	float b;
	float c;
	float d;
	glm::ivec3 angle;
};
}
