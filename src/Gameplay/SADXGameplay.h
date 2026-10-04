#pragma once
#include "PoolId.h"

namespace Ninja
{
struct SAPlacement;
}

/*
Gameplay of Sonic Adventure DX objects and the player, built on the behaviour scaffolding (BABehavior.h, EntityBehavior.h, EntityState.h). Each thing says in SADXGameplay.cpp what is decoded from the game
(external, the pseudocode of _Main.rel) and what is a placeholder.
registerAll adds the tags, collision behaviours and entity behaviours (once, after BABehaviors::init and before any level is made); setupObject gives a new stage object the behaviours its type has (called by
Level::setSAEntities for every object, with its SET record, null for an object the stage creates); attachPlayer makes an entity the player: the keyboard moves it.
*/
namespace SADXGameplay
{
void registerAll();
void setupObject(PoolId entityId_, const char* name_, const Ninja::SAPlacement* pPlacement_);
void attachPlayer(PoolId entityId_);
}
