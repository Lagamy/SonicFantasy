#pragma once
#include "Camera.h"

namespace Graphics
{
/*
Debug labels over the entities: a name drawn with ImGui's text (the background draw list, so it is part of the DebugUI frame: draw is called by DebugUI::draw while its frame is open)
at the screen position of a point above each entity, found with the camera's projection * view. Text drawn in screen space always faces the camera.
Shown while the editor is active (Editor::active, through DebugUI::draw). The shown mode is written at the top of the screen and as the first line of each label, in a color per mode; text is 1.3 times the font size at 100 units and scales with the distance. An entity farther than the collision view's draw distance (and the camera's far plane) or outside the view frustum is skipped, as in CollisionView.
`mode` picks the name, changed with the up and down arrow keys (selectMode, see Editor::processInputs): the entity's name, its mesh's name (SAMesh, or the mesh of its ONE archive) or its collision's name (Physics::Collision::name).
*/
struct Text
{
	enum Mode
	{
		EntityName,
		MeshName,
		CollisionName,
		ModeCount
	};
	Mode mode = EntityName;

	/* Moves the mode by step_ (wrapping). */
	void selectMode(int step_);
	void draw(const Camera& rCamera_);
};
}
