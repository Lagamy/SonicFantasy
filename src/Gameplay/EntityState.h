#pragma once
#include <glm/glm.hpp>
#include "PoolId.h"

/*
The state an entity's behaviours keep between ticks, stored in Globals::Game::entityStates (a Pool) and found by the entity's stateId (Entity.h); made on first use by entityStateOf (API.h) and removed with the
entity. A plain bag of what behaviours so far need, shared by all of them (each says what it uses where it is written): a countdown in ticks, a phase, a number, a vector and a target entity. Nothing here has a
meaning of its own.
*/
struct EntityState
{
	float timer = 0.0f;
	int phase = 0;
	float value = 0.0f;
	glm::vec3 vector = glm::vec3(0.0f);
	PoolId target;
};
