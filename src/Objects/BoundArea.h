#pragma once
#include <cstdint>
#include "PoolId.h"

/*
What a triangle attribute of a level collision (CollisionMesh::triangleAttributes, Shadow's raw per-triangle values) does, stored in
Globals::Game::boundAreas. The collision system looks the attribute of every triangle an entity's move crosses up here:
- no BoundArea with that attribute: the triangle is ignored (not solid, no event) in Shadow, solid in SADX (see below);
- isCollision: the triangle is solid, the entity stops on it;
- OnTouch (null = none): called with the PoolId of the entity every frame its move crosses a triangle of this area, once per frame
  however many triangles it crosses, after the entity has been moved. A solid triangle also stops the entity as above, and triangles beyond the
  solid one the entity did not reach do not count. The callback must not hold on to references into the entities pool.
One struct covers solid ground, triggers and death planes, since collision can trigger things too (e.g. camera changes, hypothesis).
For a collision without per-triangle attributes (SADX) the attribute is the surface flags of the triangle's piece; a flags value without an area is solid there (unlike Shadow, where it is ignored), so only the flags registered here change anything.
*/
struct BoundArea
{
	uint32_t attribute;
	bool isCollision = false;
	void (*OnTouch)(PoolId entityId_) = nullptr;
};
