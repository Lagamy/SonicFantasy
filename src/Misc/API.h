#pragma once 
#include "PoolId.h"
#include "Globals.h"

namespace Ninja
{
struct SAPlacement;
}

PoolId addEntity(const char* name_, glm::vec3 position_);
Entity& getEntity(PoolId id_);
Entity& getEntityByName(const char* name_);
PoolId addLevel(const char* name_, glm::vec3 position_);
Level& getLevel(PoolId id_);
Level& getLevelByName(const char* name_);
PoolId addBATag(const BATagKey& rKey_);
PoolId addBehavior(const BABehaviorKey& rKey_, BABehavior behavior_);
bool entityExists(PoolId entityId_);
void changeAnimation(PoolId entityId_, const SAMotionKey& rKey_);
void changeAnimation(PoolId entityId_, const SAMotionKey& rKey_, const SAModelKey& rMesh_);
void changeAnimation(PoolId entityId_, const STH2005::MotionKey& rKey_, bool loop_ = true);
void blinkAnimation(PoolId entityId_, const SAMotionKey* pKey_, const SAModelKey* pMesh_);
void postGameEvent(const char* name_, PoolId entityId_, const glm::vec3& rPosition_);
void addGameEventHandler(const char* name_, GameEventHandler pHandler_);
EntityState& entityStateOf(PoolId entityId_);
PoolId addEntityBehavior(const char* name_, void (*pBehave_)(PoolId entityId_));
void giveEntityBehavior(PoolId entityId_, PoolId behaviorId_);
PoolId spawnSAObject(PoolId shaderId_, const SAEntityKey& rKey_, const SAModelKey& rModel_, const std::vector<SACollisionShapesKey>& rCollisions_, const glm::vec3& rPosition_, const glm::mat4& rRotation_, const std::string& name_, const std::string& what_, const Ninja::SAPlacement* pPlacement_);
PoolId spawnONEObject(PoolId shaderId_, const STH2005::EntityKey& rKey_, uint32_t modelIndex_, const std::vector<STH2005::CollisionKey>& rCollisions_, const glm::vec3& rPosition_, const glm::mat4& rRotation_, const std::string& name_, const std::string& what_);
void setCameraPosition(const glm::vec3& rPosition_);