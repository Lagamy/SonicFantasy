#pragma once

#ifdef EDITOR
#include "Camera.h"
#include "PoolId.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>

namespace Graphics
{
/*
The transform handles of the editor (Editor::gizmo) on the selected entity (EntityListPanel::selected), drawn over the scene and dragged with the left mouse button. Three modes, switched by the keys 1 / 2 / 3 (Editor::processInputs):
- Move: three arrows along the world axes; dragging one moves the entity along that axis (Entity::setPosition), the point under the cursor staying under it.
- Rotate: three rings around the world axes; dragging one turns the entity about that axis (Entity::setRotation), by the angle the cursor sweeps around the entity.
- Scale: three boxes at the ends of the entity's own axes (the columns of its rotation, the axes Entity::scale works along); dragging one scales that axis (Entity::setScale), in proportion to the box's distance from the entity.
The handles keep one size on the screen (a number of pixels at the entity's distance from the camera). They are picked by a ray from the cursor against the handle shapes in the world (a segment for an arrow, a ring as a polyline, a box for a box), the one the ray passes closest to
within a few pixels; the picked one is drawn yellow. Drawing is ImGui's background draw list (like Text), inside DebugUI::draw's frame: no depth test, so the handles show through the scene, and the colors are x red, y green, z blue.
update runs before the editor camera's button hold (Editor::processInputs): a left press on a handle starts a drag that wantsMouse() reports until the button is released, and the camera does not take that press; a press anywhere else is left to the camera and to the UI, and nothing is
deselected by it (the selection changes in the panels and by a click on an entity, pickEntity). A press that began on the camera or on the UI never grabs a handle it is moved over. When a drag ends the spatial index is marked dirty (a moved or turned entity changes its sphere).
Undo (Ctrl+Z) is Editor's: it notices a finished change of the selected entity's transform, from any source. Rotation and scale are kept over Play / Stop (Editor::KeptEntity) and saved with the level (Loading::saveLevelEdits: "transforms", "added"[].rotation / scale).
*/
/* The entity under the window position (x_, y_) in pixels as the camera sees it (UninitializedPoolId if none), for click to select (Editor::processInputs). Entities are picked by the exact box of their model, else by their bounding sphere, see the definition. */
PoolId pickEntity(GLFWwindow* pWindow_, const Camera& rCamera_, double x_, double y_);

/* The level whose bounds box the ray under the window position enters first (UninitializedPoolId if none), for click to select in the Map view when no entity is under the cursor. */
PoolId pickLevel(GLFWwindow* pWindow_, const Camera& rCamera_, double x_, double y_);

/* Where the ray under the window position (x_, y_) crosses the horizontal plane at height_ (world y), within the camera's far plane. False if the ray is level with the plane, points away from it or the window has no size. Used by Editor's paste. */
bool pickPlanePoint(GLFWwindow* pWindow_, const Camera& rCamera_, double x_, double y_, float height_, glm::vec3& rPoint_);

struct Gizmo
{
	enum Mode
	{
		Move,
		Rotate,
		Scale
	};
	Mode mode = Move;

	/* True while a handle is being dragged: the camera must not start a button hold meanwhile. */
	bool wantsMouse() const;

	/*
	Picks the handle under the cursor, starts a drag on a left press on it, and applies / ends the running drag. Nothing without a selected entity. uiWantsMouse_: the cursor is on an ImGui window; cameraDragging_: the editor camera is in a button hold.
	rGroup_ is the selection, the primary one first. One entity: its own handles (Scale along its own axes). Several: one set of handles at the centre of their positions, on the world axes: Move moves them all by the same step, Rotate turns the group about the centre (each entity's
	position goes round the centre and its rotation is turned in front, as for one entity), Scale scales the group about the centre along the world axes (positions from the centre, and each entity's own scale by the same factor, which is exact only for an entity aligned with the world axes).
	*/
	void update(GLFWwindow* pWindow_, const Camera& rCamera_, const std::vector<PoolId>& rGroup_, bool uiWantsMouse_, bool cameraDragging_);

	/* Draws the handles of the mode for the selection (nothing without an existing entity). */
	void draw(const Camera& rCamera_, const std::vector<PoolId>& rGroup_);

	/*
	The same handles on the places of one or more levels in the map (the Map view, Editor::mapView): positions and rotations in degrees (MapLevel), Move and Rotate only (Scale has no handles), the group acting as above (one level: about its own position). updateLevels changes the values
	being dragged and returns true when it did; the caller applies them (Level::setMapTransform) and keeps them (Map::set).
	*/
	bool updateLevels(GLFWwindow* pWindow_, const Camera& rCamera_, std::vector<glm::vec3>& rPositions_, std::vector<glm::vec3>& rRotationsDegrees_, bool uiWantsMouse_, bool cameraDragging_);
	void drawLevels(const Camera& rCamera_, const std::vector<glm::vec3>& rPositions_);

	/* What the handles work on: where they are drawn (origin), and the position, rotation and scale the drag changes. */
	struct Target
	{
		glm::vec3 origin;
		glm::vec3 position;
		glm::mat4 rotation;
		glm::vec3 scale;
	};

private:
	/* The one drag logic of update and updateLevels (picks, starts, applies and ends a drag on rTarget_), and the drawing of the handles of one target. */
	void drive(GLFWwindow* pWindow_, const Camera& rCamera_, Target& rTarget_, bool uiWantsMouse_, bool cameraDragging_);
	void drawTarget(const Camera& rCamera_, const Target& rTarget_);

	/* The group drag of update and updateLevels: the handles are at the centre of the members' positions; while a drag runs the members get their values from the ones they had when it started. True when rMembers_ was changed. */
	bool driveGroup(GLFWwindow* pWindow_, const Camera& rCamera_, std::vector<Target>& rMembers_, bool uiWantsMouse_, bool cameraDragging_);

	/* The members' values at the start of the group drag. */
	std::vector<Target> groupStart;

	/* The handle (axis 0..2) under the cursor, -1 none; the axis being dragged while dragging. */
	int hovered = -1;
	int dragAxis = -1;
	bool dragging = false;

	/* The left button as update last saw it, so that a press counts once. */
	bool leftWasDown = false;

	/*
	What the drag started from: the entity's position, rotation and scale; the handle's origin (where it was drawn), unit direction and length in world units; grabValue is the cursor's start along the axis line (Move, Scale: distance from the origin) or
	its start angle around the axis (Rotate).
	*/
	glm::vec3 startPosition = glm::vec3(0.0f);
	glm::mat4 startRotation = glm::mat4(1.0f);
	glm::vec3 startScale = glm::vec3(1.0f);
	glm::vec3 grabOrigin = glm::vec3(0.0f);
	glm::vec3 grabDirection = glm::vec3(0.0f);
	float grabLength = 1.0f;
	float grabValue = 0.0f;
};
}
#endif
