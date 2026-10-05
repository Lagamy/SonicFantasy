#pragma once
#include "PoolId.h"
#include <glm/glm.hpp>

/*
Test setup of the physics and collision systems that main.cpp calls while they are being worked on: what the triangle values do and
the physics of the two test entities. Nothing here is needed by the systems themselves.
*/
namespace DebugSetup
{
/* Gives both test entities a physics of weight 1 (the gravity is Globals::Physics::gravity). */
void enablePhysics(PoolId sonicId_, PoolId shadowId_);

/* Gives Shadow a hand-made collision shape (the body capsule seen in a RAM dump, see DebugSetup.cpp). */
void addShadowShape(PoolId shadowId_);

/* Gives Sonic a hand-made body sphere (see DebugSetup.cpp). */
void addSonicShape(PoolId sonicId_);

/*
Reads a Shadow stage's placement file from the disc (e.g. "stg0100/stg0100_nrm.dat") and adds a marker for each of its objects to the collision view
(C key), colored by the object's type, at the position the level draws it at (see DebugSetup.cpp). levelPosition_ is the position of the level.
*/
void addShadowPlacementMarkers(const char* path_, const glm::vec3& rLevelPosition_);
}
