#include "DebugSetup.h"
#include "API.h"
#include "Globals.h"
#include "GPU_Globals.h"
#ifdef EDITOR
#include "Editor.h"
#endif
#include "ONEInstance.h"
#include "Placement.h"
#include <glm/ext/matrix_transform.hpp>

namespace DebugSetup
{
void enablePhysics(PoolId sonicId_, PoolId shadowId_)
{
	getEntity(sonicId_).setPhysics(1.0f);
	getEntity(shadowId_).setPhysics(1.0f);
}

#ifdef EDITOR
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
		editor.collisionView.markers.push_back(Graphics::CollisionView::Marker{position, rPlacement.type});
	}
}
#endif
}
