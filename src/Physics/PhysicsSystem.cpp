#include "PhysicsSystem.h"
#include "Globals.h"
#include <algorithm>

namespace Physics
{
/*
Longest time step the physics takes (a guess): a longer frame (a loading hitch, a paused debugger) is simulated as this long, so a
body can't cross a surface in one step just because the frame was slow.
*/
constexpr float MaxDeltaTime = 0.05f;

void PhysicsSystem::update(float deltaTime_)
{
	deltaTime_ = std::min(deltaTime_, MaxDeltaTime);
	for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
	{
		if(!Globals::Game::entities.alive[i]) continue;
		Entity& rEntity = Globals::Game::entities.objects[i];
		if(rEntity.physicsId == UninitializedPoolId) continue;

		PhysicsBody& rBody = Globals::Physics::bodies[rEntity.physicsId];
		rBody.velocity += Globals::Physics::gravity * rBody.weight * deltaTime_;
		rEntity.stagingPosition = rEntity.position + rBody.velocity * deltaTime_;
	}
}
}
