#pragma once
#include <glm/glm.hpp>
#include "PoolId.h"

namespace Physics
{
/*
The physics state of one entity, stored in Globals::Physics::bodies and found by the entity's physicsId.
weight scales the global gravity (Globals::Physics::gravity) for this entity; velocity is in units per second and
is changed by the physics system (gravity) and the collision system (landing).
What the last move touched, for the gameplay that steers the body (the scripts): reset at the start of every move of the body by the collision system, then set when a solid surface of the level pushed
it out. grounded: a walkable floor (MinFloorNormalY) did, groundNormal is that surface's own normal (not the straight up it is pushed along) and groundSurface the collision behaviour of its triangle
(UninitializedPoolId = none). touchingWall: a steeper surface did, wallNormal is the push direction. The deepest overlap of the last push iteration of the move counts.
*/
struct PhysicsBody
{
	float weight = 1.0f;
	glm::vec3 velocity = glm::vec3(0.0f);
	bool grounded = false;
	glm::vec3 groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
	PoolId groundSurface;
	bool touchingWall = false;
	glm::vec3 wallNormal = glm::vec3(0.0f);
	/*
	Ground snap, set by the gameplay (0 = off): a move that ends in the air with the body not rising is tried once more straight down by this distance, and kept if that touches a walkable floor
	(the body follows the ground over crests and down slopes instead of leaving it). Shadow's ground code does a downward probe of this kind (external, FUN_8009728c of the decompile; the length is
	a hypothesis: 2.37 times a value of the player record that reads 5.0 in a RAM dump).
	*/
	float snapDistance = 0.0f;
	/*
	How the snap finds the ground, per character (set by the gameplay, like snapDistance). snapByRay false = the stepped probe above. true = the game's ground probe (external, FUN_8009728c of the Shadow the Hedgehog decompile; the steps
	and the constants are in CollisionSystem.cpp): a ray along the minus of the previous floor's normal from the body's center, of length 2.37 x snapRadius + the length of this tick's intended move, then the accept tests and a move
	along it, only if the body was on the floor the tick before; snapDistance only switches it on (> 0). snapRadius is the sphere radius the game's record holds (+0x274: 5.0 for Shadow in a RAM dump; 0 = the shape's own radius).
	snapMinNormalY: how close to the previous floor's normal a surface has to be to count as floor (cos 80 degrees = 0.1736, external: the compare constant DAT_805f2a10; it is the dot with that normal, not a y).
	*/
	bool snapByRay = false;
	float snapRadius = 0.0f;
	float snapMinNormalY = 0.1736f;
	/*
	Only with snapByRay. Hypothesis (the game's snap move, see docs/ai/STH2005_COLLISION.md; not verified): if the move of the ray snap is stopped by a wall or ceiling before it reaches a floor, the body is not on the floor after the move
	(grounded stays false), so the gameplay's air branch runs. Set by the gameplay (Shadow's player); the other bodies snap as before.
	*/
	bool snapBlockedByWall = false;
	/*
	How a tick's move uses the velocity, per character (set by the gameplay). false: by the velocity at the end of the tick (after the behaviours and the gravity). true: by the mean of tickStartVelocity and that one, as Shadow the Hedgehog
	moves (observed, tools/live_jumpdash_stick.csv 0.727 to 0.943 s: every height step of the jump equals (old vy + new vy) / 120 within 0.0005, the new vy alone is 0.02 to 0.05 short). Whether SADX does the same is unknown, so Sonic keeps false.
	tickStartVelocity is the velocity at the start of the tick, before the behaviours (GameplaySystem::tick keeps it); a teleport (Entity::setPosition) sets it to zero, so the first move after one starts from rest.
	*/
	bool moveByMeanVelocity = false;
	/* With moveByMeanVelocity, the part of the move into a floor the body stands on is made straight down (PhysicsSystem::update). */
	glm::vec3 tickStartVelocity = glm::vec3(0.0f);
	/*
	The velocity the last move's walkable-floor pushes cancelled (a resting or sliding body has its fall zeroed every tick). tickStartVelocity includes it (GameplaySystem::tick), so the mean of a body that moves by the mean velocity starts from the velocity it
	had before the floor stopped it, not from the cancelled one. Observed (live_slopes 1.40 to 1.52 s): a start vy of 0 every other tick on a 30 degree slope made the port's move 11 % short horizontally on those ticks (the ray snap pulled it back up the slope): the path drift.
	*/
	glm::vec3 floorCancelledVelocity = glm::vec3(0.0f);
};
}
