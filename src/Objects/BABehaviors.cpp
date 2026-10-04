#include "BABehaviors.h"
#include "Globals.h"
#include <format>
#include <stdexcept>
#include <utility>

void BABehaviors::init()
{
	this->tagSolid = this->addTag(BATagSolid);
	this->tagPickup = this->addTag(BATagPickup);
	this->tagHazard = this->addTag(BATagHazard);
	this->tagDeath = this->addTag(BATagDeath);
	this->tagBody = this->addTag(BATagBody);

	this->none = this->add(BABehaviorNone, {});
	this->solid = this->add(BABehaviorSolid, BABehavior{{this->tagSolid}, {}, {}});
	this->pickup = this->add(BABehaviorPickup, BABehavior{{this->tagPickup}, {}, {}});
	this->hazard = this->add(BABehaviorHazard, BABehavior{{this->tagHazard}, {}, {}});
	this->death = this->add(BABehaviorDeath, BABehavior{{this->tagDeath}, {}, {}});
	this->passable = this->add(BABehaviorPassable, {});
	this->object = this->add(BABehaviorObject, {});
	this->body = this->add(BABehaviorBody, BABehavior{{this->tagBody}, {this->tagSolid.id}, {}});
}

PoolId BABehaviors::addTag(const BATagKey& rKey_)
{
	if(Globals::Game::baTags.nameToId.contains(rKey_.name))
	{
		throw std::runtime_error(std::format("BABehaviors: a tag named {} exists already.", rKey_.name));
	}
	return Globals::Game::baTags.add(rKey_.name);
}

PoolId BABehaviors::add(const BABehaviorKey& rKey_, BABehavior behavior_)
{
	if(Globals::Game::behaviors.nameToId.contains(rKey_.name))
	{
		throw std::runtime_error(std::format("BABehaviors: a behaviour named {} exists already.", rKey_.name));
	}
	return Globals::Game::behaviors.add(rKey_.name, std::move(behavior_));
}

PoolId BABehaviors::findTag(const char* name_) const
{
	auto found = Globals::Game::baTags.nameToId.find(name_);
	return found != Globals::Game::baTags.nameToId.end() ? found->second : UninitializedPoolId;
}

PoolId BABehaviors::find(const char* name_) const
{
	auto found = Globals::Game::behaviors.nameToId.find(name_);
	return found != Globals::Game::behaviors.nameToId.end() ? found->second : UninitializedPoolId;
}

PoolId BABehaviors::idOfTag(const BATagKey& rKey_) const
{
	PoolId id = this->findTag(rKey_.name);
	if(id == UninitializedPoolId)
	{
		throw std::runtime_error(std::format("BABehaviors: there is no tag named {}.", rKey_.name));
	}
	return id;
}

PoolId BABehaviors::idOf(const BABehaviorKey& rKey_) const
{
	PoolId id = this->find(rKey_.name);
	if(id == UninitializedPoolId)
	{
		throw std::runtime_error(std::format("BABehaviors: there is no behaviour named {}.", rKey_.name));
	}
	return id;
}

void BABehaviors::resolve(Physics::Collision& rCollision_, const BARemap& rRemap_) const
{
	PoolId fallback = this->idOf(*rRemap_.fallback);
	auto behaviorOf = [&](uint32_t raw_) -> PoolId
	{
		for(const BARemapEntry& rEntry : rRemap_.entries)
		{
			if(rEntry.raw == raw_) return this->idOf(*rEntry.behavior);
		}
		return fallback;
	};

	rCollision_.fallbackBehavior = fallback;
	const Physics::CollisionMesh& rMesh = rCollision_.mesh;
	if(!rMesh.triangleAttributes.empty())
	{
		for(uint32_t value : rMesh.triangleAttributes)
		{
			if(!rCollision_.behaviorByValue.contains(value)) rCollision_.behaviorByValue.emplace(value, behaviorOf(value));
		}
	}
	else
	{
		for(const Physics::CollisionPiece& rPiece : rMesh.pieces)
		{
			if(!rCollision_.behaviorByValue.contains(rPiece.flags)) rCollision_.behaviorByValue.emplace(rPiece.flags, behaviorOf(rPiece.flags));
		}
	}
	for(Physics::CollisionShape& rShape : rCollision_.shapes)
	{
		rShape.behavior = behaviorOf(rShape.attr);
	}
}
