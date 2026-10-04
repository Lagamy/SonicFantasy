#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "PoolId.h"

/*
A tag, a category of collision (Solid, Pickup, Hazard ...): its entry in Globals::Game::baTags, a PoolMap named by its BATagKey, carries nothing but the name and so the PoolId; a behaviour lists the tags it has
and the tags it has rules for. Nothing in the engine knows what a tag means: the rules decide.
*/
struct BATag
{
};

/* What one behaviour does when its collision touches a collision with a tag: called with the PoolId of the entity that owns the behaviour's collision (parent) and the one it touched (contact). */
using BAInteraction = void (*)(PoolId parentEntity_, PoolId contactEntity_);

/*
What a collision is and does, stored in Globals::Game::behaviors (a PoolMap named by the behaviour's BABehaviorKey) and found by its PoolId. A level triangle gets one when the collision is loaded
(Physics::Collision::behaviorOf) and an object's shape too (Physics::CollisionShape::behavior), from the remaps in BAKeys.h; the collision system only compares PoolIds and tags.
- tags: what it is (PoolIds in Globals::Game::baTags).
- availableCollisions: the tags it is stopped by (a body is stopped by Solid). When a shape of this behaviour overlaps a collision whose behaviour has one of them, the move is resolved: a physics entity's
  capsule or sphere is pushed out. Nothing else is kept per tag: a rule is only "is there one".
- availableInteractions: for a tag, the function to call when this behaviour overlaps a collision with that tag. Called with this behaviour's entity as the parent and the other as the contact (UninitializedPoolId for
  a level's collision), once per gameplay tick and pair however many triangles or shapes overlap, after the entities have been moved. The function must not hold on to references into the entities pool.
For an overlap of A and B each side looks the other's tags up in its own two maps; if neither side finds one the overlap is skipped, so nothing branches on what B is.
Entity against entity (CollisionSystem.cpp): see updateEntityContacts.
*/
struct BABehavior
{
	std::vector<PoolId> tags = {};
	std::unordered_set<uint32_t> availableCollisions = {};
	std::unordered_map<uint32_t, BAInteraction> availableInteractions = {};

	/* True if this behaviour is stopped by a tag the other has. */
	bool collidesWith(const BABehavior& rOther_) const
	{
		for(PoolId tag : rOther_.tags)
		{
			if(this->availableCollisions.contains(tag.id)) return true;
		}
		return false;
	}

	/* True if this behaviour has an interaction for a tag the other has. */
	bool interactsWith(const BABehavior& rOther_) const
	{
		for(PoolId tag : rOther_.tags)
		{
			if(this->availableInteractions.contains(tag.id)) return true;
		}
		return false;
	}

	/* Calls callback_ with the function of every interaction this behaviour has for a tag the other has. */
	template <typename Callback>
	void forEachInteraction(const BABehavior& rOther_, Callback callback_) const
	{
		for(PoolId tag : rOther_.tags)
		{
			auto found = this->availableInteractions.find(tag.id);
			if(found != this->availableInteractions.end()) callback_(found->second);
		}
	}

	/* True if it has any rule at all: a shape whose behaviour has none never starts a contact test. */
	bool hasRules() const
	{
		return !this->availableCollisions.empty() || !this->availableInteractions.empty();
	}
};
