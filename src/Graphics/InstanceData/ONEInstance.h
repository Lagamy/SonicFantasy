#pragma once
#include <cstdint>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "PoolId.h"
#include "STH2005Keys.h"

namespace Graphics
{
/*
Rotation about +Y that turns a RenderWare model from its file's facing to the engine's forward axis (+Z).
Observed: Shadow (SHADOW_BODY.DFF) renders turned 90 degrees; which side he faced is unknown.
Hypothesis, unverified: he faces +X in the file, so 90 degrees brings him to -Z and he faces away from start camera(Like SASonic does).
Applied by the instance, not the importer, so imported data stays as stored in the file.
Derived from Shadow alone and applied to every RenderWare model, a level's worlds too (hypothesis for them).
*/
constexpr float RenderWareYawDegrees = 90.0f;

/*
Per-entity state of a model of an STH2005::ONE archive, and the shader it is drawn with.
archiveId is the archive (Graphics::oneArchives) this instance holds a user of, released by destroy.
meshIndex, textureDictionaryIndex and bindingIndex are indices into that archive's meshes, textureDictionaries and
bindings (STH2005::NoIndex = untextured). They are interchangeable: changeActiveMesh / changeActiveTexture pick another
one by key, loading it from the same archive if needed.
framePose has one entry per frame of the active mesh, same order, overriding its localTransform when animated.
motions are the motions the instance plays (playMotion), at most one per skeleton; update advances them; draw then fills framePose (buildPose, only for an instance that is drawn). A motion that does not loop stops on its last frame.
*/
struct ONEMotion
{
	uint32_t motionIndex;
	float frame;
	uint32_t boneFramesMesh;
	std::vector<int32_t> boneFrames;
	bool attachedPart;
	bool loop;
};

struct ONEInstance
{
	PoolId archiveId;
	PoolId shaderId;
	uint32_t meshIndex;
	uint32_t textureDictionaryIndex;
	uint32_t bindingIndex;
	std::vector<glm::mat4> framePose;
	std::vector<ONEMotion> motions;
	bool poseDirty = false;

	void playMotion(const STH2005::MotionKey& rKey_, bool loop_ = true);
	void update(float deltaTime_);
	void buildPose();
	void draw(const glm::mat4& rBase_);
	void changeActiveMesh(const STH2005::ModelKey& rKey_);
	void switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_);
	void changeActiveTexture(const STH2005::TextureKey& rKey_);
	void destroy();
};
}
