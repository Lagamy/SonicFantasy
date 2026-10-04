#pragma once
#include <array>
#include <glm/glm.hpp>
#include "Camera.h"

namespace Graphics
{
/*
A level piece is unloaded from the GPU once it is further than the render distance (the camera's far plane) times this
factor, so a piece at the edge is not loaded and unloaded every frame. Hypothesis, unverified: 1.5 is a guess.
*/
constexpr float UnloadDistanceFactor = 1.5f;

/* What a level does with a piece this frame (see levelPieceAction). */
enum class LevelPieceAction
{
	Unload,
	Skip,
	Load,
	Draw
};

/*
The streaming rule shared by the levels of both games, with the render distance being Camera::farPlane. A piece is its
bounding sphere, already in world space. A sphere fully beyond the render distance times UnloadDistanceFactor is Unload
(give its GPU buffers back); one beyond the render distance only is Skip (leave it as it is); one that reaches within it is
Load (have its GPU buffers uploaded), or Draw if it is also in the view frustum (rPlanes_ are the camera's frustumPlanes).
*/
inline LevelPieceAction levelPieceAction(const Camera& rCamera_, const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCenter_, float radius_)
{
	float gap = glm::distance(rCenter_, rCamera_.position) - radius_;
	if(gap > rCamera_.farPlane * UnloadDistanceFactor) return LevelPieceAction::Unload;
	if(gap > rCamera_.farPlane) return LevelPieceAction::Skip;
	return sphereInFrustum(rPlanes_, rCenter_, radius_) ? LevelPieceAction::Draw : LevelPieceAction::Load;
}
}
