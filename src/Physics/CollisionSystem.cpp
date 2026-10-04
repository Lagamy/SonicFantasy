#include "CollisionSystem.h"
#include "Globals.h"
#include "SAMesh.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Physics
{
/*
The BoundArea of a triangle (see BoundArea.h): the first one whose attribute equals the triangle's value (CollisionMesh::triangleValue: its
own attribute in Shadow, the piece's surface flags in SADX), nullptr if none. Without an area a Shadow triangle is not solid; an SADX
triangle (no per-triangle attributes) is, since what its flags mean is unknown, so SADX collision is all solid until areas are registered
for the flags that are not.
*/
static const BoundArea* boundAreaOf(const CollisionMesh& rMesh_, uint32_t triangle_, bool& rSolid_)
{
	rSolid_ = rMesh_.triangleAttributes.empty();
	uint32_t value = rMesh_.triangleValue(triangle_);
	for(uint32_t i = 0; i < Globals::Game::boundAreas.objects.size(); i++)
	{
		if(!Globals::Game::boundAreas.alive[i] || Globals::Game::boundAreas.objects[i].attribute != value) continue;
		rSolid_ = Globals::Game::boundAreas.objects[i].isCollision;
		return &Globals::Game::boundAreas.objects[i];
	}
	return nullptr;
}

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
rotation the lines view uses) are the ones the collision system moves; the others are only drawn.
*/
struct Capsule
{
	glm::vec3 center;
	glm::vec3 axis;
	float halfLength;
	float radius;
};

static std::vector<Capsule> capsulesOf(const Collision& rCollision_)
{
	std::vector<Capsule> capsules;
	for(const CollisionShape& rShape : rCollision_.shapes)
	{
		if(rShape.form == 0)
		{
			capsules.push_back(Capsule{rShape.center, glm::vec3(0.0f, 0.0f, 1.0f), 0.0f, rShape.a});
		}
		else if(rShape.form == 6)
		{
			glm::mat4 rotation = Graphics::composeNinjaTransform(glm::vec3(0.0f), glm::vec3(rShape.angle), glm::vec3(1.0f), Graphics::NinjaNoPosition | Graphics::NinjaNoScale);
			capsules.push_back(Capsule{rShape.center, glm::vec3(rotation * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)), rShape.b, rShape.a});
		}
	}
	return capsules;
}

/*
Moves an entity with sphere/capsule shapes from position to stagingPosition. The move is cut into steps no longer than the smallest radius (at most
MaxMoveSteps) so a fast body can't skip over a triangle; after each step the shapes are pushed out of the solid triangles they overlap, along the
direction from the triangle's closest point to the shape's axis (the triangle's normal, toward where the shape was, if the axis is on it), the deepest
overlap first, up to MaxPushIterations times. The body's velocity loses its part going into each surface it is pushed away from, so it slides along a
wall, and a walkable floor (MinFloorNormalY) pushes straight up and stops the fall without making the body slide. Every BoundArea with an OnTouch that a shape overlaps (solid or not) is touched once, last.
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
	struct Touched { const BoundArea* pArea; void (*pOnTouch)(PoolId); };
	std::vector<Touched> touched;
	for(int s = 0; s < steps; s++)
	{
		glm::vec3 before = position;
		position += step;
		for(int iteration = 0; iteration < MaxPushIterations; iteration++)
		{
			float deepest = 0.0f;
			glm::vec3 pushDirection(0.0f);
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
					bool solid;
					const BoundArea* pArea = boundAreaOf(rMesh, rEntry.triangle, solid);
					bool touchable = pArea != nullptr && pArea->OnTouch != nullptr;
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

					if(touchable && std::find_if(touched.begin(), touched.end(), [pArea](const Touched& rTouched) { return rTouched.pArea == pArea; }) == touched.end()) touched.push_back(Touched{pArea, pArea->OnTouch});
					if(!solid) continue;

					float depth = rCapsule.radius - distance;
					if(depth <= deepest) continue;
					deepest = depth;
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
			if(pushDirection.y >= MinFloorNormalY)
			{
				deepest /= pushDirection.y;
				pushDirection = glm::vec3(0.0f, 1.0f, 0.0f);
			}

			position += pushDirection * deepest;
			glm::vec3& rVelocity = Globals::Physics::bodies[bodyId].velocity;
			float into = glm::dot(rVelocity, pushDirection);
			if(into < 0.0f) rVelocity -= pushDirection * into;
		}
	}

	rEntity.stagingPosition = position;
	rEntity.setPosition(position);

	/* The callbacks run last: they may add or remove entities or areas, which invalidates rEntity and the area pointers (kept only to tell areas apart). */
	for(const Touched& rTouched : touched)
	{
		rTouched.pOnTouch(entityId);
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

void CollisionSystem::update()
{
	std::vector<QuadTreeEntry> candidates;
	for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
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
		Every triangle the move crosses is looked up in the bound areas: the nearest solid one stops the entity, and every area with an
		OnTouch that is crossed before that point (or anywhere, if nothing solid is hit) is touched, once.
		*/
		struct Touch { float t; const BoundArea* pArea; void (*pOnTouch)(PoolId); };
		std::vector<Touch> touches;
		float nearest = 2.0f;
		for(const QuadTreeEntry& rEntry : candidates)
		{
			const Collision& rCollision = Globals::Physics::collisions[rEntry.collisionId];
			const CollisionMesh& rMesh = rCollision.mesh;
			bool solid;
			const BoundArea* pArea = boundAreaOf(rMesh, rEntry.triangle, solid);
			if(!solid && (pArea == nullptr || pArea->OnTouch == nullptr)) continue;

			float t;
			if(!segmentTriangle(start - rCollision.position, end - rCollision.position, rMesh.vertices[rMesh.indices[rEntry.triangle * 3]], rMesh.vertices[rMesh.indices[rEntry.triangle * 3 + 1]], rMesh.vertices[rMesh.indices[rEntry.triangle * 3 + 2]], t)) continue;
			if(solid && t < nearest) nearest = t;
			if(pArea != nullptr && pArea->OnTouch != nullptr) touches.push_back(Touch{t, pArea, pArea->OnTouch});
		}
		if(nearest <= 1.0f)
		{
			rEntity.stagingPosition = start + (end - start) * nearest;
			Globals::Physics::bodies[rEntity.physicsId].velocity.y = 0.0f;
		}

		rEntity.setPosition(rEntity.stagingPosition);

		/* The callbacks run last: they may add or remove entities or areas, which invalidates rEntity and the area pointers (kept only to tell areas apart). */
		PoolId entityId = PoolId{i, Globals::Game::entities.generation[i]};
		std::vector<const BoundArea*> touched;
		for(const Touch& rTouch : touches)
		{
			if(rTouch.t > nearest || std::find(touched.begin(), touched.end(), rTouch.pArea) != touched.end()) continue;
			touched.push_back(rTouch.pArea);
			rTouch.pOnTouch(entityId);
		}
	}
}
}
