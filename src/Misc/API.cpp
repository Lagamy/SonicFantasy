#include "API.h"
#include "Entity.h"
#include "Level.h"
#include <format>
#include <stdexcept>
#include <utility>

PoolId addEntity(const char* name_, glm::vec3 position_)
{
    PoolId id = Globals::Game::entities.add(name_, Entity{SA_NINJA, UninitializedPoolId, position_});
    Globals::Game::entities[id].selfId = id;
    Globals::Game::entities[id].previousPosition = position_;
    Globals::Game::spatialIndex.addDynamic(id);
    return id;
}

/* The reference stays valid while the entity does (the pool does not move its entities when it grows): once it is removed the slot can be reused, so keep the PoolId across anything that removes entities. */
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

/* Adds a collision tag (a category, see BATag) under a name. */
PoolId addBATag(const BATagKey& rKey_)
{
    return Globals::Game::baBehaviors.addTag(rKey_);
}

/* Adds a collision behaviour (a bag of tags and rules, see BABehavior) under its key's name. */
PoolId addBehavior(const BABehaviorKey& rKey_, BABehavior behavior_)
{
    return Globals::Game::baBehaviors.add(rKey_, std::move(behavior_));
}

/* True if the id names a live entity (its slot is alive and the generation matches), for code that holds an id across things that can remove entities. */
bool entityExists(PoolId entityId_)
{
    return entityId_.id < Globals::Game::entities.alive.size() && Globals::Game::entities.alive[entityId_.id] && Globals::Game::entities.generation[entityId_.id] == entityId_.generation;
}

/*
Sets the animation an entity wants (Entity::animationIntent): the graphics side switches to it once a frame, only if it is not already playing, so a behaviour can call this every tick. The key has to outlive the
entity (the keys of ResourceKeys.h are static); the last call wins. The SA overload loops, as playSAMotion does.
*/
void changeAnimation(PoolId entityId_, const SAMotionKey& rKey_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    rEntity.animationIntent = AnimationIntent{&rKey_, nullptr, true};
}

void changeAnimation(PoolId entityId_, const STH2005::MotionKey& rKey_, bool loop_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    rEntity.animationIntent = AnimationIntent{nullptr, &rKey_, loop_};
}

/* Posts a one-shot presentation event (see GameEvents) and registers what handles one by name. */
void postGameEvent(const char* name_, PoolId entityId_, const glm::vec3& rPosition_)
{
    Globals::Game::gameEvents.post(name_, entityId_, rPosition_);
}

void addGameEventHandler(const char* name_, GameEventHandler pHandler_)
{
    Globals::Game::gameEvents.addHandler(name_, pHandler_);
}

/* The state of an entity's behaviours (EntityState.h), made on the first call. The reference stays valid until the entity is removed. */
EntityState& entityStateOf(PoolId entityId_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    if(rEntity.stateId == UninitializedPoolId)
    {
        rEntity.stateId = Globals::Game::entityStates.add(EntityState{});
    }
    return Globals::Game::entityStates[rEntity.stateId];
}

/* Adds an entity behaviour under a name (see EntityBehavior); find it again with Globals::Game::entityBehaviorTypes.nameToId. */
PoolId addEntityBehavior(const char* name_, void (*pBehave_)(PoolId entityId_))
{
    return Globals::Game::entityBehaviorTypes.add(name_, EntityBehavior{pBehave_});
}

/* Stacks a behaviour on an entity (after the ones it has); its first one makes the entity's record in Globals::Game::entityBehaviors, which the GameplaySystem walks. */
void giveEntityBehavior(PoolId entityId_, PoolId behaviorId_)
{
    Entity& rEntity = Globals::Game::entities[entityId_];
    if(rEntity.behaviorSetId == UninitializedPoolId)
    {
        rEntity.behaviorSetId = Globals::Game::entityBehaviors.add(EntityBehaviorSet{entityId_, {}});
    }
    Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds.push_back(behaviorId_);
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

/* Same lifetime as getEntity: valid while the level is. */
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
