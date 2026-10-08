#include "CollisionSystem.h"
#include "Globals.h"
#include "SAMesh.h"
#include "Editor.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>
#include <glm/gtc/constants.hpp>

namespace Physics
{
/*
Moller-Trumbore segment against triangle, either side. Returns the position of the hit along the segment (0 = start,
1 = end) in rT_; false if the segment misses or is parallel to the triangle.
*/
static bool segmentTriangle(const glm::vec3& rStart_, const glm::vec3& rEnd_, const glm::vec3& rA_, const glm::vec3& rB_, const glm::vec3& rC_, float& rT_)
{
	glm::vec3 direction = rEnd_ - rStart_;
	glm::vec3 edgeAB = rB_ - rA_;
	glm::vec3 edgeAC = rC_ - rA_;
	glm::vec3 p = glm::cross(direction, edgeAC);
	float determinant = glm::dot(edgeAB, p);
	if(glm::abs(determinant) < 1e-8f) return false;

	float inverse = 1.0f / determinant;
	glm::vec3 toStart = rStart_ - rA_;
	float u = glm::dot(toStart, p) * inverse;
	if(u < 0.0f || u > 1.0f) return false;

	glm::vec3 q = glm::cross(toStart, edgeAB);
	float v = glm::dot(direction, q) * inverse;
	if(v < 0.0f || u + v > 1.0f) return false;

	float t = glm::dot(edgeAC, q) * inverse;
	if(t < 0.0f || t > 1.0f) return false;
	rT_ = t;
	return true;
}

/* Closest point of the triangle a, b, c to rP_ (Ericson, Real-Time Collision Detection). */
static glm::vec3 closestPointOnTriangle(const glm::vec3& rP_, const glm::vec3& rA_, const glm::vec3& rB_, const glm::vec3& rC_)
{
	glm::vec3 ab = rB_ - rA_;
	glm::vec3 ac = rC_ - rA_;
	glm::vec3 ap = rP_ - rA_;
	float d1 = glm::dot(ab, ap);
	float d2 = glm::dot(ac, ap);
	if(d1 <= 0.0f && d2 <= 0.0f) return rA_;

	glm::vec3 bp = rP_ - rB_;
	float d3 = glm::dot(ab, bp);
	float d4 = glm::dot(ac, bp);
	if(d3 >= 0.0f && d4 <= d3) return rB_;

	float vc = d1 * d4 - d3 * d2;
	if(vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return rA_ + ab * (d1 / (d1 - d3));

	glm::vec3 cp = rP_ - rC_;
	float d5 = glm::dot(ab, cp);
	float d6 = glm::dot(ac, cp);
	if(d6 >= 0.0f && d5 <= d6) return rC_;

	float vb = d5 * d2 - d1 * d6;
	if(vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return rA_ + ac * (d2 / (d2 - d6));

	float va = d3 * d6 - d5 * d4;
	if(va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) return rB_ + (rC_ - rB_) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

	float denominator = 1.0f / (va + vb + vc);
	return rA_ + ab * (vb * denominator) + ac * (vc * denominator);
}

/* Closest points of the segments p1-q1 and p2-q2 (Ericson); the point on the first is returned in rOn1_, on the second in rOn2_. */
static void closestPointsOfSegments(const glm::vec3& rP1_, const glm::vec3& rQ1_, const glm::vec3& rP2_, const glm::vec3& rQ2_, glm::vec3& rOn1_, glm::vec3& rOn2_)
{
	constexpr float Epsilon = 1e-8f;
	glm::vec3 d1 = rQ1_ - rP1_;
	glm::vec3 d2 = rQ2_ - rP2_;
	glm::vec3 r = rP1_ - rP2_;
	float a = glm::dot(d1, d1);
	float e = glm::dot(d2, d2);
	float f = glm::dot(d2, r);
	float s = 0.0f;
	float t = 0.0f;
	if(a <= Epsilon && e <= Epsilon)
	{
		s = 0.0f;
		t = 0.0f;
	}
	else if(a <= Epsilon)
	{
		t = glm::clamp(f / e, 0.0f, 1.0f);
	}
	else
	{
		float c = glm::dot(d1, r);
		if(e <= Epsilon)
		{
			s = glm::clamp(-c / a, 0.0f, 1.0f);
		}
		else
		{
			float b = glm::dot(d1, d2);
			float denominator = a * e - b * b;
			s = denominator > Epsilon ? glm::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
			t = (b * s + f) / e;
			if(t < 0.0f)
			{
				t = 0.0f;
				s = glm::clamp(-c / a, 0.0f, 1.0f);
			}
			else if(t > 1.0f)
			{
				t = 1.0f;
				s = glm::clamp((b - c) / a, 0.0f, 1.0f);
			}
		}
	}
	rOn1_ = rP1_ + d1 * s;
	rOn2_ = rP2_ + d2 * t;
}

/*
Closest points between the segment p0-p1 (a point if both are equal) and the triangle, either side: one on the segment in rOnSegment_ and one on the
triangle in rOnTriangle_. A segment that crosses the triangle has both at the crossing. Otherwise the closest pair involves an endpoint of the segment
or one of the triangle's edges, so the minimum over those is the answer.
*/
static void closestPointsOfSegmentAndTriangle(const glm::vec3& rP0_, const glm::vec3& rP1_, const glm::vec3& rA_, const glm::vec3& rB_, const glm::vec3& rC_, glm::vec3& rOnSegment_, glm::vec3& rOnTriangle_)
{
	float t;
	if(segmentTriangle(rP0_, rP1_, rA_, rB_, rC_, t))
	{
		rOnSegment_ = rP0_ + (rP1_ - rP0_) * t;
		rOnTriangle_ = rOnSegment_;
		return;
	}

	rOnSegment_ = rP0_;
	rOnTriangle_ = closestPointOnTriangle(rP0_, rA_, rB_, rC_);
	float best = glm::distance(rOnSegment_, rOnTriangle_);
	if(rP0_ == rP1_) return;

	glm::vec3 onTriangle = closestPointOnTriangle(rP1_, rA_, rB_, rC_);
	if(glm::distance(rP1_, onTriangle) < best)
	{
		best = glm::distance(rP1_, onTriangle);
		rOnSegment_ = rP1_;
		rOnTriangle_ = onTriangle;
	}
	const glm::vec3* corners[3] = {&rA_, &rB_, &rC_};
	for(int edge = 0; edge < 3; edge++)
	{
		glm::vec3 onSegment;
		glm::vec3 onEdge;
		closestPointsOfSegments(rP0_, rP1_, *corners[edge], *corners[(edge + 1) % 3], onSegment, onEdge);
		if(glm::distance(onSegment, onEdge) < best)
		{
			best = glm::distance(onSegment, onEdge);
			rOnSegment_ = onSegment;
			rOnTriangle_ = onEdge;
		}
	}
}

/*
A sphere or capsule of an entity's collision, in the entity's space: the segment center +- axis * halfLength swept by radius.
Shape forms 0 (a sphere, radius a) and 6 (a capsule: radius a, half length b along the shape's z axis, turned by its angles with the same
rotation the lines view uses) are the ones the collision system moves; the others are only drawn. A capsule has its shape's behaviour (a Body if the shape has none).
*/
struct Capsule
{
	glm::vec3 center;
	glm::vec3 axis;
	float halfLength;
	float radius;
	PoolId behavior;
};

static std::vector<Capsule> capsulesOf(const Collision& rCollision_)
{
	std::vector<Capsule> capsules;
	for(const CollisionShape& rShape : rCollision_.shapes)
	{
		PoolId behavior = rShape.behavior != UninitializedPoolId ? rShape.behavior : Globals::Game::baBehaviors.body;
		if(rShape.form == 0)
		{
			capsules.push_back(Capsule{rShape.center, glm::vec3(0.0f, 0.0f, 1.0f), 0.0f, rShape.a, behavior});
		}
		else if(rShape.form == 6)
		{
			glm::mat4 rotation = Graphics::composeNinjaTransform(glm::vec3(0.0f), glm::vec3(rShape.angle), glm::vec3(1.0f), Graphics::NinjaNoPosition | Graphics::NinjaNoScale);
			capsules.push_back(Capsule{rShape.center, glm::vec3(rotation * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)), rShape.b, rShape.a, behavior});
		}
	}
	return capsules;
}

/*
One interaction to call: the function of a behaviour's availableInteractions with the entity that owns the behaviour's collision (parent) and the other one (contact), see BABehavior.h.
*/
struct Interaction
{
	BAInteraction function;
	PoolId parent;
	PoolId contact;
};

/* The interactions of an overlap of a collision of rA_ owned by aEntity_ and one of rB_ owned by bEntity_: each side's functions for the other's tags, each once. */
static void collectInteractions(const BABehavior& rA_, PoolId aEntity_, const BABehavior& rB_, PoolId bEntity_, std::vector<Interaction>& rOut_)
{
	auto add = [&](const BAInteraction& rFunction_, PoolId parent_, PoolId contact_)
	{
		for(const Interaction& rInteraction : rOut_)
		{
			if(rInteraction.function == rFunction_ && rInteraction.parent == parent_ && rInteraction.contact == contact_) return;
		}
		rOut_.push_back(Interaction{rFunction_, parent_, contact_});
	};
	rA_.forEachInteraction(rB_, [&](const BAInteraction& rFunction_) { add(rFunction_, aEntity_, bEntity_); });
	rB_.forEachInteraction(rA_, [&](const BAInteraction& rFunction_) { add(rFunction_, bEntity_, aEntity_); });
}

/*
A ray (a start, a unit direction, a length) against the triangles the mover's behaviour is stopped by, two sided (the winding is unknown, hypothesis, like the rest of the system): the nearest hit. rDistance_ is how far along the ray it
is, rNormal_ the triangle's normal turned to face the ray's start, rArea_ its collision behaviour. This is the game's FUN_800979e8 (external: returns the distance of the nearest hit, -1 on a miss, and the hit's position and normal on request).
*/
static bool castRay(const glm::vec3& rStart_, const glm::vec3& rDirection_, float rLength_, PoolId moverBehavior_, float& rDistance_, glm::vec3& rNormal_, PoolId& rArea_)
{
	glm::vec3 end = rStart_ + rDirection_ * rLength_;
	glm::vec3 low = glm::min(rStart_, end) - glm::vec3(0.01f);
	glm::vec3 high = glm::max(rStart_, end) + glm::vec3(0.01f);
	std::vector<QuadTreeEntry> candidates;
	Globals::Physics::quadTree.query(low, high, candidates);

	bool found = false;
	float nearest = rLength_;
	for(const QuadTreeEntry& rEntry : candidates)
	{
		const Collision& rCollision = Globals::Physics::collisions[rEntry.collisionId];
		const CollisionMesh& rMesh = rCollision.mesh;
		PoolId areaId = rCollision.behaviorOf(rEntry.triangle);
		if(areaId == UninitializedPoolId) continue;
		if(!Globals::Game::behaviors[moverBehavior_].collidesWith(Globals::Game::behaviors[areaId])) continue;

		glm::vec3 a = rMesh.vertices[rMesh.indices[rEntry.triangle * 3]];
		glm::vec3 b = rMesh.vertices[rMesh.indices[rEntry.triangle * 3 + 1]];
		glm::vec3 c = rMesh.vertices[rMesh.indices[rEntry.triangle * 3 + 2]];
		glm::vec3 normal = glm::cross(b - a, c - a);
		if(glm::dot(normal, normal) < 1e-12f) continue;
		normal = glm::normalize(normal);

		float t;
		if(!segmentTriangle(rStart_ - rCollision.position, end - rCollision.position, a, b, c, t)) continue;
		float distance = t * rLength_;
		if(distance > nearest) continue;
		nearest = distance;
		rDistance_ = distance;
		rNormal_ = glm::dot(normal, rDirection_) > 0.0f ? -normal : normal;
		rArea_ = areaId;
		found = true;
	}
	return found;
}

/*
Moves an entity with sphere/capsule shapes from position to stagingPosition. The move is cut into steps no longer than the smallest radius (at most
MaxMoveSteps) so a fast body can't skip over a triangle; after each step the shapes are pushed out of the solid triangles they overlap, along the
direction from the triangle's closest point to the shape's axis (the triangle's normal, toward where the shape was, if the axis is on it), the deepest
overlap first, up to MaxPushIterations times. The body's velocity loses its part going into each surface it is pushed away from, so it slides along a
wall, and a walkable floor (MinFloorNormalY) pushes straight up and stops the fall without making the body slide. A triangle is solid for a shape if the shape's behaviour collides with the triangle's (BABehavior::collidesWith); every interaction either side has for the other's tags (BABehavior::availableInteractions) is called once, last, whether the triangle is solid or not.
*/
static void moveCapsules(uint32_t entityIndex_, const std::vector<Capsule>& rCapsules_)
{
	Entity& rEntity = Globals::Game::entities.objects[entityIndex_];
	PoolId entityId = PoolId{entityIndex_, Globals::Game::entities.generation[entityIndex_]};
	PoolId bodyId = rEntity.physicsId;

	glm::vec3 position = rEntity.position;
	glm::vec3 delta = rEntity.stagingPosition - position;
	float smallest = rCapsules_[0].radius;
	for(const Capsule& rCapsule : rCapsules_) smallest = std::min(smallest, rCapsule.radius);
	int steps = smallest > 0.0f ? static_cast<int>(std::ceil(glm::length(delta) / smallest)) : 1;
	steps = std::clamp(steps, 1, MaxMoveSteps);
	glm::vec3 step = delta / static_cast<float>(steps);

	std::vector<QuadTreeEntry> candidates;
	std::vector<Interaction> interactions;

	/* What this move touches is recorded in the body (PhysicsBody::grounded and the others); it starts empty. */
	bool wasGrounded;
	glm::vec3 previousNormal;
	{
		Physics::PhysicsBody& rBody = Globals::Physics::bodies[bodyId];
		wasGrounded = rBody.grounded;
		previousNormal = rBody.grounded ? rBody.groundNormal : glm::vec3(0.0f, 1.0f, 0.0f);
		rBody.grounded = false;
		rBody.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
		rBody.groundSurface = UninitializedPoolId;
		rBody.touchingWall = false;
		rBody.wallNormal = glm::vec3(0.0f);
		rBody.floorCancelledVelocity = glm::vec3(0.0f);
	}
	/* One step of the move: pushes the shapes out of the solid triangles at position (the part of the move after position += step), see the comment of moveCapsules. before is the position the step started from. */
	glm::vec3 before(0.0f);
	auto pushOut = [&]()
	{
		for(int iteration = 0; iteration < MaxPushIterations; iteration++)
		{
			float deepest = 0.0f;
			glm::vec3 pushDirection(0.0f);
			PoolId deepestArea;
			for(const Capsule& rCapsule : rCapsules_)
			{
				glm::vec3 axis = rCapsule.axis * rCapsule.halfLength;
				glm::vec3 center = position + rCapsule.center;
				glm::vec3 low = glm::min(center - axis, center + axis) - glm::vec3(rCapsule.radius);
				glm::vec3 high = glm::max(center - axis, center + axis) + glm::vec3(rCapsule.radius);
				candidates.clear();
				Globals::Physics::quadTree.query(low, high, candidates);

				for(const QuadTreeEntry& rEntry : candidates)
				{
					const Collision& rCollision = Globals::Physics::collisions[rEntry.collisionId];
					const CollisionMesh& rMesh = rCollision.mesh;
					PoolId areaId = rCollision.behaviorOf(rEntry.triangle);
					if(areaId == UninitializedPoolId) continue;
					const BABehavior& rArea = Globals::Game::behaviors[areaId];
					const BABehavior& rMoverBehavior = Globals::Game::behaviors[rCapsule.behavior];
					bool solid = rMoverBehavior.collidesWith(rArea);
					bool touchable = rMoverBehavior.interactsWith(rArea) || rArea.interactsWith(rMoverBehavior);
					if(!solid && !touchable) continue;

					glm::vec3 a = rMesh.vertices[rMesh.indices[rEntry.triangle * 3]];
					glm::vec3 b = rMesh.vertices[rMesh.indices[rEntry.triangle * 3 + 1]];
					glm::vec3 c = rMesh.vertices[rMesh.indices[rEntry.triangle * 3 + 2]];
					glm::vec3 normal = glm::cross(b - a, c - a);
					if(glm::dot(normal, normal) < 1e-12f) continue;

					glm::vec3 offset = rCollision.position;
					glm::vec3 onSegment;
					glm::vec3 onTriangle;
					closestPointsOfSegmentAndTriangle(center - axis - offset, center + axis - offset, a, b, c, onSegment, onTriangle);
					glm::vec3 away = onSegment - onTriangle;
					float distance = glm::length(away);
					if(distance >= rCapsule.radius) continue;

					if(touchable) collectInteractions(rMoverBehavior, entityId, rArea, rCollision.entityId, interactions);
					if(!solid) continue;

					float depth = rCapsule.radius - distance;
					if(depth <= deepest) continue;
					deepest = depth;
					deepestArea = areaId;
					if(distance > 1e-5f)
					{
						pushDirection = away / distance;
					}
					else
					{
						normal = glm::normalize(normal);
						pushDirection = glm::dot(normal, before + rCapsule.center - offset - onTriangle) >= 0.0f ? normal : -normal;
					}
				}
			}
			if(deepest <= 0.0f) break;

			/*
			A walkable floor (normal at most about 45 degrees from up) pushes straight up, by the height that undoes the overlap, and only
			cancels the vertical velocity: pushing along a slope's normal would shift a resting body sideways a little every frame (it
			sinks by gravity, then is pushed out along the tilted normal), and the slope's part of gravity would make it slide.
			*/
			Physics::PhysicsBody& rTouched = Globals::Physics::bodies[bodyId];
			bool floorPush = false;
			if(pushDirection.y >= MinFloorNormalY)
			{
				floorPush = true;
				rTouched.grounded = true;
				rTouched.groundNormal = pushDirection;
				rTouched.groundSurface = deepestArea;
				deepest /= pushDirection.y;
				pushDirection = glm::vec3(0.0f, 1.0f, 0.0f);
			}
			else
			{
				rTouched.touchingWall = true;
				rTouched.wallNormal = pushDirection;
			}

			position += pushDirection * deepest;
			glm::vec3& rVelocity = Globals::Physics::bodies[bodyId].velocity;
			float into = glm::dot(rVelocity, pushDirection);
			if(into < 0.0f)
			{
				rVelocity -= pushDirection * into;
				if(floorPush) rTouched.floorCancelledVelocity += pushDirection * into;
			}
		}
	};
	/*
	The snap (PhysicsBody::snapDistance) is more steps, straight down and no longer than the smallest shape radius each like the move's, after the move: they go on only while the body is not on a walkable floor and not rising, and stop at the
	first step that touches one (the shape then rests on it, pushed out like any step). If none does, the position and the body's state of the move are restored.
	*/
	glm::vec3 positionBeforeSnap(0.0f);
	Physics::PhysicsBody bodyBeforeSnap;
	bool snapped = false;
	float snapDistance = Globals::Physics::bodies[bodyId].snapDistance;
	int snapSteps = snapDistance > 0.0f && !Globals::Physics::bodies[bodyId].snapByRay && smallest > 0.0f ? std::clamp(static_cast<int>(std::ceil(snapDistance / smallest)), 1, MaxMoveSteps) : 0;
	for(int s = 0; s < steps + snapSteps; s++)
	{
		bool snapping = s >= steps;
		if(snapping)
		{
			const Physics::PhysicsBody& rBody = Globals::Physics::bodies[bodyId];
			if(rBody.grounded || rBody.velocity.y > 0.0f) break;
			if(!snapped)
			{
				positionBeforeSnap = position;
				bodyBeforeSnap = rBody;
				snapped = true;
			}
		}
		before = position;
		position += snapping ? glm::vec3(0.0f, -snapDistance / static_cast<float>(snapSteps), 0.0f) : step;
		pushOut();
	}
	if(snapped && !Globals::Physics::bodies[bodyId].grounded)
	{
		position = positionBeforeSnap;
		Globals::Physics::bodies[bodyId] = bodyBeforeSnap;
	}

	/*
	The game's ground probe (PhysicsBody::snapByRay), external, FUN_8009728c, the part after its first sweep (this system's move stands for that sweep): it runs only if the body was on the floor the tick before (the game's flag 2) and
	the move did not end on one. N is the previous floor's normal (the game's +0x1c8; up if there was none). A ray from the body's center along -N, GroundRayRangeFactor x radius plus the length of the intended move long, finds the
	nearest triangle at distance d (none: no ground). d up to GroundRayNearFactor x radius is accepted; a farther one needs a second ray from the point HalfFactor x (d + radius) down along -N, backwards along the move (the game's
	last sweep displacement, the intended move here; at least a radius long), whose nearest hit has a normal within minNormalY of N. If accepted, the body moves along -N towards the hit but stops Backoff before it, or where its sphere
	touches the surface (a sphere of the radius meets a plane when it is radius / cos(angle) from it, my computation: the game sweeps the sphere along that vector with its own collision), and is on the floor if the surface it touches has
	a normal within minNormalY of N, which becomes the floor's normal. Hypotheses: the move's last sweep displacement is the whole intended move; contact is the ray's triangle.
	*/
	{
		Physics::PhysicsBody& rBody = Globals::Physics::bodies[bodyId];
		if(rBody.snapByRay && rBody.snapDistance > 0.0f && wasGrounded && !rBody.grounded)
		{
			float radius = rBody.snapRadius > 0.0f ? rBody.snapRadius : rCapsules_[0].radius;
			glm::vec3 center = position + rCapsules_[0].center;
			glm::vec3 down = -previousNormal;
			float distance;
			glm::vec3 normal;
			PoolId areaId;
			float move = glm::length(delta);
			if(castRay(center, down, GroundRayRangeFactor * radius + move, rCapsules_[0].behavior, distance, normal, areaId))
			{
				bool accepted = distance <= GroundRayNearFactor * radius;
				if(!accepted && move > 1e-6f)
				{
					glm::vec3 back = -delta / move * std::max(move, radius);
					glm::vec3 secondStart = center + down * (GroundRayHalfFactor * (distance + radius));
					float secondDistance;
					glm::vec3 secondNormal;
					PoolId secondArea;
					float backLength = glm::length(back);
					if(castRay(secondStart, back / backLength, backLength, rCapsules_[0].behavior, secondDistance, secondNormal, secondArea) && secondDistance > 0.0f)
					{
						accepted = glm::dot(secondNormal, previousNormal) > rBody.snapMinNormalY;
					}
				}
				float facing = glm::dot(normal, -down);
				if(accepted && facing > 1e-4f && glm::dot(normal, previousNormal) >= rBody.snapMinNormalY)
				{
					float travel = std::max(0.0f, std::min(distance - GroundRayBackoff, distance - rCapsules_[0].radius / facing));

					/*
					PhysicsBody::snapBlockedByWall, hypothesis (the game's snap sweep, decode in STH2005_COLLISION.md, not verified): the move of travel along down is tried in steps no longer than the smallest radius with the move's push-out, only to find out
					whether a wall or ceiling (a push that is not a walkable floor) stops it before a floor does. The trial leaves nothing behind: position, the body and the interactions are restored, and the snap below is done as without the flag. If a wall
					stopped it, the body is not on the floor after the move (grounded stays false, the position is the move's end).
					*/
					bool blocked = false;
					if(rBody.snapBlockedByWall && travel > 1e-6f && smallest > 0.0f)
					{
						glm::vec3 positionBeforeTrial = position;
						Physics::PhysicsBody bodyBeforeTrial = rBody;
						size_t interactionsBeforeTrial = interactions.size();
						int trialSteps = std::clamp(static_cast<int>(std::ceil(travel / smallest)), 1, MaxMoveSteps);
						for(int s = 0; s < trialSteps && !blocked; s++)
						{
							Physics::PhysicsBody& rTrial = Globals::Physics::bodies[bodyId];
							rTrial.grounded = false;
							rTrial.touchingWall = false;
							before = position;
							position += down * (travel / static_cast<float>(trialSteps));
							pushOut();
							blocked = rTrial.touchingWall;
							if(!blocked && rTrial.grounded) break;
						}
						position = positionBeforeTrial;
						rBody = bodyBeforeTrial;
						interactions.resize(interactionsBeforeTrial);
					}
					if(!blocked)
					{
						position += down * travel;
						rBody.grounded = true;
						rBody.groundNormal = normal;
						rBody.groundSurface = areaId;
					}
				}
			}
		}
	}

	rEntity.stagingPosition = position;
	rEntity.moveTo(position);

	/* The callbacks run last: they may add or remove entities or behaviours, which invalidates rEntity (the areas are kept by PoolId). Not in the editor: the push-out above is collision, the interactions are behaviour. */
	for(const Interaction& rInteraction : interactions)
	{
		if(editorActive()) break;
		rInteraction.function.call(rInteraction.parent, rInteraction.contact);
	}
}

/*
A shape of an entity's collision in world space, for the contacts between entities: a capsule (a segment swept by a radius, a sphere has no length) or a box. Reduced the way the lines view draws the
shape (CollisionView.cpp, addShapeLines), so what is drawn is what touches: the entity's rotation applies to SADX entities only, and an SADX capsule (form 6) stands along y while Shadow's (forms 6 and
11) lies along its z axis. Hypotheses (the forms' meanings are not decoded): a cylinder (forms 1, 2: radius a, height b along y; form 11: radius a, half length b along z) is touched as the capsule
inscribed in it, and the boxes (forms 3, 4, 5, 7 with half sizes a, b, c) as boxes. Other forms are not touched.
*/
struct WorldShape
{
	bool isBox;
	glm::vec3 center;
	glm::vec3 axis;
	float halfLength;
	float radius;
	glm::mat3 orientation;
	glm::vec3 halfSizes;
	float boundingRadius;
};

static bool worldShapeOf(const Entity& rEntity_, const CollisionShape& rSourceShape_, WorldShape& rOut_)
{
	bool sadx = rEntity_.sourceEngine == SA_NINJA;
	const CollisionShape rShape_ = scaledShape(rSourceShape_, rEntity_.collisionScale(), sadx);
	glm::mat4 entityRotation = sadx ? rEntity_.rotation : glm::mat4(1.0f);
	glm::mat4 rotation = entityRotation * Graphics::composeNinjaTransform(glm::vec3(0.0f), glm::vec3(rShape_.angle), glm::vec3(1.0f), Graphics::NinjaNoPosition | Graphics::NinjaNoScale);
	if(sadx && rShape_.form == 6) rotation = rotation * glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
	glm::vec3 alongY = glm::vec3(rotation * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
	glm::vec3 alongZ = glm::vec3(rotation * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));

	rOut_.isBox = false;
	rOut_.center = rEntity_.position + glm::vec3(entityRotation * glm::vec4(rShape_.center, 0.0f));
	rOut_.axis = alongZ;
	rOut_.halfLength = 0.0f;
	rOut_.radius = rShape_.a;
	rOut_.orientation = glm::mat3(1.0f);
	rOut_.halfSizes = glm::vec3(0.0f);
	if(rShape_.form == 0)
	{
	}
	else if(rShape_.form == 6)
	{
		rOut_.halfLength = rShape_.b;
	}
	else if(rShape_.form == 1 || rShape_.form == 2)
	{
		rOut_.axis = alongY;
		rOut_.halfLength = std::max(rShape_.b * 0.5f - rShape_.a, 0.0f);
	}
	else if(rShape_.form == 11)
	{
		rOut_.halfLength = std::max(rShape_.b - rShape_.a, 0.0f);
	}
	else if((rShape_.form >= 3 && rShape_.form <= 5) || rShape_.form == 7)
	{
		rOut_.isBox = true;
		rOut_.orientation = glm::mat3(rotation);
		rOut_.halfSizes = glm::vec3(rShape_.a, rShape_.b, rShape_.c);
	}
	else
	{
		return false;
	}
	rOut_.boundingRadius = rOut_.isBox ? glm::length(rOut_.halfSizes) : rOut_.halfLength + rOut_.radius;
	return true;
}

/* The squared distance from a point in a box's own space to the box [-half, half]. */
static float boxDistanceSquared(const glm::vec3& rPoint_, const glm::vec3& rHalf_)
{
	glm::vec3 outside = glm::max(glm::abs(rPoint_) - rHalf_, glm::vec3(0.0f));
	return glm::dot(outside, outside);
}

/*
The point of the capsule's segment closest to the box, in the box's own space (rSegmentPoint_), found by narrowing down along the segment: the distance to a convex box is convex along a line.
*/
static void closestSegmentPointToBox(const WorldShape& rCapsule_, const WorldShape& rBox_, glm::vec3& rSegmentPoint_)
{
	glm::mat3 toLocal = glm::transpose(rBox_.orientation);
	glm::vec3 from = toLocal * (rCapsule_.center - rCapsule_.axis * rCapsule_.halfLength - rBox_.center);
	glm::vec3 to = toLocal * (rCapsule_.center + rCapsule_.axis * rCapsule_.halfLength - rBox_.center);
	float low = 0.0f;
	float high = 1.0f;
	for(int i = 0; i < 24; i++)
	{
		float a = low + (high - low) / 3.0f;
		float b = high - (high - low) / 3.0f;
		if(boxDistanceSquared(from + (to - from) * a, rBox_.halfSizes) < boxDistanceSquared(from + (to - from) * b, rBox_.halfSizes)) high = b;
		else low = a;
	}
	rSegmentPoint_ = from + (to - from) * ((low + high) * 0.5f);
}

static bool boxesOverlap(const WorldShape& rA_, const WorldShape& rB_)
{
	glm::vec3 between = rB_.center - rA_.center;
	auto separated = [&](glm::vec3 axis_)
	{
		if(glm::dot(axis_, axis_) < 1e-8f) return false;
		axis_ = glm::normalize(axis_);
		float reachA = 0.0f;
		float reachB = 0.0f;
		for(int i = 0; i < 3; i++)
		{
			reachA += rA_.halfSizes[i] * glm::abs(glm::dot(rA_.orientation[i], axis_));
			reachB += rB_.halfSizes[i] * glm::abs(glm::dot(rB_.orientation[i], axis_));
		}
		return glm::abs(glm::dot(between, axis_)) > reachA + reachB;
	};
	for(int i = 0; i < 3; i++)
	{
		if(separated(rA_.orientation[i]) || separated(rB_.orientation[i])) return false;
		for(int j = 0; j < 3; j++)
		{
			if(separated(glm::cross(rA_.orientation[i], rB_.orientation[j]))) return false;
		}
	}
	return true;
}

static bool shapesOverlap(const WorldShape& rA_, const WorldShape& rB_)
{
	if(glm::distance(rA_.center, rB_.center) > rA_.boundingRadius + rB_.boundingRadius) return false;
	if(rA_.isBox && rB_.isBox) return boxesOverlap(rA_, rB_);
	if(!rA_.isBox && !rB_.isBox)
	{
		glm::vec3 onA;
		glm::vec3 onB;
		closestPointsOfSegments(rA_.center - rA_.axis * rA_.halfLength, rA_.center + rA_.axis * rA_.halfLength, rB_.center - rB_.axis * rB_.halfLength, rB_.center + rB_.axis * rB_.halfLength, onA, onB);
		return glm::distance(onA, onB) < rA_.radius + rB_.radius;
	}
	const WorldShape& rCapsule = rA_.isBox ? rB_ : rA_;
	const WorldShape& rBox = rA_.isBox ? rA_ : rB_;
	glm::vec3 closest;
	closestSegmentPointToBox(rCapsule, rBox, closest);
	return boxDistanceSquared(closest, rBox.halfSizes) < rCapsule.radius * rCapsule.radius;
}

/*
How far and which way a capsule or sphere (rMover_) has to move to stop overlapping another shape: rPush_ is a unit direction (away from rOther_) and rDepth_ the distance; false if they do not overlap.
Inside a box the way out is along the box axis it is nearest to the surface of.
*/
static bool pushOutOf(const WorldShape& rMover_, const WorldShape& rOther_, glm::vec3& rPush_, float& rDepth_)
{
	if(glm::distance(rMover_.center, rOther_.center) > rMover_.boundingRadius + rOther_.boundingRadius) return false;
	if(!rOther_.isBox)
	{
		glm::vec3 onMover;
		glm::vec3 onOther;
		closestPointsOfSegments(rMover_.center - rMover_.axis * rMover_.halfLength, rMover_.center + rMover_.axis * rMover_.halfLength, rOther_.center - rOther_.axis * rOther_.halfLength, rOther_.center + rOther_.axis * rOther_.halfLength, onMover, onOther);
		float distance = glm::distance(onMover, onOther);
		float reach = rMover_.radius + rOther_.radius;
		if(distance >= reach) return false;
		rPush_ = distance > 1e-5f ? (onMover - onOther) / distance : glm::vec3(0.0f, 1.0f, 0.0f);
		rDepth_ = reach - distance;
		return true;
	}

	glm::vec3 local;
	closestSegmentPointToBox(rMover_, rOther_, local);
	glm::vec3 clamped = glm::clamp(local, -rOther_.halfSizes, rOther_.halfSizes);
	glm::vec3 away = local - clamped;
	float distance = glm::length(away);
	if(distance >= rMover_.radius) return false;
	if(distance > 1e-5f)
	{
		rPush_ = rOther_.orientation * (away / distance);
		rDepth_ = rMover_.radius - distance;
		return true;
	}
	int nearest = 0;
	float nearestGap = 1e30f;
	for(int i = 0; i < 3; i++)
	{
		float gap = rOther_.halfSizes[i] - glm::abs(local[i]);
		if(gap < nearestGap)
		{
			nearestGap = gap;
			nearest = i;
		}
	}
	rPush_ = rOther_.orientation[nearest] * (local[nearest] >= 0.0f ? 1.0f : -1.0f);
	rDepth_ = rMover_.radius + nearestGap;
	return true;
}

/*
The contacts between the shapes of different entities, after every entity has moved. Only shapes whose behaviour has any rule (BABehavior::hasRules) start a test (a pair is looked at once, from its first
active shape), and a pair is overlap-tested only if one side has an interaction for a tag of the other:
- A capsule or sphere shape of an entity with a physics is pushed out of every shape of another entity that its behaviour collides with (BABehavior::collidesWith: the deepest first, up to MaxPushIterations times), the entity
  (all its shapes) moves by the sum and its velocity loses the part going into the shapes. The other entity is not moved (a physics entity is pushed by what it collides with, not by what collides with it).
- Every overlap of two shapes calls the interactions each side has for the other's tags (BABehavior::availableInteractions) with the shape's own entity as the parent and the other shape's entity as the contact
  (Collision::entityId), each once, last (the callbacks may change the pools). A pair with a collision rule but no interaction is not tested (the push-out above has done what it does).
*/
/* True if two bounding spheres do not touch (squared distances, no square root: this runs for every pair). */
static bool spheresApart(const glm::vec3& rCenterA_, float radiusA_, const glm::vec3& rCenterB_, float radiusB_)
{
	glm::vec3 between = rCenterA_ - rCenterB_;
	float reach = radiusA_ + radiusB_;
	return glm::dot(between, between) > reach * reach;
}

/*
Where a shape's bounding sphere is, without building its orientation (the angles of the shape do not move a sphere around its center): the center in the world and the radius, the same as WorldShape's
boundingRadius. False for a form that is not touched. Cheap enough to do for every shape every frame; the full WorldShape is only built for the pairs that get past it.
*/
static bool boundsOf(const Entity& rEntity_, const CollisionShape& rSourceShape_, glm::vec3& rCenter_, float& rRadius_)
{
	const CollisionShape rShape_ = scaledShape(rSourceShape_, rEntity_.collisionScale(), rEntity_.sourceEngine == SA_NINJA);
	if(rShape_.form == 0) rRadius_ = rShape_.a;
	else if(rShape_.form == 6) rRadius_ = rShape_.b + rShape_.a;
	else if(rShape_.form == 1 || rShape_.form == 2) rRadius_ = std::max(rShape_.b * 0.5f - rShape_.a, 0.0f) + rShape_.a;
	else if(rShape_.form == 11) rRadius_ = std::max(rShape_.b - rShape_.a, 0.0f) + rShape_.a;
	else if((rShape_.form >= 3 && rShape_.form <= 5) || rShape_.form == 7) rRadius_ = glm::length(glm::vec3(rShape_.a, rShape_.b, rShape_.c));
	else return false;
	glm::vec3 center = rShape_.center;
	if(rEntity_.sourceEngine == SA_NINJA) center = glm::vec3(rEntity_.rotation * glm::vec4(center, 0.0f));
	rCenter_ = rEntity_.position + center;
	return true;
}

static void updateEntityContacts()
{
	struct ShapeRef
	{
		uint32_t entity;
		PoolId parent;
		PoolId behaviorId;
		const BABehavior* pBehavior;
		const CollisionShape* pShape;
		glm::vec3 center;
		float radius;
		bool active;
		bool built;
		WorldShape shape;
	};
	std::vector<ShapeRef> shapes;
	bool anyActive = false;
	/* Only the active entities have shapes in the contacts (SpatialIndex::updateActive). */
	for(uint32_t i : Globals::Game::spatialIndex.activeEntities)
	{
		if(!Globals::Game::entities.alive[i]) continue;
		const Entity& rEntity = Globals::Game::entities.objects[i];
		if(rEntity.collisionId == UninitializedPoolId) continue;
		const Collision& rCollision = Globals::Physics::collisions[rEntity.collisionId];
		for(const CollisionShape& rShape : rCollision.shapes)
		{
			if(rShape.behavior == UninitializedPoolId) continue;
			ShapeRef ref;
			ref.entity = i;
			ref.parent = rCollision.entityId;
			ref.behaviorId = rShape.behavior;
			ref.pBehavior = &Globals::Game::behaviors[rShape.behavior];
			ref.pShape = &rShape;
			ref.active = ref.pBehavior->hasRules();
			ref.built = false;
			if(!boundsOf(rEntity, rShape, ref.center, ref.radius)) continue;
			anyActive = anyActive || ref.active;
			shapes.push_back(ref);
		}
	}
	if(!anyActive) return;

	/* The full shape of a reference, built the first time it is needed (the entity does not change while this runs, except by the push-outs, which move built shapes along). */
	auto shapeOf = [&](ShapeRef& rRef_) -> const WorldShape&
	{
		if(!rRef_.built)
		{
			worldShapeOf(Globals::Game::entities.objects[rRef_.entity], *rRef_.pShape, rRef_.shape);
			rRef_.built = true;
		}
		return rRef_.shape;
	};

	/*
	A uniform grid of the shapes' bounding spheres (each shape is in every cell its box touches), so a shape is only tested against the shapes of the cells around it instead of against all of them: with a
	few hundred objects with rules and thousands of shapes, testing every pair every tick was what made the frames slow. candidatesOf gives each shape near a reference once (stamps tell the ones already given).
	*/
	constexpr float CellSize = 128.0f;
	std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
	auto cellKey = [](int x_, int y_, int z_) -> uint64_t
	{
		return (static_cast<uint64_t>(x_ & 0x1FFFFF) << 42) | (static_cast<uint64_t>(y_ & 0x1FFFFF) << 21) | static_cast<uint64_t>(z_ & 0x1FFFFF);
	};
	auto forEachCell = [&](const glm::vec3& rCenter_, float radius_, auto&& rVisit_)
	{
		glm::ivec3 low = glm::ivec3(glm::floor((rCenter_ - glm::vec3(radius_)) / CellSize));
		glm::ivec3 high = glm::ivec3(glm::floor((rCenter_ + glm::vec3(radius_)) / CellSize));
		for(int x = low.x; x <= high.x; x++)
		{
			for(int y = low.y; y <= high.y; y++)
			{
				for(int z = low.z; z <= high.z; z++) rVisit_(cellKey(x, y, z));
			}
		}
	};
	for(uint32_t s = 0; s < shapes.size(); s++)
	{
		forEachCell(shapes[s].center, shapes[s].radius, [&](uint64_t key_) { grid[key_].push_back(s); });
	}
	std::vector<uint32_t> stamps(shapes.size(), 0);
	uint32_t stamp = 0;
	auto candidatesOf = [&](const glm::vec3& rCenter_, float radius_, std::vector<uint32_t>& rOut_)
	{
		rOut_.clear();
		stamp++;
		forEachCell(rCenter_, radius_, [&](uint64_t key_)
		{
			auto found = grid.find(key_);
			if(found == grid.end()) return;
			for(uint32_t index : found->second)
			{
				if(stamps[index] == stamp) continue;
				stamps[index] = stamp;
				rOut_.push_back(index);
			}
		});
	};
	std::vector<uint32_t> candidates;

	std::vector<glm::vec3> shifts(Globals::Game::entities.objects.size(), glm::vec3(0.0f));
	for(ShapeRef& rMover : shapes)
	{
		const Entity& rEntity = Globals::Game::entities.objects[rMover.entity];
		if(rEntity.physicsId == UninitializedPoolId || rMover.pBehavior->availableCollisions.empty() || rMover.pShape->form > 6) continue;
		if(shapeOf(rMover).isBox) continue;
		for(int iteration = 0; iteration < MaxPushIterations; iteration++)
		{
			float deepest = 0.0f;
			glm::vec3 direction(0.0f);
			candidatesOf(rMover.center, rMover.radius * 2.0f, candidates);
			for(uint32_t index : candidates)
			{
				ShapeRef& rOther = shapes[index];
				if(rOther.entity == rMover.entity || !rMover.pBehavior->collidesWith(*rOther.pBehavior)) continue;
				if(spheresApart(rMover.center, rMover.radius, rOther.center, rOther.radius)) continue;
				glm::vec3 push;
				float depth;
				if(pushOutOf(rMover.shape, shapeOf(rOther), push, depth) && depth > deepest)
				{
					deepest = depth;
					direction = push;
				}
			}
			if(deepest <= 0.0f) break;
			for(ShapeRef& rShape : shapes)
			{
				if(rShape.entity != rMover.entity) continue;
				rShape.center += direction * deepest;
				if(rShape.built) rShape.shape.center += direction * deepest;
			}
			shifts[rMover.entity] += direction * deepest;
			glm::vec3& rVelocity = Globals::Physics::bodies[rEntity.physicsId].velocity;
			float into = glm::dot(rVelocity, direction);
			if(into < 0.0f) rVelocity -= direction * into;
		}
	}
	for(uint32_t i = 0; i < shifts.size(); i++)
	{
		if(shifts[i] == glm::vec3(0.0f)) continue;
		Entity& rEntity = Globals::Game::entities.objects[i];
		rEntity.moveTo(rEntity.position + shifts[i]);
	}

	std::vector<Interaction> interactions;
	for(size_t i = 0; i < shapes.size(); i++)
	{
		if(!shapes[i].active) continue;
		candidatesOf(shapes[i].center, shapes[i].radius, candidates);
		for(uint32_t j : candidates)
		{
			if(shapes[j].entity == shapes[i].entity || (shapes[j].active && j < i)) continue;
			if(spheresApart(shapes[i].center, shapes[i].radius, shapes[j].center, shapes[j].radius)) continue;
			const BABehavior& rA = *shapes[i].pBehavior;
			const BABehavior& rB = *shapes[j].pBehavior;
			if(!rA.interactsWith(rB) && !rB.interactsWith(rA)) continue;
			if(!shapesOverlap(shapeOf(shapes[i]), shapeOf(shapes[j]))) continue;
			collectInteractions(rA, shapes[i].parent, rB, shapes[j].parent, interactions);
		}
	}
	for(const Interaction& rInteraction : interactions)
	{
		if(editorActive()) break;
		rInteraction.function.call(rInteraction.parent, rInteraction.contact);
	}
}

void CollisionSystem::rebuildQuadTree()
{
	std::vector<PoolId> ids;
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(!Globals::Game::levels.alive[i]) continue;
		PoolId id = Globals::Game::levels.objects[i].collisionId;
		if(id != UninitializedPoolId) ids.push_back(id);
	}
	Globals::Physics::quadTree.build(ids);
}

bool CollisionSystem::groundGap(PoolId entityId_, float maxGap_, float& rGap_) const
{
	const Entity& rEntity = Globals::Game::entities[entityId_];
	if(rEntity.collisionId == UninitializedPoolId) return false;
	std::vector<Capsule> capsules = capsulesOf(Globals::Physics::collisions[rEntity.collisionId]);
	if(capsules.empty()) return false;
	const Capsule& rCapsule = capsules[0];
	glm::vec3 lowest = rCapsule.center - rCapsule.axis * (rCapsule.halfLength * (rCapsule.axis.y >= 0.0f ? 1.0f : -1.0f));
	float distance;
	glm::vec3 normal;
	PoolId areaId;
	if(!castRay(rEntity.position + lowest, glm::vec3(0.0f, -1.0f, 0.0f), rCapsule.radius + maxGap_, rCapsule.behavior, distance, normal, areaId)) return false;
	rGap_ = distance - rCapsule.radius;
	return true;
}

void CollisionSystem::update()
{
	std::vector<QuadTreeEntry> candidates;
	/* Only the active entities move (SpatialIndex::updateActive). */
	for(uint32_t i : Globals::Game::spatialIndex.activeEntities)
	{
		if(!Globals::Game::entities.alive[i]) continue;
		Entity& rEntity = Globals::Game::entities.objects[i];
		if(rEntity.physicsId == UninitializedPoolId) continue;

		/* An entity whose collision has sphere or capsule shapes is moved by them; any other entity keeps the segment probe below. */
		if(rEntity.collisionId != UninitializedPoolId)
		{
			std::vector<Capsule> capsules = capsulesOf(Globals::Physics::collisions[rEntity.collisionId]);
			if(!capsules.empty())
			{
				moveCapsules(i, capsules);
				continue;
			}
		}

		glm::vec3 start = rEntity.position + glm::vec3(0.0f, ProbeStartHeight, 0.0f);
		glm::vec3 end = rEntity.stagingPosition;
		candidates.clear();
		Globals::Physics::quadTree.query(glm::min(start, end), glm::max(start, end), candidates);

		/*
		Every triangle the move crosses has its behaviour looked at: the nearest one a Body collides with stops the entity, and every interaction of a Body or of the behaviour of a triangle crossed before that point
		(or anywhere, if nothing solid is hit) is called, once.
		*/
		const BABehavior& rBody = Globals::Game::behaviors[Globals::Game::baBehaviors.body];
		struct Touch { float t; PoolId area; PoolId parent; };
		std::vector<Touch> touches;
		float nearest = 2.0f;
		for(const QuadTreeEntry& rEntry : candidates)
		{
			const Collision& rCollision = Globals::Physics::collisions[rEntry.collisionId];
			const CollisionMesh& rMesh = rCollision.mesh;
			PoolId areaId = rCollision.behaviorOf(rEntry.triangle);
			if(areaId == UninitializedPoolId) continue;
			const BABehavior& rArea = Globals::Game::behaviors[areaId];
			bool solid = rBody.collidesWith(rArea);
			bool touchable = rBody.interactsWith(rArea) || rArea.interactsWith(rBody);
			if(!solid && !touchable) continue;

			float t;
			if(!segmentTriangle(start - rCollision.position, end - rCollision.position, rMesh.vertices[rMesh.indices[rEntry.triangle * 3]], rMesh.vertices[rMesh.indices[rEntry.triangle * 3 + 1]], rMesh.vertices[rMesh.indices[rEntry.triangle * 3 + 2]], t)) continue;
			if(solid && t < nearest) nearest = t;
			if(touchable) touches.push_back(Touch{t, areaId, rCollision.entityId});
		}
		if(nearest <= 1.0f)
		{
			rEntity.stagingPosition = start + (end - start) * nearest;
			Globals::Physics::bodies[rEntity.physicsId].velocity.y = 0.0f;
		}

		rEntity.moveTo(rEntity.stagingPosition);

		/* The callbacks run last: they may add or remove entities or behaviours, which invalidates rEntity and rBody (the areas are kept by PoolId). */
		PoolId entityId = PoolId{i, Globals::Game::entities.generation[i]};
		std::vector<Interaction> interactions;
		for(const Touch& rTouch : touches)
		{
			if(rTouch.t > nearest) continue;
			collectInteractions(rBody, entityId, Globals::Game::behaviors[rTouch.area], rTouch.parent, interactions);
		}
		for(const Interaction& rInteraction : interactions)
		{
			if(editorActive()) break;
			rInteraction.function.call(rInteraction.parent, rInteraction.contact);
		}
	}

	updateEntityContacts();
}
}
