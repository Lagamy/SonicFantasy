#pragma once
#include "PoolId.h"
#include <glm/glm.hpp>

/*
Test setup of the physics and collision systems that main.cpp calls while they are being worked on: what the triangle values do and
the physics of the two test entities (their body shapes are their prefabs' Changed files). Nothing here is needed by the systems themselves.
*/
namespace DebugSetup
{
/* Gives both test entities a physics of weight 1 (the gravity is Globals::Physics::gravity). */
void enablePhysics(PoolId sonicId_, PoolId shadowId_);

#ifdef EDITOR
/*
Reads a Shadow stage's placement file from the disc (e.g. "stg0100/stg0100_nrm.dat") and adds a marker for each of its objects to the collision view
(C key), colored by the object's type, at the position the level draws it at (see DebugSetup.cpp). levelPosition_ is the position of the level.
*/
void addShadowPlacementMarkers(const char* path_, const glm::vec3& rLevelPosition_);
#endif
}
