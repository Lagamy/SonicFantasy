#include "QuadTree.h"
#include "Globals.h"
#include <algorithm>
#include <tuple>

namespace Physics
{
static bool overlaps(const glm::vec2& rMinA_, const glm::vec2& rMaxA_, const glm::vec2& rMinB_, const glm::vec2& rMaxB_)
{
	return rMinA_.x <= rMaxB_.x && rMaxA_.x >= rMinB_.x && rMinA_.y <= rMaxB_.y && rMaxA_.y >= rMinB_.y;
}

/* XZ box of an entry, read through its collision's PoolId: one triangle, or the whole collision's bounds. */
static void entryBox(const QuadTreeEntry& rEntry_, glm::vec2& rMin_, glm::vec2& rMax_)
{
	const Collision& rCollision = Globals::Physics::collisions[rEntry_.collisionId];
	if(rEntry_.triangle == WholeCollision)
	{
		rMin_ = glm::vec2(rCollision.boundsMin.x, rCollision.boundsMin.z);
		rMax_ = glm::vec2(rCollision.boundsMax.x, rCollision.boundsMax.z);
		return;
	}
	const CollisionMesh& rMesh = rCollision.mesh;
	glm::vec3 a = rMesh.vertices[rMesh.indices[rEntry_.triangle * 3]] + rCollision.position;
	glm::vec3 b = rMesh.vertices[rMesh.indices[rEntry_.triangle * 3 + 1]] + rCollision.position;
	glm::vec3 c = rMesh.vertices[rMesh.indices[rEntry_.triangle * 3 + 2]] + rCollision.position;
	rMin_ = glm::vec2(std::min({a.x, b.x, c.x}), std::min({a.z, b.z, c.z}));
	rMax_ = glm::vec2(std::max({a.x, b.x, c.x}), std::max({a.z, b.z, c.z}));
}

void QuadTree::clear()
{
	this->nodes.clear();
}

void QuadTree::build(const std::vector<PoolId>& rCollisionIds_)
{
	this->clear();
	if(rCollisionIds_.empty()) return;

	glm::vec2 min(0.0f);
	glm::vec2 max(0.0f);
	bool first = true;
	for(PoolId id : rCollisionIds_)
	{
		const Collision& rCollision = Globals::Physics::collisions[id];
		if(rCollision.mesh.indices.empty()) continue;
		glm::vec2 boundsMin(rCollision.boundsMin.x, rCollision.boundsMin.z);
		glm::vec2 boundsMax(rCollision.boundsMax.x, rCollision.boundsMax.z);
		min = first ? boundsMin : glm::min(min, boundsMin);
		max = first ? boundsMax : glm::max(max, boundsMax);
		first = false;
	}
	if(first) return;

	QuadTreeNode root;
	root.min = min;
	root.max = max;
	this->nodes.push_back(root);

	for(PoolId id : rCollisionIds_)
	{
		const Collision& rCollision = Globals::Physics::collisions[id];
		uint32_t triangleCount = static_cast<uint32_t>(rCollision.mesh.indices.size() / 3);
		for(uint32_t t = 0; t < triangleCount; t++)
		{
			QuadTreeEntry entry{id, t};
			glm::vec2 entryMin;
			glm::vec2 entryMax;
			entryBox(entry, entryMin, entryMax);
			this->insert(0, 0, entry, entryMin, entryMax);
		}
	}
}

/* Stores the entry in every leaf under node_ whose rectangle its box overlaps; a leaf that grows past maxLeafEntries splits. */
void QuadTree::insert(uint32_t node_, uint32_t depth_, const QuadTreeEntry& rEntry_, const glm::vec2& rMin_, const glm::vec2& rMax_)
{
	if(!overlaps(this->nodes[node_].min, this->nodes[node_].max, rMin_, rMax_)) return;

	if(this->nodes[node_].children[0] == NoChild)
	{
		this->nodes[node_].entries.push_back(rEntry_);
		if(this->nodes[node_].entries.size() > this->maxLeafEntries && depth_ < this->maxDepth)
		{
			this->split(node_, depth_);
		}
		return;
	}

	for(int32_t child : std::array<int32_t, 4>(this->nodes[node_].children))
	{
		this->insert(child, depth_ + 1, rEntry_, rMin_, rMax_);
	}
}

/*
Turns a leaf into four children (split at the middle of its rectangle) and moves its entries into them. If every entry
overlaps all four children the split gains nothing and only multiplies the entries; that is accepted, depth is capped by maxDepth.
*/
void QuadTree::split(uint32_t node_, uint32_t depth_)
{
	glm::vec2 min = this->nodes[node_].min;
	glm::vec2 max = this->nodes[node_].max;
	glm::vec2 mid = (min + max) * 0.5f;
	std::vector<QuadTreeEntry> moved = std::move(this->nodes[node_].entries);
	this->nodes[node_].entries.clear();

	const glm::vec2 mins[4] = {min, {mid.x, min.y}, {min.x, mid.y}, mid};
	const glm::vec2 maxs[4] = {mid, {max.x, mid.y}, {mid.x, max.y}, max};
	for(uint32_t i = 0; i < 4; i++)
	{
		QuadTreeNode child;
		child.min = mins[i];
		child.max = maxs[i];
		this->nodes.push_back(child);
		this->nodes[node_].children[i] = static_cast<int32_t>(this->nodes.size() - 1);
	}

	for(const QuadTreeEntry& rEntry : moved)
	{
		glm::vec2 entryMin;
		glm::vec2 entryMax;
		entryBox(rEntry, entryMin, entryMax);
		for(int32_t child : std::array<int32_t, 4>(this->nodes[node_].children))
		{
			this->insert(child, depth_ + 1, rEntry, entryMin, entryMax);
		}
	}
}

void QuadTree::query(const glm::vec3& rMin_, const glm::vec3& rMax_, std::vector<QuadTreeEntry>& rOut_) const
{
	if(this->nodes.empty()) return;

	glm::vec2 min(rMin_.x, rMin_.z);
	glm::vec2 max(rMax_.x, rMax_.z);
	size_t firstNew = rOut_.size();

	std::vector<uint32_t> stack = {0};
	while(!stack.empty())
	{
		const QuadTreeNode& rNode = this->nodes[stack.back()];
		stack.pop_back();
		if(!overlaps(rNode.min, rNode.max, min, max)) continue;

		if(rNode.children[0] != NoChild)
		{
			for(int32_t child : rNode.children) stack.push_back(child);
			continue;
		}
		for(const QuadTreeEntry& rEntry : rNode.entries)
		{
			glm::vec2 entryMin;
			glm::vec2 entryMax;
			entryBox(rEntry, entryMin, entryMax);
			if(overlaps(entryMin, entryMax, min, max)) rOut_.push_back(rEntry);
		}
	}

	/* A triangle in several leaves was found once per leaf: sort what this call added and drop the repeats. */
	auto order = [](const QuadTreeEntry& rA_, const QuadTreeEntry& rB_)
	{
		return std::tie(rA_.collisionId.id, rA_.collisionId.generation, rA_.triangle) < std::tie(rB_.collisionId.id, rB_.collisionId.generation, rB_.triangle);
	};
	std::sort(rOut_.begin() + firstNew, rOut_.end(), order);
	rOut_.erase(std::unique(rOut_.begin() + firstNew, rOut_.end(), [](const QuadTreeEntry& rA_, const QuadTreeEntry& rB_)
	{
		return rA_.collisionId == rB_.collisionId && rA_.triangle == rB_.triangle;
	}), rOut_.end());
}
}
