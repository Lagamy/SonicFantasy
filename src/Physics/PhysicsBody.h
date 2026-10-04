#pragma once
#include <glm/glm.hpp>

namespace Physics
{
/*
The physics state of one entity, stored in Globals::Physics::bodies and found by the entity's physicsId.
weight scales the global gravity (Globals::Physics::gravity) for this entity; velocity is in units per second and
is changed by the physics system (gravity) and the collision system (landing).
*/
struct PhysicsBody
{
	float weight = 1.0f;
	glm::vec3 velocity = glm::vec3(0.0f);
};
}
