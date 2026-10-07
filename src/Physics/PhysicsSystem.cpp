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
	/* Only the active entities (within the draw distance of the camera, SpatialIndex::updateActive). */
	for(uint32_t i : Globals::Game::spatialIndex.activeEntities)
	{
		if(!Globals::Game::entities.alive[i]) continue;
		Entity& rEntity = Globals::Game::entities.objects[i];
		if(rEntity.physicsId == UninitializedPoolId) continue;

		/* The move takes the end velocity or the mean of the tick's start and end velocities (PhysicsBody::moveByMeanVelocity). */
		PhysicsBody& rBody = Globals::Physics::bodies[rEntity.physicsId];
		rBody.velocity += Globals::Physics::gravity * rBody.weight * deltaTime_;
		glm::vec3 moveVelocity = rBody.moveByMeanVelocity ? (rBody.tickStartVelocity + rBody.velocity) * 0.5f : rBody.velocity;
		/*
		A body on a walkable floor that moves by the mean velocity: the part of the move into the floor is made straight down, of the same depth into the floor, because the floor's push (CollisionSystem) is straight up. A press along the normal
		(Shadow's player presses along it from GROUND_GRAVITY_FAST) would otherwise be undone straight up and leave the body 11.34 * sqrt(1 - ny^2) a second uphill along the surface, which the original does not do. Observed
		(live_slopes, scratchpad pressdrift.py): the sum of those press displacements, 4.3 at 2.0 s and 8.1 at 3.5 s, points the way the remaining path drift does. Hypothesis: that is the drift's cause; not run.
		*/
		if(rBody.moveByMeanVelocity && rBody.grounded && rBody.groundNormal.y > 1e-3f)
		{
			float into = glm::dot(moveVelocity, rBody.groundNormal);
			if(into < 0.0f)
			{
				moveVelocity -= rBody.groundNormal * into;
				moveVelocity.y += into / rBody.groundNormal.y;
			}
		}
		rEntity.stagingPosition = rEntity.position + moveVelocity * deltaTime_;
	}
}
}
