#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "CollisionMesh.h"
#include "CollisionShape.h"

namespace Physics
{
/*
A collision resource, stored in Globals::Physics::collisions and found by its PoolId (the holder, a Level or an Entity, keeps the id
and removes it in destroy). mesh is the triangle soup in the space entities and the renderer use (SADX: level space; Shadow: the .BSP's space after the 90 degree
yaw the renderer applies to RenderWare data, see RenderWareYawDegrees, applied by Loading::addONECollision); position is where that space sits in the world (a level's position, set by Level): the world position of a vertex is mesh.vertices[i] + position.
boundsMin / boundsMax are the box around the vertices in world space (position included).
shapes are the object shapes of a game that stores collision as shapes (SADX's collision tables, relative to the holder's position); an entity's collision has them and no mesh.
Spatial structures (QuadTree) store this PoolId, never the Collision itself.
*/
struct Collision
{
	CollisionMesh mesh;
	std::vector<CollisionShape> shapes;
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 boundsMin = glm::vec3(0.0f);
	glm::vec3 boundsMax = glm::vec3(0.0f);

	void computeBounds();

	/* Moves the collision (and its bounds) to a new world position. Spatial structures built from it have to be built again. */
	void setPosition(const glm::vec3& rPosition_);
};
}
