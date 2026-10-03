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
Per-entity state of a model of an STH2005::ONE archive, and the shader it is drawn with.
archiveId is the archive (Graphics::oneArchives) this instance holds a user of, released by destroy.
meshIndex, textureDictionaryIndex and bindingIndex are indices into that archive's meshes, textureDictionaries and
bindings (STH2005::NoIndex = untextured). They are interchangeable: changeActiveMesh / changeActiveTexture pick another
one by key, loading it from the same archive if needed.
framePose has one entry per frame of the active mesh, same order, overriding its localTransform when animated.
motions are the motions the instance plays (playMotion), at most one per skeleton; update advances them and fills framePose.
*/
struct ONEMotion
{
	uint32_t motionIndex;
	float frame;
	uint32_t boneFramesMesh;
	std::vector<int32_t> boneFrames;
	bool attachedPart;
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

	void playMotion(const STH2005::MotionKey& rKey_);
	void update(float deltaTime_);
	void draw(const glm::mat4& rBase_);
	void changeActiveMesh(const STH2005::ModelKey& rKey_);
	void switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_);
	void changeActiveTexture(const STH2005::TextureKey& rKey_);
	void destroy();
};
}
