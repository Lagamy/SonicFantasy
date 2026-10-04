#pragma once
#include <cstdint>
#include <vector>

/*
The names of the two things collision behaviour is made of (BABehavior.h, BABehaviors.h):
- a tag is a category (Solid, Pickup, Hazard ...): its entry in Globals::Game::baTags, a PoolMap named by it, is only the name's PoolId. What a thing is is its list of tags.
- a behaviour is a bag of tags and rules: what a collision is (its tags) and what it does with the tags of what it touches (BABehavior::availableCollisions, availableInteractions). Its entry is in
  Globals::Game::behaviors, a PoolMap named by it. The remaps below give a game's raw values behaviours.
A script (a mod) finds a tag or a behaviour by name (BABehaviors::findTag / find) and keeps its PoolId; the built-in ones below are in the pools from the start.
*/
struct BATagKey
{
	const char* name;
};

struct BABehaviorKey
{
	const char* name;
};

/*
Built-in tags, the basic categories: Solid (what a body is stopped by), Pickup (collected on touch: rings, items), Hazard (hurts on touch), Death (kills on touch: a death plane), and Body (a moving
entity's own shapes: what the others' rules name when they act on a character). They carry no rules themselves; the behaviours that have them do.
*/
inline const BATagKey BATagSolid{"Solid"};
inline const BATagKey BATagPickup{"Pickup"};
inline const BATagKey BATagHazard{"Hazard"};
inline const BATagKey BATagDeath{"Death"};
inline const BATagKey BATagBody{"Body"};

/*
Built-in behaviours, named like the one tag they have, with no rules (the interactions come from whoever has a rule for the tag): Solid, Pickup, Hazard, Death. None has no tags and no rules (ignored by
everything), and so do Passable (seen but not solid: what it stands for is unknown, see the remaps) and Object (a stage object's shape, until it is given tags). Body has the Body tag and collides with Solid.
*/
inline const BABehaviorKey BABehaviorNone{"None"};
inline const BABehaviorKey BABehaviorSolid{"Solid"};
inline const BABehaviorKey BABehaviorPickup{"Pickup"};
inline const BABehaviorKey BABehaviorHazard{"Hazard"};
inline const BABehaviorKey BABehaviorDeath{"Death"};
inline const BABehaviorKey BABehaviorPassable{"Passable"};
inline const BABehaviorKey BABehaviorBody{"Body"};
inline const BABehaviorKey BABehaviorObject{"Object"};

/*
A remap: which behaviour each raw value of a game has, written out in advance (BABehaviors::resolve looks them up once when a collision is loaded, so the collision system never sees raw values). A raw value
that is not listed has the fallback. Four are used, one per kind of data: a level's triangle values and an object's shape attributes (Physics::CollisionShape::attr), for each game.
*/
struct BARemapEntry
{
	uint32_t raw;
	const BABehaviorKey* behavior;
};

struct BARemap
{
	std::vector<BARemapEntry> entries;
	const BABehaviorKey* fallback;
};

/*
Shadow's level attributes (observed, from you, in the attribute view of stg0100): 0x2, 0x202, 0x20001, 0x8 and 0x28001 (the loop) are collision, 0x10 is some kind of bound or trigger (Passable until it is known) and 0x20 a death plane (Death; nothing
acts on the tag yet). Any other value is ignored (None).
*/
inline const BARemap BARemapShadowLevel{{{0x2, &BABehaviorSolid}, {0x202, &BABehaviorSolid}, {0x20001, &BABehaviorSolid}, {0x8, &BABehaviorSolid}, {0x28001, &BABehaviorSolid}, {0x10, &BABehaviorPassable}, {0x20, &BABehaviorDeath}}, &BABehaviorNone};

/*
SADX's level triangle values are the surface flags of the land table COL: what the bits mean is unknown (only 0x80000000, drawn, is a hypothesis, see ResourceLoader.cpp), so nothing is listed and all of its
collision stays solid. List a flags value here once you know it.
*/
inline const BARemap BARemapSADXLevel{{}, &BABehaviorSolid};

/* The attributes of object shapes (SADX's tables, Shadow's .CCL flags): their meaning is unknown, so every shape is an Object for now. */
inline const BARemap BARemapSADXShape{{}, &BABehaviorObject};
inline const BARemap BARemapShadowShape{{}, &BABehaviorObject};
