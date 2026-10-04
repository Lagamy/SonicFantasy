#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "PoolId.h"

namespace Physics
{
/* QuadTreeEntry::triangle value for an entry that stands for a whole collision (its bounds box) and not one triangle. */
constexpr uint32_t WholeCollision = 0xFFFFFFFF;

/*
What the tree stores: the PoolId of a Collision (Globals::Physics::collisions) and the index of one of its triangles. The tree
never holds a Collision or a reference to one (a collision can be removed and its slot reused), so geometry is fetched through the id when needed.
*/
struct QuadTreeEntry
{
	PoolId collisionId;
	uint32_t triangle;
};

/*
A node covers a rectangle of the XZ plane (Y is up, hypothesis: both games stand on XZ). children are indices into
QuadTree::nodes (NoChild = a leaf); only leaves hold entries.
*/
constexpr int32_t NoChild = -1;
struct QuadTreeNode
{
	glm::vec2 min;
	glm::vec2 max;
	std::array<int32_t, 4> children = {NoChild, NoChild, NoChild, NoChild};
	std::vector<QuadTreeEntry> entries;
};

/*
Spatial index over the triangles of Collisions, by their XZ extent. Built in one go with build(): the root covers the
XZ bounds of all the given collisions, a leaf holding more than maxLeafEntries entries splits in four until maxDepth.
A triangle that overlaps several leaves is stored in each. Coordinates are world ones (each collision's mesh moved by its Collision::position). The tree is not updated by itself when a collision is removed or changed: build it again (CollisionSystem::rebuildQuadTree does, for the levels' collisions).
*/
struct QuadTree
{
	std::vector<QuadTreeNode> nodes;
	uint32_t maxDepth = 8;
	uint32_t maxLeafEntries = 16;

	void build(const std::vector<PoolId>& rCollisionIds_);
	void clear();

	/*
	Appends to rOut_ the entries whose XZ box overlaps the box rMin_ / rMax_ (all three axes are given, only X and Z are used),
	each once, in no particular order. This is the XZ box of the triangle, not an exact triangle test.
	*/
	void query(const glm::vec3& rMin_, const glm::vec3& rMax_, std::vector<QuadTreeEntry>& rOut_) const;

private:
	void insert(uint32_t node_, uint32_t depth_, const QuadTreeEntry& rEntry_, const glm::vec2& rMin_, const glm::vec2& rMax_);
	void split(uint32_t node_, uint32_t depth_);
};
}
