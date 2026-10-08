#pragma once 
#include "PoolId.h"
#include "Globals.h"

namespace Ninja
{
struct SAPlacement;
}

PoolId addEntity(const char* name_, glm::vec3 position_, PoolMap<Entity>* pPool_ = &Globals::Game::entities);
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
bool removeEntityBehavior(PoolId entityId_, PoolId behaviorId_);
void setEntityBehaviors(PoolId entityId_, const std::vector<PoolId>& rBehaviorIds_);
std::vector<Physics::CollisionShape> entityShapes(PoolId entityId_);
void setEntityShapes(PoolId entityId_, const std::vector<Physics::CollisionShape>& rShapes_);
TextureChoice entityTexture(PoolId entityId_);
void setEntityTexture(PoolId entityId_, const TextureChoice& rChoice_);
MeshChoice entityMesh(PoolId entityId_);
void setEntityMesh(PoolId entityId_, const MeshChoice& rChoice_);
PoolId spawnPrefab(PoolId prefabId_, PoolId shaderId_, const SpawnParams& rParams_, PoolMap<Entity>* pPool_ = &Globals::Game::entities);
void setCameraPosition(const glm::vec3& rPosition_);
PoolId addCamera(const char* name_, const glm::vec3& rPosition_);
Camera& getMainCamera();
PoolId addRenderTarget(uint32_t width_, uint32_t height_);