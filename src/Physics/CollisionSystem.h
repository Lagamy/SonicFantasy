#pragma once

namespace Physics
{
/*
How far above the entity's position the segment starts (a guess, in world units): an entity resting exactly on a surface can
sit a rounding error below it, and a segment starting there would miss the surface and let it fall through.
*/
constexpr float ProbeStartHeight = 1.0f;

/*
Limits of the sphere/capsule movement (guesses): the most steps a move is cut into (a step is at most the smallest shape radius long), and how many
times per step the shapes are pushed out of the triangles they overlap.
*/
constexpr int MaxMoveSteps = 16;
constexpr int MaxPushIterations = 4;

/* The smallest y of a triangle's normal for it to count as a walkable floor (a guess: 0.7 is about 45 degrees from flat). */
constexpr float MinFloorNormalY = 0.7f;

/*
Decides where every entity with a physics (Entity::physicsId) ends up this frame, after PhysicsSystem::update: it checks
the move from position to stagingPosition against the triangles of Globals::Physics::quadTree, writes its decision into
stagingPosition and then sets the entity's position to it (Entity::setPosition), so the renderer draws the new position.
Entities without a physics are not touched.

An entity whose collision (Entity::collisionId) has shapes of form 0 (a sphere) or 6 (a capsule) is moved by them: the shapes are
pushed out of the solid triangles they overlap after each step of the move, the body's velocity slides along the surfaces, and the behaviour of
every triangle a shape overlaps can be touched (BABehavior.h: the triangle's behaviour decides, from its raw value through the collision's remap; a triangle is solid for a shape whose behaviour collides with it). Triangles are two sided, since the winding is unknown.
Other forms (boxes, cylinders) are only drawn.
Any other entity keeps a placeholder: a vertical-ish segment from just above its position to its staging position. The nearest triangle that
segment crosses (either side) stops the entity if a Body collides with it, and the behaviour of every crossed triangle can be touched: staging
becomes the hit point and the body's vertical velocity is zeroed.
*/
struct CollisionSystem
{
	void update();

	/*
	Builds Globals::Physics::quadTree again from the collision of every live level (Level::collisionId). Called by Level whenever
	its collision is set or removed, so the tree always matches the collisions that exist.
	*/
	void rebuildQuadTree();
};
}
