#pragma once
#include "Entity.h"
#include "SAKeys.h"
#include "Camera.h"
#include "PoolId.h"
#include "Constants.h"
#include <vector>
#include <glm/glm.hpp>

/*
A level is the static world geometry of a stage, the counterpart of Entity for things without behaviour.
It is created first and gets its instance afterwards, with setSAGPUInstance (an SADX land table) or setONEGPUInstance
(a Shadow stage), both from the keys of ResourceKeys.h: whatever the keys name that is not loaded yet is loaded then.
Only one of the two can be called, once. Until instanceId is set it is neither updated nor drawn. Removing the level from its pool (destroy) removes the instance and its collision.
collisionId is the PoolId of the level's collision in Globals::Physics::collisions (UninitializedPoolId = none), created by setSACollision / setONECollision; it is independent of the instance, so a level can have collision without being drawn. Its Collision::position follows the level's position (setPosition, and when the collision is set). Setting or removing it, or moving the level, rebuilds Globals::Physics::quadTree (CollisionSystem::rebuildQuadTree).
setONEEntities creates the objects a Shadow stage places: an Entity (added to Globals::Game::entities, owned by the level and removed in destroy) for every record of the key's placement files whose type id the key lists, made from that EntityKey (its first model and its textures, and its collisions as shapes). Positions are the records' ones with the yaw the level is drawn with and the level's position; the records' rotation is given to the entities (Entity::setRotation); the other models (outside a Variant) are not used and the first motion of a key plays once (looping for the enemies and characters, EntityKey::loopMotion). A record whose type the key does not list gets a marker (a cross, see CollisionView::markers) instead of an entity. Only once; an object that fails to load is reported on stderr and skipped.
setSAEntities is the same for an SADX stage: an Entity for every record of the land table key's SET files whose type the key lists, made from that SAEntityKey (its model, textures, collision shapes and first motion, looping; rotation from the record's BAMS angles, no scale yet), at the record's position, and a marker for the others. Then an Entity for each of the key's stageEntities (objects the stage's own code creates, e.g. Emerald Coast's floating bridge). Only once, shared with setONEEntities (one per level).
sourceEngine says which engine the instance is from, as in Entity.
*/
struct Level
{
	SourceEngineEnum sourceEngine = SA_NINJA;
	PoolId instanceId = UninitializedPoolId;
	glm::vec3 position;
	PoolId collisionId = UninitializedPoolId;
	std::vector<PoolId> entityIds;

	void setSAGPUInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_);
	void setONEGPUInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_);
	void setPosition(const glm::vec3& rPosition_);
	void setONEEntities(PoolId shaderId_, const STH2005::LevelKey& rLevel_);
	void setSAEntities(PoolId shaderId_, const SALandTableKey& rLandTable_);
	void setSACollision(const SALandTableKey& rLandTable_);
	void setONECollision(const STH2005::LevelKey& rLevel_);
	void update(float deltaTime_);
	void draw(const Camera& rCamera_);
	void destroy();
};
