#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include "PoolId.h"

namespace Physics
{
/*
One collision shape of an object, as the game stores it: the raw fields of SADX's 0x30-byte collision entry (external: decoded by
reading the collision init of the Ghidra pseudocode in sadxDecomp, fn_1_1E63C, and checked by dumping ~150 tables; hypothesis-level).
Nothing is interpreted here beyond the layout; what the values mean is only what the init code implies:
- form: the shape kind. 0 looks like a sphere (radius a), 1 and 2 like cylinders (a, b), 3 to 5 and 7 like boxes (a, b, c), 6 uses a + b.
  Higher values (8 to 10) appear in tables and are unknown. Values from 11 on are not SADX's: readCCL (Shadow's .CCL) uses 6 for a capsule and 11 for a cylinder, a = radius and b = half length, along the shape's z axis (hypothesis); 6 is also an SADX form of unknown meaning.
- kind, push, damage, attr: raw flags (what collides with what, push, damage, attributes); meanings unknown.
- behavior: the shape's behaviour (Globals::Game::behaviors), from attr by the remap of its source (BABehaviors::resolve); UninitializedPoolId until a loader sets it (a moving entity's shape then counts as a Body).
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
	PoolId behavior;

	bool operator==(const CollisionShape&) const = default;
};

/*
A shape of an entity scaled by the entity's scale (Entity::collisionScale), for the contacts and the lines view; the physics bodies' own shapes are not scaled (they do not call this).
Hypothesis (the port's choice, not the game's: whether the game's objects scale their collision is unknown): the center is scaled per axis; a box's half sizes by each axis; a length along an axis by that axis' component, and a radius by the larger
of the components it is measured across (a sphere: all three; the cylinders and capsules: the two perpendicular to their axis, which is y for forms 1 and 2 and SADX's 6, z for form 11 and Shadow's 6). The rest (8 to 10, drawn as a circle) by the largest. A shape
that is turned by its own angles is scaled along the entity's axes anyway, so it is only exact when the angles are zero.
*/
inline CollisionShape scaledShape(const CollisionShape& rShape_, const glm::vec3& rScale_, bool sadx_)
{
	glm::vec3 s = glm::abs(rScale_);
	CollisionShape shape = rShape_;
	shape.center *= rScale_;
	if((rShape_.form >= 3 && rShape_.form <= 5) || rShape_.form == 7)
	{
		shape.a *= s.x;
		shape.b *= s.y;
		shape.c *= s.z;
	}
	else if(rShape_.form == 1 || rShape_.form == 2 || (rShape_.form == 6 && sadx_))
	{
		shape.a *= std::max(s.x, s.z);
		shape.b *= s.y;
	}
	else if(rShape_.form == 6 || rShape_.form == 11)
	{
		shape.a *= std::max(s.x, s.y);
		shape.b *= s.z;
	}
	else
	{
		shape.a *= std::max({s.x, s.y, s.z});
	}
	return shape;
}
}
