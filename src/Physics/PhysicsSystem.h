#pragma once

namespace Physics
{
/*
Moves every entity that has a physics (Entity::physicsId): gravity changes the body's velocity and the entity's
stagingPosition becomes position + velocity * deltaTime. It does not touch position: the collision system decides
what happens to the staging position and then moves the entity (CollisionSystem::update runs after this one).
*/
struct PhysicsSystem
{
	void update(float deltaTime_);
};
}
