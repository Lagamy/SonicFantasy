#include "API.h"
#include "Entity.h"
#include "Level.h"
#include <format>
#include <stdexcept>

PoolId addEntity(const char* name_, glm::vec3 position_)
{
    return Globals::Game::entities.add(name_, Entity{SA_NINJA, UninitializedPoolId, position_}); 
}

/* The reference is valid only until the next addEntity (the pool can move its entities): keep the PoolId, not the reference. */
Entity& getEntity(PoolId id_)
{
    return Globals::Game::entities[id_];
}

/*
The entity added under that name (the first one if a name was used twice). Throws if there is none, in every build: PoolMap::getIdByName
only checks in Debug builds. Same lifetime warning as getEntity.
*/
Entity& getEntityByName(const char* name_)
{
    auto found = Globals::Game::entities.nameToId.find(name_);
    if(found == Globals::Game::entities.nameToId.end())
    {
        throw std::runtime_error(std::format("getEntityByName: no entity named {}.", name_));
    }
    return Globals::Game::entities[found->second];
}

PoolId addLevel(const char* name_, glm::vec3 position_)
{
    return Globals::Game::levels.add(name_, Level{SA_NINJA, UninitializedPoolId, position_}); 
}

/* Registers what a triangle attribute does (see BoundArea). At most one area per attribute is meant: the first one found for an attribute is used. */
PoolId addBoundArea(uint32_t attribute_, bool isCollision_, void (*pOnTouch_)(PoolId entityId_))
{
    return Globals::Game::boundAreas.add(BoundArea{attribute_, isCollision_, pOnTouch_});
}

/* Moves the player camera (index 0 of Globals::Game::cameras); the view and its UBO are updated by Camera::set. */
void setCameraPosition(const glm::vec3& rPosition_)
{
    if(Globals::Game::cameras.empty())
    {
        throw std::runtime_error("setCameraPosition: there is no player camera.");
    }
    Globals::Game::cameras[0].set(rPosition_);
}

/* Same lifetime warning as getEntity: valid only until the next addLevel. */
Level& getLevel(PoolId id_)
{
    return Globals::Game::levels[id_];
}

/* Like getEntityByName: the first level added under that name, throws if there is none. */
Level& getLevelByName(const char* name_)
{
    auto found = Globals::Game::levels.nameToId.find(name_);
    if(found == Globals::Game::levels.nameToId.end())
    {
        throw std::runtime_error(std::format("getLevelByName: no level named {}.", name_));
    }
    return Globals::Game::levels[found->second];
}
