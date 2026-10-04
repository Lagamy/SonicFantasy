#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "PoolId.h"

/*
A thing that happened once in the gameplay (a ring was collected, a spring launched the player), for what presents it (a sound, an effect, a sparkle): the name (a string literal, which has to outlive the
event), the entity it is about (UninitializedPoolId = none) and where it happened, so a handler needs nothing from the entity.
*/
struct GameEvent
{
	const char* name;
	PoolId entityId;
	glm::vec3 position;
};

using GameEventHandler = void (*)(const GameEvent& rEvent_);

/*
The request list of one-shot presentation events. Behaviours (the gameplay) post events and never present anything themselves; once a frame, after the gameplay and the physics, drain hands each event to the handler
registered for its name (addHandler; none = nothing happens, it is only logged when logEvents is on) and empties the list. An event about an entity that is gone by then is dropped. A handler may post events:
they wait for the next drain. Looping presentation (an animation) is not an event: see Entity::animationIntent.
*/
struct GameEvents
{
	std::vector<GameEvent> pending;
	std::unordered_map<std::string, GameEventHandler> handlers;
	bool logEvents = false;

	void post(const char* name_, PoolId entityId_, const glm::vec3& rPosition_);
	void addHandler(const char* name_, GameEventHandler pHandler_);
	void drain();
};
