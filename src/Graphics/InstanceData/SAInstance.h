#pragma once
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "PoolId.h"
#include "Texture.h"

namespace Graphics
{
/*
Per-entity state of a shared SAMesh, and everything needed to draw it: the shader and the textures.
The instance holds one user of the mesh and of its TextureSet (both released by destroy).
textureSetId is the TextureSet, textureBindingId that set's
TextureBinding for the mesh, giving each submesh's texture; both are UninitializedPoolId for an untextured instance.
currentSAAnimation is the SAMotion it plays (UninitializedPoolId = none; set by Loading::playSAMotion, one user of it),
animationFrame the current frame in it. update advances it; draw then fills nodePose (buildPose, only for an instance that is
drawn, since sampling every node is the costly part): one entry per SAMesh node, same order, overriding its localTransform.
*/
struct SAInstance
{
	PoolId saMeshId;
	PoolId shaderId;
	PoolId textureSetId;
	PoolId textureBindingId;
	PoolId currentSAAnimation;
	float animationFrame;
	/* Frames a second the motion plays at; 0 = the default (SAMotionFramesPerSecond in SAInstance.cpp). Set by Entity::update from Entity::animationRate. */
	float framesPerSecond = 0.0f;
	std::vector<glm::mat4> nodePose;
	bool poseDirty = false;
	/* A texture drawn on every textured submesh in place of the binding's (an external texture, Entity::setOverrideTexture); nullptr = the binding's. It points into the external pools, which outlive the entities that use it. */
	Texture* pOverrideTexture = nullptr;

	void update(float deltaTime_);
	float playbackRate() const;
	void buildPose();
	void draw(const glm::mat4& rBase_);
	void destroy();
};
}
