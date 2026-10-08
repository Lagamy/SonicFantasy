#include "SpatialIndex.h"
#include "Camera.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include <algorithm>
#include <unordered_set>

/* Items in a leaf at most (a guess: small enough that a leaf is cheap to test, big enough that the tree stays shallow). */
constexpr uint32_t LeafSize = 6;

/*
Builds the node for items[begin, end) (the array is reordered): a leaf if there are few, otherwise the items are split at the median of their centers along the axis the centers are spread widest on.
The node's sphere is centered in the box of the items' spheres and reaches the farthest edge of any of them.
*/
int32_t SpatialIndex::buildNode(uint32_t begin_, uint32_t end_)
{
	glm::vec3 low(1e30f);
	glm::vec3 high(-1e30f);
	for(uint32_t i = begin_; i < end_; i++)
	{
		low = glm::min(low, this->items[i].center - glm::vec3(this->items[i].radius));
		high = glm::max(high, this->items[i].center + glm::vec3(this->items[i].radius));
	}
	Node node;
	node.center = (low + high) * 0.5f;
	node.radius = 0.0f;
	for(uint32_t i = begin_; i < end_; i++)
	{
		node.radius = std::max(node.radius, glm::distance(node.center, this->items[i].center) + this->items[i].radius);
	}
	node.left = -1;
	node.right = -1;
	node.first = begin_;
	node.count = end_ - begin_;

	int32_t index = static_cast<int32_t>(this->nodes.size());
	this->nodes.push_back(node);
	if(end_ - begin_ <= LeafSize) return index;

	glm::vec3 centerLow(1e30f);
	glm::vec3 centerHigh(-1e30f);
	for(uint32_t i = begin_; i < end_; i++)
	{
		centerLow = glm::min(centerLow, this->items[i].center);
		centerHigh = glm::max(centerHigh, this->items[i].center);
	}
	glm::vec3 spread = centerHigh - centerLow;
	int axis = spread.x >= spread.y && spread.x >= spread.z ? 0 : (spread.y >= spread.z ? 1 : 2);
	uint32_t middle = begin_ + (end_ - begin_) / 2;
	std::nth_element(this->items.begin() + begin_, this->items.begin() + middle, this->items.begin() + end_, [axis](const Item& rA_, const Item& rB_) { return rA_.center[axis] < rB_.center[axis]; });

	int32_t left = this->buildNode(begin_, middle);
	int32_t right = this->buildNode(middle, end_);
	this->nodes[index].left = left;
	this->nodes[index].right = right;
	return index;
}

void SpatialIndex::rebuild()
{
	/* After the first build the entities that were dynamic stay so (an entity made at run time moves by its behaviors even without a physics); the first build starts from the physics and behaviorNotCulled alone. */
	std::unordered_set<uint32_t> kept;
	if(this->built)
	{
		this->pruneDynamic();
		for(PoolId entityId : this->dynamicEntities) kept.insert(entityId.id);
	}
	this->items.clear();
	this->nodes.clear();
	this->dynamicEntities.clear();
	for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
	{
		if(!Globals::Game::entities.alive[i]) continue;
		Entity& rEntity = Globals::Game::entities.objects[i];
		if(kept.count(i) || rEntity.physicsId != UninitializedPoolId || rEntity.behaviorNotCulled)
		{
			this->dynamicEntities.push_back(PoolId{i, Globals::Game::entities.generation[i]});
			continue;
		}
		glm::vec3 center = rEntity.position + glm::vec3(rEntity.rotation * glm::vec4(rEntity.bounds.center, 1.0f));
		this->items.push_back(Item{i, Globals::Game::entities.generation[i], center, rEntity.bounds.radius});
	}
	if(!this->items.empty()) this->buildNode(0, static_cast<uint32_t>(this->items.size()));
	this->built = true;
	this->dirty = false;
}

void SpatialIndex::rebuildIfDirty()
{
	if(this->dirty && this->built) this->rebuild();
}

void SpatialIndex::addDynamic(PoolId entityId_)
{
	this->dynamicEntities.push_back(entityId_);
}

void SpatialIndex::pruneDynamic()
{
	this->dynamicEntities.erase(std::remove_if(this->dynamicEntities.begin(), this->dynamicEntities.end(), [](PoolId entityId_)
	{
		return entityId_.id >= Globals::Game::entities.alive.size() || !Globals::Game::entities.alive[entityId_.id] || Globals::Game::entities.generation[entityId_.id] != entityId_.generation;
	}), this->dynamicEntities.end());
}

void SpatialIndex::queryVisible(const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCameraPosition_, float maxDistance_, std::vector<uint32_t>& rOut_) const
{
	this->lastVisibleStatic = 0;
	if(this->nodes.empty()) return;
	std::vector<int32_t> stack;
	stack.push_back(0);
	while(!stack.empty())
	{
		const Node& rNode = this->nodes[stack.back()];
		stack.pop_back();
		if(glm::distance(rNode.center, rCameraPosition_) - rNode.radius > maxDistance_) continue;
		if(!sphereInFrustum(rPlanes_, rNode.center, rNode.radius)) continue;
		if(rNode.left >= 0)
		{
			stack.push_back(rNode.left);
			stack.push_back(rNode.right);
			continue;
		}
		for(uint32_t i = rNode.first; i < rNode.first + rNode.count; i++)
		{
			const Item& rItem = this->items[i];
			if(!Globals::Game::entities.alive[rItem.entityIndex] || Globals::Game::entities.generation[rItem.entityIndex] != rItem.generation) continue;
			if(glm::distance(rItem.center, rCameraPosition_) - rItem.radius > maxDistance_) continue;
			if(!sphereInFrustum(rPlanes_, rItem.center, rItem.radius)) continue;
			rOut_.push_back(rItem.entityIndex);
			this->lastVisibleStatic++;
		}
	}
}

float SpatialIndex::nearestDistance(const glm::vec3& rPoint_, float maxDistance_) const
{
	float best = maxDistance_;
	std::vector<int32_t> stack;
	if(!this->nodes.empty()) stack.push_back(0);
	while(!stack.empty())
	{
		const Node& rNode = this->nodes[stack.back()];
		stack.pop_back();
		if(glm::distance(rNode.center, rPoint_) - rNode.radius >= best) continue;
		if(rNode.left >= 0)
		{
			stack.push_back(rNode.left);
			stack.push_back(rNode.right);
			continue;
		}
		for(uint32_t i = rNode.first; i < rNode.first + rNode.count; i++)
		{
			const Item& rItem = this->items[i];
			if(!Globals::Game::entities.alive[rItem.entityIndex] || Globals::Game::entities.generation[rItem.entityIndex] != rItem.generation) continue;
			best = std::min(best, std::max(0.0f, glm::distance(rItem.center, rPoint_) - rItem.radius));
		}
	}
	for(PoolId entityId : this->dynamicEntities)
	{
		if(entityId.id >= Globals::Game::entities.alive.size() || !Globals::Game::entities.alive[entityId.id] || Globals::Game::entities.generation[entityId.id] != entityId.generation) continue;
		Entity& rEntity = Globals::Game::entities.objects[entityId.id];
		glm::vec3 center = rEntity.position + glm::mat3(rEntity.rotation) * rEntity.bounds.center;
		best = std::min(best, std::max(0.0f, glm::distance(center, rPoint_) - rEntity.bounds.radius));
	}
	return best;
}

void SpatialIndex::updateActive(bool cull_, const glm::vec3& rCameraPosition_, float maxDistance_)
{
	this->rebuildIfDirty();
	for(uint32_t index : this->activeEntities)
	{
		if(index < Globals::Game::entities.alive.size() && Globals::Game::entities.alive[index]) Globals::Game::entities.objects[index].active = false;
	}
	this->activeEntities.clear();

	if(!cull_ || !this->built)
	{
		for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
		{
			if(Globals::Game::entities.alive[i]) this->activeEntities.push_back(i);
		}
	}
	else
	{
		std::vector<int32_t> stack;
		if(!this->nodes.empty()) stack.push_back(0);
		while(!stack.empty())
		{
			const Node& rNode = this->nodes[stack.back()];
			stack.pop_back();
			if(glm::distance(rNode.center, rCameraPosition_) - rNode.radius > maxDistance_) continue;
			if(rNode.left >= 0)
			{
				stack.push_back(rNode.left);
				stack.push_back(rNode.right);
				continue;
			}
			for(uint32_t i = rNode.first; i < rNode.first + rNode.count; i++)
			{
				const Item& rItem = this->items[i];
				if(!Globals::Game::entities.alive[rItem.entityIndex] || Globals::Game::entities.generation[rItem.entityIndex] != rItem.generation) continue;
				if(glm::distance(rItem.center, rCameraPosition_) - rItem.radius > maxDistance_) continue;
				this->activeEntities.push_back(rItem.entityIndex);
			}
		}
		this->pruneDynamic();
		for(PoolId entityId : this->dynamicEntities)
		{
			Entity& rEntity = Globals::Game::entities.objects[entityId.id];
			if(rEntity.behaviorNotCulled || rEntity.withinRange(rCameraPosition_, maxDistance_)) this->activeEntities.push_back(entityId.id);
		}
	}
	for(uint32_t index : this->activeEntities)
	{
		Globals::Game::entities.objects[index].active = true;
	}
}
