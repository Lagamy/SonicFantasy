#pragma once
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "PoolId.h"
#include "Texture.h"

namespace Graphics
{
/* One node's position, rotation (BAMS, as floats) and scale: what a motion gives at a frame, the rest values filling the channels it doesn't key. */
struct SANodeSample
{
	glm::vec3 position;
	glm::vec3 rotation;
	glm::vec3 scale;
};

float advanceMotionFrame(float frame_, float delta_, uint32_t frameCount_, bool loop_);

/*
Per-entity state of a shared SAMesh, and everything needed to draw it: the shader and the textures.
The instance holds one user of the mesh and of its TextureSet (both released by destroy).
textureSetId is the TextureSet, textureBindingId that set's
TextureBinding for the mesh, giving each submesh's texture; both are UninitializedPoolId for an untextured instance.
currentSAAnimation is the SAMotion it plays (UninitializedPoolId = none; set by Loading::playSAMotion, one user of it),
animationFrame the current frame in it. update advances it; draw then fills nodePose (buildPose, only for an instance that is
drawn, since sampling every node is the costly part): one entry per SAMesh node, same order, overriding its localTransform.
blendRate > 0 means a blend is running (beginBlend): blendFrom is the pose the previous motion had when it was replaced, blendFactor how far the pose has gone from it to the new motion's first frame
(0 to 1, advanced by blendRate a second by update). The new motion is held at its first frame meanwhile and plays from the start when the factor reaches 1.
*/
struct SAInstance
{
	PoolId saMeshId;
	PoolId shaderId;
	PoolId textureSetId;
	PoolId textureBindingId;
	PoolId currentSAAnimation;
	float animationFrame;
	/* Frames a second the motion plays at; 0 = the default (SAMotionFramesPerSecond in SAInstance.cpp), negative = backwards. Set by Entity::update from Entity::animationRate. */
	float framesPerSecond = 0.0f;
	/* false = the motion plays once and holds its last frame (its first going backwards; set by Entity::update from the wanted motion). */
	bool loop = true;
	std::vector<glm::mat4> nodePose;
	std::vector<SANodeSample> blendFrom;
	float blendFactor = 0.0f;
	float blendRate = 0.0f;
	bool poseDirty = false;
	/* A texture drawn on every textured submesh in place of the binding's (an external texture, Entity::setOverrideTexture); nullptr = the binding's. It points into the external pools, which outlive the entities that use it. */
	Texture* pOverrideTexture = nullptr;

	void update(float deltaTime_);
	float playbackRate() const;
	void samplePose(std::vector<SANodeSample>& rOut_) const;
	void buildPose();
	void beginBlend(float rate_);
	void cancelBlend();
	void draw(const glm::mat4& rBase_);
	void destroy();
};
}
