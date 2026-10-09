#pragma once
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "PoolId.h"
#include "Camera.h"

namespace Graphics
{
/*
Debug view of the collision, toggled with `enabled` (the C key, see Editor::processInputs) and drawn only while the editor is active (Editor::active, the markers too). Drawn by Renderer::runAll for the editor camera after the scene
with the Default shader, and not part of it: it only reads Globals::Physics and Globals::Game.
- Level collision (Level::collisionId): the edges of every triangle, in world space (the collision's position included, as in the collision system).
  With colorByAttribute (the V key), each triangle is colored by its raw value: its attribute when the collision stores them per triangle
  (Shadow's), otherwise the surface flags of its piece (SADX land table COLs). The color is a hash of the value, so equal values share
  one; the colors say nothing about what a value means. `attributes` lists the values with their triangle counts and colors, for the debug UI's legend,
  and with one selected (the arrow keys, see selectAttribute) only its triangles are drawn. Otherwise it is colored by the triangle's slope only, not by any game data:
  floor-like (normal up) green, ceiling-like blue, steep orange. Uploaded again when the set of level collisions, the color mode or the selection changes; only the grid cells within drawDistance and the camera's frustum are drawn. Depth tested.
- Entities with a physics: a yellow line from where the collision system's probe starts down to the position, plus a magenta cross at the position.
  Entities with a collision (Entity::collisionId): its object shapes (SADX tables) and its triangle edges, moved by the entity's position (and, for an SADX entity, turned by its rotation); a shape is colored by its raw attribute value like the level's triangles (colorByAttribute, listed in `entityAttributes`), the triangle edges and all shapes without that mode cyan. Rebuilt every frame and
  drawn without depth test, so they show through the level.
- markers: a cross at each position in world space, colored by its value like the attribute view (equal values share a color), for placed objects that have no model (Level::setONEEntities)
  or for checking where objects are (DebugSetup). Filled by the caller, drawn every frame without depth test, also while `enabled` is off; the debug UI lists the labels with their colors.
*/
struct CollisionView
{
	bool enabled = false;
	bool colorByAttribute = true;
	int selectedAttribute = -1;

	/* Lines of the level collisions and the entities' shapes farther than this from the camera (units) are not drawn; the camera's far plane limits it too. */
	float drawDistance = 2000.0f;

	/* A cross to draw at a world position; value picks its color (see above), label (a string that outlives it, may be nullptr) names what it stands for in the legend. */
	struct Marker
	{
		glm::vec3 position;
		uint32_t value;
		const char* label = nullptr;
	};

	/* The color a value is drawn in (the same for attributes and markers). */
	static glm::vec4 colorOf(uint32_t value_);
	std::vector<Marker> markers;

	/*
	One distinct triangle value (attribute or piece flags) of the shown level collisions: its raw value, how many triangles have it, its line color and the name of the behaviour the collision's remap gives it
	(External/Collision/Remaps.json; "?" if it has none). Most common first.
	*/
	struct AttributeCount
	{
		uint32_t attribute;
		uint32_t count;
		glm::vec4 color;
		std::string behavior = "?";
	};
	std::vector<AttributeCount> attributes;

	/* The same for the object shapes (Physics::CollisionShape::attr, a raw value) that were drawn in the last frame, `count` being the number of shapes, most common first; they are colored like the attributes while colorByAttribute is on (cyan otherwise). */
	std::vector<AttributeCount> entityAttributes;

	/*
	The highlight of one object shape value, like selectedAttribute for the level: while hasSelectedEntityValue (and colorByAttribute) is set only the shapes with selectedEntityValue are drawn.
	The value is kept (not an index) because entityAttributes changes every frame. entityTabActive is set by the debug UI while its Entities sub tab is shown, and the arrow keys then move this
	selection instead of the level's (see main.cpp).
	*/
	bool hasSelectedEntityValue = false;
	uint32_t selectedEntityValue = 0;
	bool entityTabActive = false;

	/* Set by the inspector while its BoundArea tab is shown (like entityTabActive, read by the next frame's draw): the entities' shapes are drawn then even with `enabled` off (the level's lines stay off), the selected entity's in white. */
	bool boundAreaTabActive = false;

	void init();
	void draw(const Camera& rCamera_);

	/* The ImGui panel of the view (switches, draw distance, legends); inside an open tab, in the Debug window and the editor's Level panel. */
	void drawPanel();

	/* Moves the selection along `attributes` by step_ (wrapping), through "none" (-1, nothing highlighted) as well. */
	void selectAttribute(int step_);

	/* The same along entityAttributes, through "none" (nothing highlighted) as well. */
	void selectEntityAttribute(int step_);
	void destroy();

private:
	struct LineVertex
	{
		glm::vec3 position;
		glm::vec4 color;
	};

	GLuint levelVAO = 0;
	GLuint levelVBO = 0;
	GLsizei levelVertexCount = 0;

	/* The level's triangles as faces (three vertices each, same cells as the lines, see LineCell), drawn semi transparent under the lines. */
	GLuint fillVAO = 0;
	GLuint fillVBO = 0;

	/* The level lines are grouped in cells of a grid (a cell's lines are one range of the buffer) so that draw can skip the cells outside the view: its bounding sphere and the lines' range. */
	struct LineCell
	{
		glm::vec3 center;
		float radius;
		GLint first;
		GLsizei count;
		GLint fillFirst;
		GLsizei fillCount;
	};
	std::vector<LineCell> levelCells;
	int shownSelected = -1;
	std::vector<PoolId> shownLevelCollisions;
	bool shownByAttribute = false;
	GLuint dynamicVAO = 0;
	GLuint dynamicVBO = 0;
	GLsizeiptr dynamicCapacity = 0;

	void rebuildLevelLines(const std::vector<PoolId>& rCollisionIds_);
};
}
