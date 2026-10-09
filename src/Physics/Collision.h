#pragma once
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include "PoolId.h"
#include "CollisionMesh.h"
#include "CollisionShape.h"

namespace Physics
{
/*
A collision resource, stored in Globals::Physics::collisions and found by its PoolId (the holder, a Level or an Entity, keeps the id
and removes it in destroy). mesh is the triangle soup in the space entities and the renderer use (SADX: level space; Shadow: the .BSP's space after the 90 degree
yaw the renderer applies to RenderWare data, see RenderWareYawDegrees, applied by Loading::addONECollision); position is where that space sits in the world (a level's position, set by Level): the world position of a vertex is mesh.vertices[i] + position.
boundsMin / boundsMax are the box around the vertices in world space (position included).
entityId is the entity that owns the collision (Entity::selfId), UninitializedPoolId for a level's: it is the parent entity an onTouch gets (BABehavior.h).
name is shown by Graphics::Text (the tables or entries the shapes came from, empty if made by hand).
shapes are the object shapes of a game that stores collision as shapes (SADX's collision tables, relative to the holder's position); an entity's collision has them and no mesh.
Spatial structures (QuadTree) store this PoolId, never the Collision itself.
*/
struct Collision
{
	CollisionMesh mesh;
	std::vector<CollisionShape> shapes;
	std::string name;
	PoolId entityId;
	std::unordered_map<uint32_t, PoolId> behaviorByValue;
	PoolId fallbackBehavior;
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 boundsMin = glm::vec3(0.0f);
	glm::vec3 boundsMax = glm::vec3(0.0f);

	/*
	The behaviour of a triangle (Globals::Game::behaviors): that of its raw value (CollisionMesh::triangleValue) in behaviorByValue, which BABehaviors::resolve fills when the collision is loaded from the
	remap of its source (External/Collision/Remaps.json), or fallbackBehavior for a value that is not in it.
	*/
	PoolId behaviorOf(uint32_t triangle_) const
	{
		auto found = this->behaviorByValue.find(this->mesh.triangleValue(triangle_));
		return found != this->behaviorByValue.end() ? found->second : this->fallbackBehavior;
	}

	void computeBounds();

	/* Moves the collision (and its bounds) to a new world position. Spatial structures built from it have to be built again. */
	void setPosition(const glm::vec3& rPosition_);
};
}
