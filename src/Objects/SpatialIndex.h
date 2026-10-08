#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "PoolId.h"

/*
Where the entities are, for asking "which of them are in this view" or "which are in range" without looking at every one. Each entity brings its own sphere (Entity::bounds: model and collision shapes, kept up to date by
Entity::refreshBounds, which marks the index dirty); the index knows nothing else about what an entity is. Two parts:
- the static ones: put in a bounding volume hierarchy by rebuild (once the level's entities are all made, main.cpp, and again at the start of the next query after anything marked the index dirty). Each entity is a sphere
  (its bounds moved by its position and rotation then); a node is the sphere around its children, so a query throws away whole subtrees outside the view. They are assumed not to move: a static entity that moves is
  where it was at the last rebuild. One that is removed is skipped at query time (its slot is checked), and stays in the tree until the next rebuild.
- the dynamic ones, in a flat list that is simply walked: an entity with a physics, with behaviorNotCulled, or one made after the first rebuild (addDynamic; later rebuilds keep it there). They are few, and a test per entity is cheap.
Until the first rebuild the index is not built, and the renderer draws every entity as it used to.
The same index decides which entities are active (updateActive, once per gameplay tick): those within the draw distance of the camera, or with behaviorNotCulled. Only the active ones have behaviors, physics and collision
run (GameplaySystem, PhysicsSystem, CollisionSystem iterate activeEntities), so the render distance (Camera::farPlane) is the one limit for drawing and simulating.
*/
struct SpatialIndex
{
	/* One static entity: its slot, the generation it had, and its sphere in the world. */
	struct Item
	{
		uint32_t entityIndex;
		uint32_t generation;
		glm::vec3 center;
		float radius;
	};

	/* A node of the hierarchy: the sphere around everything below it; a leaf (left < 0) holds items[first .. first + count), any other node its two children. */
	struct Node
	{
		glm::vec3 center;
		float radius;
		int32_t left;
		int32_t right;
		uint32_t first;
		uint32_t count;
	};

	bool built = false;

	/* Something changed an entity's bounds or physics (Entity::refreshBounds, setPhysics): the next updateActive or rebuildIfDirty rebuilds the hierarchy. */
	bool dirty = false;
	std::vector<Item> items;
	std::vector<Node> nodes;
	std::vector<PoolId> dynamicEntities;

	/* The slots of the entities that are active this tick (Entity::active), set by updateActive. */
	std::vector<uint32_t> activeEntities;

	/* How many static entities the last query kept, for the debug UI. */
	mutable uint32_t lastVisibleStatic = 0;

	/* Sorts every live entity into the static hierarchy or the dynamic list (the first time by its physics and behaviorNotCulled only; later the dynamic list is kept). */
	void rebuild();

	/* Rebuilds if something marked the index dirty and it was built before. */
	void rebuildIfDirty();

	void addDynamic(PoolId entityId_);

	/* Drops the dynamic entities that are gone from the list. */
	void pruneDynamic();

	/*
	Makes the entities within maxDistance_ of the camera (and the dynamic ones with behaviorNotCulled) the active ones: clears Entity::active of the previous ones, fills activeEntities and sets the flag. With cull_ false, or
	the index not built, every live entity is active.
	*/
	void updateActive(bool cull_, const glm::vec3& rCameraPosition_, float maxDistance_);

	/* Appends to rOut_ the slots of the static entities whose sphere is within maxDistance_ of the camera and not outside the frustum (planes as Camera::frustumPlanes), that are still alive. */
	void queryVisible(const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCameraPosition_, float maxDistance_, std::vector<uint32_t>& rOut_) const;

	/* The distance from rPoint_ to the nearest entity's bounding sphere (0 inside one), or maxDistance_ if none is closer; static and dynamic entities both count. Does not rebuild a dirty index. */
	float nearestDistance(const glm::vec3& rPoint_, float maxDistance_) const;

private:
	int32_t buildNode(uint32_t begin_, uint32_t end_);
};
