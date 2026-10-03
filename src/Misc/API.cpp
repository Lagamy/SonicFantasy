#include "API.h"
#include "Entity.h"

PoolId addEntity(const char* name_, glm::vec3 position_)
{
    return Globals::Game::entities.add(name_, Entity{SA_NINJA, UninitializedPoolId, position_}); 
}