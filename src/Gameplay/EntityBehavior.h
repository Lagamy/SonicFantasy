#pragma once
#include <string>
#include <vector>
#include "PoolId.h"
#include "ExternalField.h"

/*
A piece of gameplay logic an entity can have, stored in Globals::Game::entityBehaviorTypes (a PoolMap named by the behaviour, so scripts and mods find one by name and keep its PoolId). pBehave is called
once a gameplay tick (GameplaySystem.h, not once a frame) for every entity that has the behaviour, with that entity's PoolId (GameplaySystem::tickTime is the tick's time). It may add or remove entities,
behaviours and the entity's own behaviours: the system works on copies of its lists. Not to be confused with BABehavior (BABehavior.h), what a collision does.
*/
struct EntityBehavior
{
	void (*pBehave)(PoolId entityId_) = nullptr;

	/* A behaviour written in a script has no pBehave: scriptFunction is the Lua registry reference of its function (Scripting.h), called with the entity's handle. 0 = none. */
	int scriptFunction = 0;

	/* A script behaviour: the file that registered it (sf.behavior), as the Lua chunk name without the '@' ("Assets/Scripts/sadx/objects.lua"), so the editor can offer a dropped script's behaviours. Empty otherwise. */
	std::string sourceFile;

	/* A script behaviour's declared fields (sf.behavior's third argument), in the order the inspector lists them. Before the behaviour's first call on an entity Scripting::callBehavior writes each into the entity's sf.state table: the value the editor set (Entity::externals) or the default. */
	std::vector<ExternalField> fields;
};

/*
The behaviours one entity has, stacked in the order they were given, run in that order: one record per entity that has any, stored in Globals::Game::entityBehaviors (a Pool, so the GameplaySystem walks
exactly the entities with behaviours and never looks at the others). The record is what feeds a behaviour its entity (entityId); the entity holds the record's id (Entity::behaviorSetId) like its other handles
and removes the record when it is destroyed. behaviorIds are PoolIds of Globals::Game::entityBehaviorTypes.
*/
struct EntityBehaviorSet
{
	PoolId entityId;
	std::vector<PoolId> behaviorIds;
};
