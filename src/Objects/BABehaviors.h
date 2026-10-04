#pragma once
#include "BAKeys.h"
#include "BABehavior.h"

namespace Physics
{
struct Collision;
}

/*
Our own convention for collision behaviours (the engine's, not either game's): this struct describes and fills Globals::Game::baTags and Globals::Game::behaviors (init: the built-in tags and behaviours of
BAKeys.h and what they collide with) and gives a collision its behaviours from a remap. The built-in ids are kept here so code can use them without looking a name up; a script or a mod adds a tag with addTag
and a behaviour with add, and finds them with findTag and find.
*/
struct BABehaviors
{
	PoolId tagSolid;
	PoolId tagPickup;
	PoolId tagHazard;
	PoolId tagDeath;
	PoolId tagBody;

	PoolId none;
	PoolId solid;
	PoolId pickup;
	PoolId hazard;
	PoolId death;
	PoolId passable;
	PoolId body;
	PoolId object;

	/* Adds the built-in tags and behaviours; once, before any collision is loaded (resolve needs them). */
	void init();

	/* Adds a tag or a behaviour under its key's name; throws if that name is taken in that pool. */
	PoolId addTag(const BATagKey& rKey_);
	PoolId add(const BABehaviorKey& rKey_, BABehavior behavior_);

	/* The id of the tag / behaviour named name_, UninitializedPoolId if there is none. idOfTag and idOf are the same for a key and throw if there is none. */
	PoolId findTag(const char* name_) const;
	PoolId find(const char* name_) const;
	PoolId idOfTag(const BATagKey& rKey_) const;
	PoolId idOf(const BABehaviorKey& rKey_) const;

	/*
	Gives every distinct raw triangle value of the collision's mesh (its own attribute, or its piece's flags, see CollisionMesh::triangleValue) its behaviour in Collision::behaviorByValue, sets
	Collision::fallbackBehavior and the behaviour of every shape from its attr, all from rRemap_. Called when a collision is loaded.
	*/
	void resolve(Physics::Collision& rCollision_, const BARemap& rRemap_) const;
};
