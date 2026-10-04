#include "GameEvents.h"
#include "API.h"
#include "Globals.h"
#include <cstdio>
#include <utility>

void GameEvents::post(const char* name_, PoolId entityId_, const glm::vec3& rPosition_)
{
	this->pending.push_back(GameEvent{name_, entityId_, rPosition_});
}

void GameEvents::addHandler(const char* name_, GameEventHandler pHandler_)
{
	this->handlers[name_] = pHandler_;
}

void GameEvents::drain()
{
	std::vector<GameEvent> events = std::move(this->pending);
	this->pending.clear();
	for(const GameEvent& rEvent : events)
	{
		if(rEvent.entityId != UninitializedPoolId && !entityExists(rEvent.entityId)) continue;
		if(this->logEvents)
		{
			std::fprintf(stderr, "Event %s: entity %s at (%.1f, %.1f, %.1f)\n", rEvent.name, rEvent.entityId != UninitializedPoolId ? Globals::Game::entities.names[rEvent.entityId.id].c_str() : "-", rEvent.position.x, rEvent.position.y, rEvent.position.z);
		}
		auto found = this->handlers.find(rEvent.name);
		if(found != this->handlers.end()) found->second(rEvent);
	}
}
