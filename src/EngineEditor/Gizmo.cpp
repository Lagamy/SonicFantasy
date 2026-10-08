#include "Gizmo.h"
#include "Globals.h"
#include "API.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Graphics
{
namespace
{
/* Sizes in pixels at the entity's depth (they become world units through Frame::unitsPerPixel); the ring is this fraction of the arrow length. */
constexpr float HandleLengthPixels = 110.0f;
constexpr float PickTolerancePixels = 8.0f;
constexpr float BoxHalfPixels = 6.0f;
constexpr float ArrowHeadPixels = 14.0f;
constexpr float LineThickness = 3.0f;
constexpr float RingFraction = 0.85f;
constexpr int RingSegments = 64;
constexpr float MinScale = 0.01f;

struct Ray
{
	glm::vec3 origin;
	glm::vec3 direction;
};

/* Where the handles are: the entity's drawn position, one unit direction per handle, the arrow length in world units and the world size of a pixel at that depth. */
struct Frame
{
	glm::vec3 origin;
	glm::vec3 axes[3];
	float length;
	float unitsPerPixel;
};

Frame makeFrame(const Camera& rCamera_, const Entity& rEntity_, Gizmo::Mode mode_)
{
	Frame frame;
	frame.origin = rEntity_.renderPosition();

	/* Scale works along the entity's own axes (Entity::scale is applied before the rotation), move and rotate along the world's. */
	for(int i = 0; i < 3; i++)
	{
		frame.axes[i] = glm::vec3(0.0f);
		frame.axes[i][i] = 1.0f;
		if(mode_ == Gizmo::Scale) frame.axes[i] = glm::normalize(glm::vec3(rEntity_.rotation[i]));
	}
	float depth = std::max(glm::dot(frame.origin - rCamera_.position, rCamera_.front), rCamera_.nearPlane);
	frame.unitsPerPixel = 2.0f * depth * std::tan(glm::radians(rCamera_.fov) * 0.5f) / static_cast<float>(std::max(rCamera_.viewportSize().y, 1u));
	frame.length = HandleLengthPixels * frame.unitsPerPixel;
	return frame;
}

/* The ray from the camera through the window position (x_, y_) in pixels. False if the window has no size. */
bool rayAt(GLFWwindow* pWindow_, const Camera& rCamera_, double x_, double y_, Ray& rRay_)
{
	int width = 0;
	int height = 0;
	glfwGetWindowSize(pWindow_, &width, &height);
	if(width <= 0 || height <= 0) return false;
	glm::vec2 ndc(2.0f * static_cast<float>(x_) / static_cast<float>(width) - 1.0f, 1.0f - 2.0f * static_cast<float>(y_) / static_cast<float>(height));
	glm::mat4 inverse = glm::inverse(rCamera_.projection * rCamera_.view);
	glm::vec4 nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f);
	glm::vec4 farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
	nearPoint /= nearPoint.w;
	farPoint /= farPoint.w;
	rRay_.origin = glm::vec3(nearPoint);
	rRay_.direction = glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));
	return true;
}

/* The ray from the camera through the cursor (the cursor is free in the editor, so its position is the mouse's). False if the window has no size. */
bool cursorRay(GLFWwindow* pWindow_, const Camera& rCamera_, Ray& rRay_)
{
	double x = 0.0;
	double y = 0.0;
	glfwGetCursorPos(pWindow_, &x, &y);
	return rayAt(pWindow_, rCamera_, x, y, rRay_);
}

/*
The shortest distance between the ray and the segment from a_ along d_ (a unit vector) for t in [0, length_]; t_ gets the segment's parameter at the closest point. The closest points of two lines are found from the zero of the
derivative of their distance, then t is clamped to the segment and the ray's parameter recomputed for it (and kept on the ray's side of its origin).
*/
float rayToSegment(const Ray& rRay_, const glm::vec3& rA_, const glm::vec3& rD_, float length_, float& rT_)
{
	glm::vec3 w = rA_ - rRay_.origin;
	float b = glm::dot(rD_, rRay_.direction);
	float d1 = glm::dot(rD_, w);
	float e1 = glm::dot(rRay_.direction, w);
	float denominator = 1.0f - b * b;
	float t = denominator > 1e-6f ? (b * e1 - d1) / denominator : 0.0f;
	t = std::clamp(t, 0.0f, length_);
	float s = std::max(e1 + t * b, 0.0f);
	rT_ = t;
	return glm::distance(rA_ + rD_ * t, rRay_.origin + rRay_.direction * s);
}

/* Where along the infinite line (a_, unit d_) the ray passes closest to it. False when the line points almost along the ray (the position along it is then not defined by the cursor). */
bool rayToLine(const Ray& rRay_, const glm::vec3& rA_, const glm::vec3& rD_, float& rT_)
{
	glm::vec3 w = rA_ - rRay_.origin;
	float b = glm::dot(rD_, rRay_.direction);
	float denominator = 1.0f - b * b;
	if(denominator < 0.02f) return false;
	rT_ = (b * glm::dot(rRay_.direction, w) - glm::dot(rD_, w)) / denominator;
	return true;
}

/* The angle of the ray's point on the plane through origin_ with normal axes_[axis_], measured counterclockwise about that axis from axes_[(axis_ + 1) % 3]. False when the plane is almost edge-on or behind the ray. */
bool rayAngle(const Ray& rRay_, const glm::vec3& rOrigin_, const glm::vec3 (&rAxes_)[3], int axis_, float& rAngle_)
{
	const glm::vec3& rNormal = rAxes_[axis_];
	float facing = glm::dot(rRay_.direction, rNormal);
	if(std::abs(facing) < 0.05f) return false;
	float t = glm::dot(rOrigin_ - rRay_.origin, rNormal) / facing;
	if(t <= 0.0f) return false;
	glm::vec3 offset = rRay_.origin + rRay_.direction * t - rOrigin_;
	rAngle_ = std::atan2(glm::dot(offset, rAxes_[(axis_ + 2) % 3]), glm::dot(offset, rAxes_[(axis_ + 1) % 3]));
	return true;
}

/* Whether the ray hits the box of half size half_ around center_ with the (orthonormal) axes of rAxes_ (slab test in the box's axes). */
bool rayHitsBox(const Ray& rRay_, const glm::vec3& rCenter_, const glm::vec3 (&rAxes_)[3], float half_)
{
	float tMin = 0.0f;
	float tMax = 1e30f;
	for(int k = 0; k < 3; k++)
	{
		float o = glm::dot(rRay_.origin - rCenter_, rAxes_[k]);
		float d = glm::dot(rRay_.direction, rAxes_[k]);
		if(std::abs(d) < 1e-8f)
		{
			if(std::abs(o) > half_) return false;
			continue;
		}
		float t1 = (-half_ - o) / d;
		float t2 = (half_ - o) / d;
		if(t1 > t2) std::swap(t1, t2);
		tMin = std::max(tMin, t1);
		tMax = std::min(tMax, t2);
		if(tMin > tMax) return false;
	}
	return true;
}

/* A point of ring axis_ at angle_ (the ring is in the plane of the other two axes, see rayAngle). */
glm::vec3 ringPoint(const Frame& rFrame_, int axis_, float angle_)
{
	return rFrame_.origin + (rFrame_.axes[(axis_ + 1) % 3] * std::cos(angle_) + rFrame_.axes[(axis_ + 2) % 3] * std::sin(angle_)) * (rFrame_.length * RingFraction);
}

/* The handle the ray passes closest to, within PickTolerancePixels (-1 if none). */
int pick(Gizmo::Mode mode_, const Frame& rFrame_, const Ray& rRay_)
{
	float bestDistance = PickTolerancePixels * rFrame_.unitsPerPixel;
	int best = -1;
	for(int i = 0; i < 3; i++)
	{
		float distance = 0.0f;
		float t = 0.0f;
		if(mode_ == Gizmo::Rotate)
		{
			distance = 1e30f;
			glm::vec3 previous = ringPoint(rFrame_, i, 0.0f);
			for(int k = 1; k <= RingSegments; k++)
			{
				glm::vec3 next = ringPoint(rFrame_, i, glm::two_pi<float>() * static_cast<float>(k) / static_cast<float>(RingSegments));
				float segment = glm::distance(previous, next);
				distance = std::min(distance, rayToSegment(rRay_, previous, (next - previous) / segment, segment, t));
				previous = next;
			}
		}
		else
		{
			distance = rayToSegment(rRay_, rFrame_.origin, rFrame_.axes[i], rFrame_.length, t);
			if(mode_ == Gizmo::Scale && rayHitsBox(rRay_, rFrame_.origin + rFrame_.axes[i] * rFrame_.length, rFrame_.axes, (BoxHalfPixels + PickTolerancePixels * 0.5f) * rFrame_.unitsPerPixel)) distance = 0.0f;
		}
		if(distance < bestDistance)
		{
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

/* The screen position (ImGui's, pixels) of a world point; false behind the camera. */
bool project(const glm::mat4& rViewProjection_, const ImVec2& rSize_, const glm::vec3& rPoint_, ImVec2& rOut_)
{
	glm::vec4 clip = rViewProjection_ * glm::vec4(rPoint_, 1.0f);
	if(clip.w <= 1e-4f) return false;
	rOut_ = ImVec2((clip.x / clip.w * 0.5f + 0.5f) * rSize_.x, (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * rSize_.y);
	return true;
}
}

bool Gizmo::wantsMouse() const
{
	return this->dragging;
}

void Gizmo::update(GLFWwindow* pWindow_, const Camera& rCamera_, PoolId selected_, bool uiWantsMouse_, bool cameraDragging_)
{
	bool leftDown = glfwGetMouseButton(pWindow_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	bool pressed = leftDown && !this->leftWasDown;
	this->leftWasDown = leftDown;
	this->hovered = -1;
	Ray ray;
	if(selected_ == UninitializedPoolId || !entityExists(selected_) || !cursorRay(pWindow_, rCamera_, ray))
	{
		this->dragging = false;
		return;
	}
	Entity& rEntity = getEntity(selected_);

	if(this->dragging)
	{
		if(!leftDown)
		{
			this->dragging = false;
			Globals::Game::spatialIndex.dirty = true;
			return;
		}
		switch(this->mode)
		{
		case Move:
		{
			float t = 0.0f;
			if(rayToLine(ray, this->grabOrigin, this->grabDirection, t)) rEntity.setPosition(this->startPosition + this->grabDirection * (t - this->grabValue));
			break;
		}
		case Rotate:
		{
			/* The sweep is wrapped to (-pi, pi] so that crossing the angle's seam does not jump; the turn is about the world axis, in front of the start orientation. */
			float angle = 0.0f;
			glm::vec3 axes[3] = {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
			if(rayAngle(ray, this->grabOrigin, axes, this->dragAxis, angle))
			{
				float delta = std::remainder(angle - this->grabValue, glm::two_pi<float>());
				rEntity.setRotation(glm::rotate(glm::mat4(1.0f), delta, this->grabDirection) * this->startRotation);
			}
			break;
		}
		case Scale:
		{
			/* The factor is the box's distance from the entity now over at the grab, written as a step of the handle length so that a grab near the entity does not divide by about zero. */
			float t = 0.0f;
			if(rayToLine(ray, this->grabOrigin, this->grabDirection, t))
			{
				glm::vec3 scale = this->startScale;
				scale[this->dragAxis] = std::max(this->startScale[this->dragAxis] * (1.0f + (t - this->grabValue) / this->grabLength), MinScale);
				rEntity.setScale(scale);
			}
			break;
		}
		}
		return;
	}

	if(uiWantsMouse_ || cameraDragging_) return;
	Frame frame = makeFrame(rCamera_, rEntity, this->mode);
	this->hovered = pick(this->mode, frame, ray);
	if(!pressed || this->hovered < 0) return;

	/* A handle whose start cannot be read (an arrow pointing at the camera, a ring seen edge on) is not grabbed. */
	float grab = 0.0f;
	bool ok = this->mode == Rotate ? rayAngle(ray, frame.origin, frame.axes, this->hovered, grab) : rayToLine(ray, frame.origin, frame.axes[this->hovered], grab);
	if(!ok) return;
	this->dragging = true;
	this->dragAxis = this->hovered;
	this->hovered = -1;
	this->grabValue = grab;
	this->grabOrigin = frame.origin;
	this->grabDirection = frame.axes[this->dragAxis];
	this->grabLength = frame.length;
	this->startPosition = rEntity.position;
	this->startRotation = rEntity.rotation;
	this->startScale = rEntity.scale;
}

/*
An entity with a model is hit where the ray crosses one of the model's triangles (Entity::rayHitsModel). Every other live entity (no model, or one whose vertices cannot be read) is a sphere: its bounds (model and collision shapes, moved by its position and rotation like the spatial index does),
at least PickMinPixels on the screen so that an entity without a model (a camera, a point) can be clicked. Of everything the ray hits the nearest hit wins.
*/
PoolId pickEntity(GLFWwindow* pWindow_, const Camera& rCamera_, double x_, double y_)
{
	Ray ray;
	if(!rayAt(pWindow_, rCamera_, x_, y_, ray)) return UninitializedPoolId;
	constexpr float PickMinPixels = 12.0f;
	float tanHalf = std::tan(glm::radians(rCamera_.fov) * 0.5f);
	float height = static_cast<float>(std::max(rCamera_.viewportSize().y, 1u));
	PoolId best = UninitializedPoolId;
	float bestDistance = 0.0f;
	for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
	{
		if(!Globals::Game::entities.alive[i]) continue;
		Entity& rEntity = Globals::Game::entities.objects[i];
		float hitDistance = 0.0f;
		bool hasTriangles = false;
		if(rEntity.rayHitsModel(ray.origin, ray.direction, hitDistance, hasTriangles))
		{
			if(best == UninitializedPoolId || hitDistance < bestDistance)
			{
				best = PoolId(i, Globals::Game::entities.generation[i]);
				bestDistance = hitDistance;
			}
			continue;
		}
		if(hasTriangles) continue;
		glm::vec3 center = rEntity.renderPosition();
		if(rEntity.bounds.valid) center += glm::mat3(rEntity.rotation) * rEntity.bounds.center;
		float along = glm::dot(center - ray.origin, ray.direction);
		if(along <= 0.0f) continue;
		float pixelUnits = 2.0f * std::max(glm::dot(center - rCamera_.position, rCamera_.front), rCamera_.nearPlane) * tanHalf / height;
		float radius = std::max(rEntity.bounds.valid ? rEntity.bounds.radius : 0.0f, PickMinPixels * pixelUnits);
		if(glm::distance(center, ray.origin + ray.direction * along) > radius) continue;
		if(best == UninitializedPoolId || along < bestDistance)
		{
			best = PoolId(i, Globals::Game::entities.generation[i]);
			bestDistance = along;
		}
	}
	return best;
}

bool pickPlanePoint(GLFWwindow* pWindow_, const Camera& rCamera_, double x_, double y_, float height_, glm::vec3& rPoint_)
{
	Ray ray;
	if(!rayAt(pWindow_, rCamera_, x_, y_, ray) || std::abs(ray.direction.y) < 1e-4f) return false;
	float t = (height_ - ray.origin.y) / ray.direction.y;
	if(t <= 0.0f || t > rCamera_.farPlane) return false;
	rPoint_ = ray.origin + ray.direction * t;
	return true;
}

void Gizmo::draw(const Camera& rCamera_, PoolId selected_)
{
	if(selected_ == UninitializedPoolId || !entityExists(selected_)) return;
	Frame frame = makeFrame(rCamera_, getEntity(selected_), this->mode);
	ImDrawList* pList = ImGui::GetBackgroundDrawList();
	ImVec2 size = ImGui::GetIO().DisplaySize;
	glm::mat4 viewProjection = rCamera_.projection * rCamera_.view;
	constexpr ImU32 Colors[3] = {IM_COL32(235, 70, 70, 255), IM_COL32(90, 215, 90, 255), IM_COL32(80, 130, 245, 255)};
	constexpr ImU32 Highlight = IM_COL32(255, 220, 60, 255);
	int active = this->dragging ? this->dragAxis : this->hovered;

	ImVec2 origin;
	if(!project(viewProjection, size, frame.origin, origin)) return;
	for(int i = 0; i < 3; i++)
	{
		ImU32 color = i == active ? Highlight : Colors[i];
		if(this->mode == Rotate)
		{
			ImVec2 previous;
			bool havePrevious = project(viewProjection, size, ringPoint(frame, i, 0.0f), previous);
			for(int k = 1; k <= RingSegments; k++)
			{
				ImVec2 next;
				bool haveNext = project(viewProjection, size, ringPoint(frame, i, glm::two_pi<float>() * static_cast<float>(k) / static_cast<float>(RingSegments)), next);
				if(havePrevious && haveNext) pList->AddLine(previous, next, color, LineThickness);
				previous = next;
				havePrevious = haveNext;
			}
			continue;
		}
		ImVec2 end;
		if(!project(viewProjection, size, frame.origin + frame.axes[i] * frame.length, end)) continue;
		pList->AddLine(origin, end, color, LineThickness);
		if(this->mode == Scale)
		{
			pList->AddRectFilled(ImVec2(end.x - BoxHalfPixels, end.y - BoxHalfPixels), ImVec2(end.x + BoxHalfPixels, end.y + BoxHalfPixels), color);
			continue;
		}

		/* The arrow head is a flat triangle on the screen, pointing along the arrow's screen direction; an arrow seen end on (no screen length) has none. */
		ImVec2 along(end.x - origin.x, end.y - origin.y);
		float screenLength = std::sqrt(along.x * along.x + along.y * along.y);
		if(screenLength < 1.0f) continue;
		along = ImVec2(along.x / screenLength, along.y / screenLength);
		ImVec2 across(-along.y, along.x);
		ImVec2 tip(end.x + along.x * ArrowHeadPixels * 0.7f, end.y + along.y * ArrowHeadPixels * 0.7f);
		ImVec2 baseCenter(end.x - along.x * ArrowHeadPixels * 0.3f, end.y - along.y * ArrowHeadPixels * 0.3f);
		float half = ArrowHeadPixels * 0.4f;
		pList->AddTriangleFilled(tip, ImVec2(baseCenter.x + across.x * half, baseCenter.y + across.y * half), ImVec2(baseCenter.x - across.x * half, baseCenter.y - across.y * half), color);
	}
}
}
