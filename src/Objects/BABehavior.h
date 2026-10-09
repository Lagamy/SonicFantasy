#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "PoolId.h"

/*
A tag, a category of collision (Solid, Pickup, Hazard ...): its entry in Globals::Game::baTags, a PoolMap named by its BATagKey, carries nothing but the name and so the PoolId; a behaviour lists the tags it has
and the tags it has rules for. Nothing in the engine knows what a tag means: the rules decide.
*/
struct BATag
{
};

namespace Scripting
{
void callInteraction(int scriptFunction_, PoolId parentEntity_, PoolId contactEntity_);
}

/*
What one behaviour does when its collision touches a collision with a tag: called with the PoolId of the entity that owns the behaviour's collision (parent) and the one it touched (contact). It is either a C++
function (pFunction) or a script function (scriptFunction: a Lua registry reference, see Scripting.h); a function pointer converts to one, so a behaviour lists `{tag, &function}`. Two are equal if they are the
same function, which is how the collision system calls each once.
*/
struct BAInteraction
{
	void (*pFunction)(PoolId parentEntity_, PoolId contactEntity_) = nullptr;
	int scriptFunction = 0;

	BAInteraction() = default;
	BAInteraction(void (*pFunction_)(PoolId parentEntity_, PoolId contactEntity_)) : pFunction(pFunction_) {}

	static BAInteraction script(int scriptFunction_)
	{
		BAInteraction interaction;
		interaction.scriptFunction = scriptFunction_;
		return interaction;
	}

	bool operator==(const BAInteraction& rOther_) const
	{
		return this->pFunction == rOther_.pFunction && this->scriptFunction == rOther_.scriptFunction;
	}

	void call(PoolId parentEntity_, PoolId contactEntity_) const
	{
		if(this->pFunction != nullptr) this->pFunction(parentEntity_, contactEntity_);
		else if(this->scriptFunction != 0) Scripting::callInteraction(this->scriptFunction, parentEntity_, contactEntity_);
	}
};

/*
A script function a bound area can name as an onTouch function, stored in Globals::Game::boundAreaFunctions (a PoolMap named by the function, so the editor and the project's data file find one by name): the
Lua registry reference of fn(parent, contact) (sf.onTouch) and the file that registered it, as the Lua chunk name without the '@', so the editor can list a script file's functions. The reference stays the same
when the name is registered again (the function behind it is replaced), so a behaviour built from it keeps working.
*/
struct BoundAreaFunction
{
	int scriptFunction = 0;
	std::string sourceFile;
};

/*
What a collision is and does, stored in Globals::Game::behaviors (a PoolMap named by the behaviour's BABehaviorKey) and found by its PoolId. A level triangle gets one when the collision is loaded
(Physics::Collision::behaviorOf) and an object's shape too (Physics::CollisionShape::behavior), from the remaps in External/Collision/Remaps.json (BABehaviors.h); the collision system only compares PoolIds and tags.
- tags: what it is (PoolIds in Globals::Game::baTags).
- availableCollisions: the tags it is stopped by (a body is stopped by Solid). When a shape of this behaviour overlaps a collision whose behaviour has one of them, the move is resolved: a physics entity's
  capsule or sphere is pushed out. Nothing else is kept per tag: a rule is only "is there one".
- availableInteractions: for a tag, the functions to call (a list: a tag may be under several) when this behaviour overlaps a collision with that tag. Called with this behaviour's entity as the parent and the other as the contact (UninitializedPoolId for
  a level's collision), once per gameplay tick and pair however many triangles or shapes overlap, after the entities have been moved. The function must not hold on to references into the entities pool.
For an overlap of A and B each side looks the other's tags up in its own two maps; if neither side finds one the overlap is skipped, so nothing branches on what B is.
Entity against entity (CollisionSystem.cpp): see updateEntityContacts.
*/
struct BABehavior
{
	std::vector<PoolId> tags = {};
	std::unordered_set<uint32_t> availableCollisions = {};
	std::unordered_map<uint32_t, std::vector<BAInteraction>> availableInteractions = {};

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

	/* Calls callback_ with every function this behaviour has for a tag the other has (the lists of all the other's tags; the same function twice is called twice, the collision system drops the repeat). */
	template <typename Callback>
	void forEachInteraction(const BABehavior& rOther_, Callback callback_) const
	{
		for(PoolId tag : rOther_.tags)
		{
			auto found = this->availableInteractions.find(tag.id);
			if(found == this->availableInteractions.end()) continue;
			for(const BAInteraction& rInteraction : found->second) callback_(rInteraction);
		}
	}

	/* True if it has any rule at all: a shape whose behaviour has none never starts a contact test. */
	bool hasRules() const
	{
		return !this->availableCollisions.empty() || !this->availableInteractions.empty();
	}
};
