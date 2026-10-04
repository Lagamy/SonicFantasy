#include "DebugSetup.h"
#include "API.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include "CollisionView.h"
#include "ONEInstance.h"
#include "Placement.h"
#include <glm/ext/matrix_transform.hpp>

namespace DebugSetup
{
/*
Shadow (observed, from you, in the attribute view of stg0100): 0x2, 0x202, 0x20001, 0x8 and 0x28001 (the loop) are collision, 0x10 is some
kind of bound or trigger and 0x20 a death plane; the last two have no OnTouch yet. Any other value is ignored.
SADX (the surface flags of a land table COL, see the lines view with the SADX level on): what the bits mean is unknown (only 0x80000000,
drawn, is a hypothesis, see ResourceLoader.cpp), so nothing is registered and all of its collision stays solid. Register a flags value
here once you know it, e.g. addBoundArea(flags, false) for one that is not solid.
*/
void registerBoundAreas()
{
	addBoundArea(0x2, true);
	addBoundArea(0x202, true);
	addBoundArea(0x20001, true);
	addBoundArea(0x8, true);
	addBoundArea(0x28001, true);
	addBoundArea(0x10, false);
	addBoundArea(0x20, false);
}

void enablePhysics(PoolId sonicId_, PoolId shadowId_)
{
	Globals::Physics::gravity = {0.0f, -98.0f, 0.0f};
	getEntity(sonicId_).setPhysics(1.0f);
	getEntity(shadowId_).setPhysics(1.0f);
}

/*
Shadow has no .CCL; his body collision is not stored in a file we have found. Observed (a RAM dump of the running game, from you): one
capsule of radius 5.5 and no length (the game's f3 = 0, f4 = 5.5) with flags 0x302, owned by a CharaColliBody that sits at the player's position.
Made here as Physics::CollisionShape form 6 (a capsule: a = radius, b = half length, see readCCL). Hypothesis: the entity's position is at the
feet (the usual model origin) and the game's body position at the middle (rings sit at that height), so the center is one radius above the position and the
capsule rests on the ground with the feet; if the model floats or sinks, this offset is what to move.
*/
void addShadowShape(PoolId shadowId_)
{
	Physics::CollisionShape shape{};
	shape.form = 6;
	shape.attr = 0x302;
	shape.a = 5.5f;
	shape.b = 0.0f;
	shape.center = glm::vec3(0.0f, shape.a, 0.0f);
	Physics::Collision collision;
	collision.shapes.push_back(shape);
	getEntity(shadowId_).collisionId = Globals::Physics::collisions.add(std::move(collision));
}

/*
Sonic's body sphere. External, from the Ghidra pseudocode of _Main.rel (hypothesis-level): the game's collision table for Sonic (_Main.rel 0x354E10) holds a
box-like body entry (form 7) and a cylinder-like entry (form 1) that the collision system does not use and that are not made here. The game sets the body
entry's size from a per-character record (Sonic: 4) and its center to the task position plus (0, 5.4, 0) every frame (see the notes on FUN_805AA55C and
the record at _Main.rel 0x2C61E8); a sphere (form 0) of that size and center is what moves him.
The game's positions are hypothesized to be at the feet, while the model origin is about 5.3 above them (observed, from you: main.cpp places
Sonic 5.3 above the ground), so the center is moved down by that height into the entity's space (observed: without it the sphere was at the
top of his head), which puts it 0.1 above the entity position. The sphere's bottom is then 1.4 above his feet (5.4 - 4), so resting it on the
ground sinks the model by that much; the game may keep the feet on the ground with a separate probe that is not modeled here.
*/
void addSonicShape(PoolId sonicId_)
{
	constexpr float BodyRadius = 4.0f;
	constexpr float BodyHeight = 5.4f;
	constexpr float ModelOriginHeight = 5.3f;
	Physics::CollisionShape shape{};
	shape.form = 0;
	shape.a = BodyRadius;
	shape.center = glm::vec3(0.0f, BodyHeight - ModelOriginHeight, 0.0f);
	Physics::Collision collision;
	collision.shapes.push_back(shape);
	getEntity(sonicId_).collisionId = Globals::Physics::collisions.add(std::move(collision));
}

/*
Markers for the objects of a Shadow stage placement file. The records' positions are in the game's space; the level is drawn and its collision built
with RenderWareYawDegrees about +Y (hypothesis, see ONEInstance.h), so the same yaw is applied here and the level's position added. If the markers do not
sit on the drawn level (road, buildings), that yaw or the file's axes are what to look at. The color follows the record's type.
*/
void addShadowPlacementMarkers(const char* path_, const glm::vec3& rLevelPosition_)
{
	std::vector<STH2005::Placement> placements = STH2005::readPlacements(Globals::Discs::shadow.readFile(path_));
	glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), glm::radians(Graphics::RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
	for(const STH2005::Placement& rPlacement : placements)
	{
		glm::vec3 position = glm::vec3(yaw * glm::vec4(rPlacement.position, 1.0f)) + rLevelPosition_;
		Graphics::collisionView.markers.push_back(Graphics::CollisionView::Marker{position, rPlacement.type});
	}
}
}
