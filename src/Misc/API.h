#pragma once 
#include "PoolId.h"
#include "Globals.h"

PoolId addEntity(const char* name_, glm::vec3 position_);
Entity& getEntity(PoolId id_);
Entity& getEntityByName(const char* name_);
PoolId addLevel(const char* name_, glm::vec3 position_);
Level& getLevel(PoolId id_);
Level& getLevelByName(const char* name_);
PoolId addBoundArea(uint32_t attribute_, bool isCollision_, void (*pOnTouch_)(PoolId entityId_) = nullptr);
void setCameraPosition(const glm::vec3& rPosition_);