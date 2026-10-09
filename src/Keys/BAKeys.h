#pragma once
#include <cstdint>
#include <vector>

/*
The names of the two things collision behaviour is made of (BABehavior.h, BABehaviors.h):
- a tag is a category (Solid, Pickup, Hazard ...): its entry in Globals::Game::baTags, a PoolMap named by it, is only the name's PoolId. What a thing is is its list of tags.
- a behaviour is a bag of tags and rules: what a collision is (its tags) and what it does with the tags of what it touches (BABehavior::availableCollisions, availableInteractions). Its entry is in
  Globals::Game::behaviors, a PoolMap named by it. The remaps (BABehaviors.h, External/Collision/Remaps.json) give a game's raw values behaviours.
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
everything), and so do Passable (seen but not solid: what it stands for is unknown, see Remaps.json) and Object (a stage object's shape, until it is given tags). Body has the Body tag and collides with Solid.
*/
inline const BABehaviorKey BABehaviorNone{"None"};
inline const BABehaviorKey BABehaviorSolid{"Solid"};
inline const BABehaviorKey BABehaviorPickup{"Pickup"};
inline const BABehaviorKey BABehaviorHazard{"Hazard"};
inline const BABehaviorKey BABehaviorDeath{"Death"};
inline const BABehaviorKey BABehaviorPassable{"Passable"};
inline const BABehaviorKey BABehaviorBody{"Body"};
inline const BABehaviorKey BABehaviorObject{"Object"};
