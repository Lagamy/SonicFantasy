#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <utility>
#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "PoolId.h"
#include "STH2005Keys.h"
#include "Texture.h"

namespace Graphics
{
/*
Rotation about +Y that turns a RenderWare model from its file's facing to the engine's forward axis (+Z).
Observed: Shadow (SHADOW_BODY.DFF) renders turned 90 degrees; which side he faced is unknown.
Hypothesis, unverified: he faces +X in the file, so 90 degrees brings him to -Z and he faces away from start camera(Like SASonic does).
Applied by the instance, not the importer, so imported data stays as stored in the file.
Derived from Shadow alone and applied to every RenderWare model, a level's worlds too (hypothesis for them).
(Tried once: taking it out of the instances and keeping it for the level only; it did not fix his facing and was put back, see docs/STH2005_PLAYER_REFERENCE.md 14.)
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

/*
Debug switch (sf.setAnimationScaleMode, a key in Scripts/shadow/player.lua) for what a motion's scale channels do, to find by eye which reading the game has: 0 = the scale and the parent's-scale compensation are applied, with the translation tracks'
parent scale divided out (the hypothesis, see boneLocalTransform), 1 = neither (translation and rotation only), 2 = the scale applied as a plain hierarchy would (the offsets scaled twice: kept to compare).
*/
inline int ONEScaleMode = 0;

/* Debug switch (sf.setAnimationEulerOrder, key J in Scripts/shadow/player.lua): the order the Euler angles of a motion are applied in, 0 Rz*Ry*Rx (the default), 1 Ry*Rz*Rx, 2 Rz*Rx*Ry, 3 Rx*Ry*Rz, 4 Rx*Rz*Ry, 5 Ry*Rx*Rz (ONEInstance.cpp, EulerOrders). */
inline int ONEEulerOrder = 0;

/* One layer of a pose the gameplay asks for (ONEInstance::setPose): a motion, the frame to sample it at and its weight. */
struct PoseRequest
{
	const STH2005::MotionKey* pKey;
	float frame;
	float weight;
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
	/*
	A pose set by the gameplay (setPose): motions of the main skeleton sampled at the frames it gives and mixed by weight, in place of the motions playMotion plays for that skeleton (the hands still play theirs). The weights are absolute: the pose
	is their weighted average (each layer is mixed into the sum so far by its weight over the total so far, on translation, rotation and scale of each frame). The timing, the fades and the pairs are the gameplay's (Scripts/shadow/animation.lua).
	*/
	struct PoseLayer
	{
		uint32_t motionIndex;
		float frame;
		float weight;
	};
	std::vector<PoseLayer> poseLayers;
	std::unordered_map<uint64_t, std::vector<int32_t>> layerBoneFrames;
	uint32_t layerBoneFramesMesh = 0xFFFFFFFF;
	/* The playback speed as a multiple of ONEMotionFramesPerSecond (set by Entity::update from Entity::animationRate). */
	float speed = 1.0f;
	/*
	The cross-fade between two motions: playMotion, replacing a playing motion of the main skeleton, keeps the pose that was shown (blendFrom, one local matrix per frame) and buildPose mixes it into the new
	pose over BlendSeconds (ONEInstance.cpp): blendElapsed is the time since the switch, blending is off at the end of it.
	*/
	std::vector<glm::mat4> blendFrom;
	float blendElapsed = 0.0f;
	bool blending = false;
	/* A texture drawn on every textured submesh in place of the binding's (an external texture, Entity::setOverrideTexture); nullptr = the binding's. It points into the external pools, which outlive the entities that use it. */
	Texture* pOverrideTexture = nullptr;

	void playMotion(const STH2005::MotionKey& rKey_, bool loop_ = true);
	void setPose(const std::vector<PoseRequest>& rLayers_);
	/* A frame of the active mesh by name, for the debug log: its local translation (3), its local rotation as Euler angles in radians in the order Rz * Ry * Rx (3, x y z), its scale (3) and its position in the model's own space (3), from the pose built last. False if there is no such frame. */
	bool boneInfo(const std::string& rName_, float (&rOut_)[12]) const;
	/* The world matrix of the frame of the active mesh named rName_, in the model's own space (no entity transform, no RenderWareYawDegrees turn), from the pose built last (the rest pose if none): the effects attached to a bone use it. False if there is no such frame. */
	bool frameMatrix(const std::string& rName_, glm::mat4& rOut_) const;
	/* The lowest y, in the model's own space, of the skinned vertices as drawn last (rOut_[0]) and in the rest pose (rOut_[1]), for the debug log: where the soles are against the model's origin. */
	void lowestPoint(float (&rOut_)[2]) const;
	void update(float deltaTime_);
	void buildPose();
	void draw(const glm::mat4& rBase_);
	void changeActiveMesh(const STH2005::ModelKey& rKey_);
	void switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_);
	void changeActiveTexture(const STH2005::TextureKey& rKey_);
	void destroy();
};
}
