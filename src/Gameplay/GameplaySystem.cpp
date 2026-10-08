#include "GameplaySystem.h"
#include "Globals.h"
#include "API.h"
#include "GPU_Globals.h"
#include "Scripting.h"
#include "Editor.h"
#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

static bool behaviorTypeExists(PoolId behaviorId_)
{
	return behaviorId_.id < Globals::Game::entityBehaviorTypes.alive.size() && Globals::Game::entityBehaviorTypes.alive[behaviorId_.id] && Globals::Game::entityBehaviorTypes.generation[behaviorId_.id] == behaviorId_.generation;
}

void GameplaySystem::update(float frameDeltaTime_)
{
	this->accumulator += frameDeltaTime_;
	float step = this->tickTime();
	int ticks = 0;
	auto start = std::chrono::steady_clock::now();
	while(this->accumulator >= step && ticks < this->maxTicksPerFrame)
	{
		this->tick();
		this->accumulator -= step;
		ticks++;
	}
	if(this->accumulator >= step) this->accumulator = 0.0f;
	this->ticksLastFrame = ticks;
	this->tickMillisecondsLastFrame = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

/*
Removes every entity in Globals::Game::markedForDestroy (Entity::markForDestroy; one that was marked twice or is already gone is skipped) and empties the pool. Entity::destroy releases the entity's
instance, collision, physics and behaviour record as the pool removes it.
*/
void GameplaySystem::destroyedCleanUp()
{
	std::vector<PoolId> marked;
	for(uint32_t i = 0; i < Globals::Game::markedForDestroy.objects.size(); i++)
	{
		if(Globals::Game::markedForDestroy.alive[i]) marked.push_back(Globals::Game::markedForDestroy.objects[i]);
	}
	Globals::Game::markedForDestroy.clear();
	for(PoolId entityId : marked)
	{
		if(entityExists(entityId)) Globals::Game::entities.remove(entityId);
	}
}

/*
The records and each one's behaviours are copied before anything is called: a behaviour can add or remove entities and behaviours, which would change the pools the loops are reading. An entity (or behaviour)
that is gone by its turn is skipped.
*/
void GameplaySystem::tick()
{
	Scripting::processPendingUnloads();
	this->destroyedCleanUp();

	/*
	Which entities are active this tick (SpatialIndex::updateActive): those within the draw distance of the camera, and those with behaviorNotCulled. Only they have their
	previous position (and a body's tickStartVelocity, PhysicsBody::moveByMeanVelocity) kept and are run by the behaviours, the physics and the collision system. There is no camera before the first one is made, then all are active.
	*/
	bool cull = this->cullFarEntities && Globals::Game::mainCamera != UninitializedPoolId;
	glm::vec3 cameraPosition = cull ? getMainCamera().position : glm::vec3(0.0f);
	float maxDistance = cull ? getMainCamera().farPlane : 0.0f;
	Globals::Game::spatialIndex.updateActive(cull, cameraPosition, maxDistance);
	for(uint32_t i : Globals::Game::spatialIndex.activeEntities)
	{
		Entity& rEntity = Globals::Game::entities.objects[i];
		rEntity.previousPosition = rEntity.position;
		rEntity.previousRotation = rEntity.rotation;
		if(rEntity.physicsId != UninitializedPoolId)
		{
			Physics::PhysicsBody& rBody = Globals::Physics::bodies[rEntity.physicsId];
			rBody.tickStartVelocity = rBody.velocity + rBody.floorCancelledVelocity;
			rBody.floorCancelledVelocity = glm::vec3(0.0f);
		}
	}

	/*
	In the editor (editorActive) nothing is simulated except the collision: no behaviours, no physics, no effects. The collision system moves an entity to its stagingPosition (PhysicsSystem::update), so each active
	entity's is set to where it is: collision then only pushes it out of what it overlaps and calls the interactions.
	*/
	if(editorActive())
	{
		for(uint32_t i : Globals::Game::spatialIndex.activeEntities)
		{
			Entity& rEntity = Globals::Game::entities.objects[i];
			rEntity.stagingPosition = rEntity.position;
		}
		Globals::Physics::collisionSystem.update();
		return;
	}

	std::vector<std::pair<PoolId, std::vector<PoolId>>> sets;
	for(uint32_t i = 0; i < Globals::Game::entityBehaviors.objects.size(); i++)
	{
		if(!Globals::Game::entityBehaviors.alive[i]) continue;
		const EntityBehaviorSet& rSet = Globals::Game::entityBehaviors.objects[i];
		sets.emplace_back(rSet.entityId, rSet.behaviorIds);
	}

	for(const auto& rSet : sets)
	{
		if(!entityExists(rSet.first) || !getEntity(rSet.first).active) continue;
		for(PoolId behaviorId : rSet.second)
		{
			if(!entityExists(rSet.first)) break;
			if(!behaviorTypeExists(behaviorId)) continue;
			const EntityBehavior& rBehavior = Globals::Game::entityBehaviorTypes[behaviorId];
			if(rBehavior.pBehave != nullptr) rBehavior.pBehave(rSet.first);
			else if(rBehavior.scriptFunction != 0) Scripting::callBehavior(rBehavior.scriptFunction, rSet.first, behaviorId);
		}
	}

	Globals::Physics::physicsSystem.update(this->tickTime());
	Globals::Physics::collisionSystem.update();
	Graphics::dashTrail.tick();
	Graphics::effectBalls.tick(this->tickTime());
}
