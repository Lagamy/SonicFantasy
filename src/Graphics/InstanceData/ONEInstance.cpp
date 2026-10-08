#include "ONEInstance.h"
#include "GPU_Globals.h"
#include "RenderStats.h"
#include "GPU_API.h"
#include "ResourceLoader.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
/*
Hypothesis, unverified: Shadow's motions play at 60 frames per second (its game logic rate), at speed 1.
*/
constexpr float ONEMotionFramesPerSecond = 60.0f;

/*
Hypothesis, unverified: the Euler angles of a motion (and of a .BON) are applied Rz * Ry * Rx (X first, as the SA code
does), which reproduces every DFF frame rotation of SH.BON (observed, error below 3e-4). The rest pose cannot tell where Ry goes (y is about 0 there, and z before x is
what it fixes), but the motions use non-zero y, and a large pitch with swing and twist (the skates: the thighs turn 3.3 about x with y of 0.9, the hips 0.6 about y) shows the
order. Graphics::ONEEulerOrder (ONEInstance.h; sf.setAnimationEulerOrder, key J in Scripts/shadow/player.lua) picks one of the six to judge by eye.
*/
static const char* const EulerOrders[6] = {"zyx", "yzx", "zxy", "xyz", "xzy", "yxz"};

/*
Placeholder, not from the game: how long a switch from one motion to another is cross-faded (the game's blend is not decoded). The mix is done on translation, rotation (slerp) and scale of each frame's local matrix.
*/
constexpr float BlendSeconds = 0.1f;

static glm::mat4 mixTransforms(const glm::mat4& rFrom_, const glm::mat4& rTo_, float t_)
{
	auto split = [](const glm::mat4& rMatrix_, glm::vec3& rTranslation_, glm::quat& rRotation_, glm::vec3& rScale_)
	{
		rTranslation_ = glm::vec3(rMatrix_[3]);
		glm::mat3 axes(rMatrix_);
		rScale_ = glm::vec3(glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]));
		for(int i = 0; i < 3; i++)
		{
			if(rScale_[i] > 1e-6f) axes[i] /= rScale_[i];
		}
		rRotation_ = glm::quat_cast(axes);
	};
	glm::vec3 translationA, scaleA, translationB, scaleB;
	glm::quat rotationA, rotationB;
	split(rFrom_, translationA, rotationA, scaleA);
	split(rTo_, translationB, rotationB, scaleB);
	glm::mat4 result = glm::translate(glm::mat4(1.0f), glm::mix(translationA, translationB, t_));
	result *= glm::mat4_cast(glm::slerp(rotationA, rotationB, t_));
	return glm::scale(result, glm::mix(scaleA, scaleB, t_));
}

/*
The value of a track at frame_: cubic Hermite between the keys around it (tangents are the slope per frame, hypothesis),
the first or last value outside them.
*/
static float sampleTrack(const STH2005::MotionTrack& rTrack_, float frame_)
{
	if(frame_ <= rTrack_.front().frame) return rTrack_.front().value;
	if(frame_ >= rTrack_.back().frame) return rTrack_.back().value;

	size_t k = 0;
	while(frame_ >= rTrack_[k + 1].frame) k++;
	const STH2005::Keyframe& rA = rTrack_[k];
	const STH2005::Keyframe& rB = rTrack_[k + 1];
	float span = static_cast<float>(rB.frame - rA.frame);
	float t = (frame_ - rA.frame) / span;
	float t2 = t * t;
	float t3 = t2 * t;
	return (2.0f * t3 - 3.0f * t2 + 1.0f) * rA.value + (t3 - 2.0f * t2 + t) * span * rA.tangent
		+ (-2.0f * t3 + 3.0f * t2) * rB.value + (t3 - t2) * span * rB.inTangent;
}

static glm::vec3 sampleVec3(const STH2005::MotionTrack (&rTracks_)[3], float frame_)
{
	if(rTracks_[0].empty()) return glm::vec3(1.0f);
	return glm::vec3(sampleTrack(rTracks_[0], frame_), sampleTrack(rTracks_[1], frame_), sampleTrack(rTracks_[2], frame_));
}

/*
Plays a motion of the instance's archive (decoded on first use) from its first frame, looping. A motion of the same
skeleton (same first bone name) that is already playing is replaced. The key's archive has to be the instance's.
*/
void ONEInstance::playMotion(const STH2005::MotionKey& rKey_, bool loop_)
{
	if(oneArchives.getName(this->archiveId) != rKey_.archive.path)
	{
		throw std::runtime_error(std::format("ONEInstance: {} is not the archive of this instance ({}).", rKey_.archive.path, oneArchives.getName(this->archiveId)));
	}
	STH2005::ONE& rArchive = oneArchives[this->archiveId];
	uint32_t motionIndex = rArchive.useMotion(rKey_);
	const std::string& rSkeleton = rArchive.motions[motionIndex].bones.front().name;

	ONEMotion motion{motionIndex, 0.0f, STH2005::NoIndex, {}, rKey_.attachedPart, loop_};
	for(ONEMotion& rActive : this->motions)
	{
		if(rArchive.motions[rActive.motionIndex].bones.front().name == rSkeleton)
		{
			if(!rKey_.attachedPart && !this->framePose.empty() && rActive.motionIndex != motionIndex)
			{
				this->blendFrom = this->framePose;
				this->blendElapsed = 0.0f;
				this->blending = true;
			}
			rActive = motion;
			return;
		}
	}
	this->motions.push_back(motion);
}

bool ONEInstance::boneInfo(const std::string& rName_, float (&rOut_)[12]) const
{
	const RenderWareMesh& rMesh = oneArchives[this->archiveId].meshes[this->meshIndex];
	for(size_t f = 0; f < rMesh.frames.size(); f++)
	{
		if(rMesh.frames[f].name != rName_) continue;
		auto local = [&](size_t i) -> const glm::mat4&
		{
			return this->framePose.size() == rMesh.frames.size() ? this->framePose[i] : rMesh.frames[i].localTransform;
		};
		const glm::mat4& rMatrix = local(f);
		glm::mat3 axes(rMatrix);
		glm::vec3 scale(glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]));
		for(int i = 0; i < 3; i++)
		{
			if(scale[i] > 1e-6f) axes[i] /= scale[i];
		}
		rOut_[0] = rMatrix[3].x;
		rOut_[1] = rMatrix[3].y;
		rOut_[2] = rMatrix[3].z;
		rOut_[3] = std::atan2(axes[1][2], axes[2][2]);
		rOut_[4] = std::asin(glm::clamp(-axes[0][2], -1.0f, 1.0f));
		rOut_[5] = std::atan2(axes[0][1], axes[0][0]);
		rOut_[6] = scale.x;
		rOut_[7] = scale.y;
		rOut_[8] = scale.z;
		glm::mat4 world = rMatrix;
		for(int32_t parent = rMesh.frames[f].parent; parent >= 0; parent = rMesh.frames[parent].parent)
		{
			world = local(static_cast<size_t>(parent)) * world;
		}
		rOut_[9] = world[3].x;
		rOut_[10] = world[3].y;
		rOut_[11] = world[3].z;
		return true;
	}
	return false;
}

bool ONEInstance::frameMatrix(const std::string& rName_, glm::mat4& rOut_) const
{
	const RenderWareMesh& rMesh = oneArchives[this->archiveId].meshes[this->meshIndex];
	bool posed = this->framePose.size() == rMesh.frames.size();
	for(size_t f = 0; f < rMesh.frames.size(); f++)
	{
		if(rMesh.frames[f].name != rName_) continue;
		glm::mat4 world = posed ? this->framePose[f] : rMesh.frames[f].localTransform;
		for(int32_t parent = rMesh.frames[f].parent; parent >= 0; parent = rMesh.frames[parent].parent)
		{
			world = (posed ? this->framePose[parent] : rMesh.frames[parent].localTransform) * world;
		}
		rOut_ = world;
		return true;
	}
	return false;
}

/*
The vertices of every atomic carried by the frames' world matrices (a skinned vertex by its weights over world[skin bone frame] * inverse bind matrix, like the shader), the lowest y of them: with the pose built last, and with every frame at its rest transform.
*/
void ONEInstance::lowestPoint(float (&rOut_)[2]) const
{
	const RenderWareMesh& rMesh = oneArchives[this->archiveId].meshes[this->meshIndex];
	for(int pass = 0; pass < 2; pass++)
	{
		bool posed = pass == 0 && this->framePose.size() == rMesh.frames.size();
		std::vector<glm::mat4> world(rMesh.frames.size());
		for(size_t i = 0; i < rMesh.frames.size(); i++)
		{
			const glm::mat4& rLocal = posed ? this->framePose[i] : rMesh.frames[i].localTransform;
			world[i] = rMesh.frames[i].parent < 0 ? rLocal : world[rMesh.frames[i].parent] * rLocal;
		}
		float lowest = 1e30f;
		for(const RenderWareAtomic& rAtomic : rMesh.atomics)
		{
			bool skinned = !rAtomic.skinBoneFrames.empty();
			std::vector<glm::mat4> bones(rAtomic.skinBoneFrames.size());
			for(size_t b = 0; b < bones.size(); b++) bones[b] = world[rAtomic.skinBoneFrames[b]] * rAtomic.inverseBindMatrices[b];
			for(const RenderWareSubmesh& rSubmesh : rAtomic.submeshes)
			{
				for(const Vertex& rVertex : rSubmesh.mesh.verticies)
				{
					glm::vec4 position(rVertex.position, 1.0f);
					glm::vec4 result(0.0f);
					float total = 0.0f;
					if(skinned)
					{
						for(int k = 0; k < 4; k++)
						{
							if(rVertex.boneWeights[k] <= 0.0f) continue;
							result += rVertex.boneWeights[k] * (bones[rVertex.boneIndices[k]] * position);
							total += rVertex.boneWeights[k];
						}
					}
					if(total <= 0.0f) result = world[rAtomic.frame] * position;
					lowest = std::min(lowest, result.y);
				}
			}
		}
		rOut_[pass] = lowest;
	}
}

/*
Sets the pose layers (see PoseLayer): the motions are decoded on first use. An empty list goes back to the motions playMotion plays. The gameplay calls this every tick it wants a pose; the archive of each key has to be the instance's.
*/
void ONEInstance::setPose(const std::vector<PoseRequest>& rLayers_)
{
	STH2005::ONE& rArchive = oneArchives[this->archiveId];
	this->poseLayers.clear();
	for(const PoseRequest& rRequest : rLayers_)
	{
		if(oneArchives.getName(this->archiveId) != rRequest.pKey->archive.path)
		{
			throw std::runtime_error(std::format("ONEInstance: {} is not the archive of this instance ({}).", rRequest.pKey->archive.path, oneArchives.getName(this->archiveId)));
		}
		this->poseLayers.push_back(PoseLayer{rArchive.useMotion(*rRequest.pKey), rRequest.frame, rRequest.weight});
	}
	this->poseDirty = true;
}

/*
update advances every playing motion (looping over its last frame, or stopping on it if it does not loop); buildPose, called by draw, fills framePose: every frame starts at its rest
localTransform, and the frames named like a motion's bones take translate(T) * rotation(R) * scale(S) from it.
The bone names are matched with the active mesh's frame names (from the .BON the model was built with), once per mesh;
a bone without a frame in the mesh is skipped. The first bone of an attachedPart motion is skipped too: it is a bone of
the main skeleton, posed by the main motion (observed: lte_idle holds the identity there, sh_idle the real wrist pose).
Hypothesis, unverified: the scale is the last three channels of a bone.
*/
void ONEInstance::update(float deltaTime_)
{
	if(this->motions.empty()) return;
	STH2005::ONE& rArchive = oneArchives[this->archiveId];
	for(ONEMotion& rActive : this->motions)
	{
		const STH2005::Motion& rMotion = rArchive.motions[rActive.motionIndex];
		if(rMotion.lastFrame > 0)
		{
			float advanced = rActive.frame + deltaTime_ * ONEMotionFramesPerSecond * this->speed;
			rActive.frame = rActive.loop ? std::fmod(advanced, static_cast<float>(rMotion.lastFrame)) : std::min(advanced, static_cast<float>(rMotion.lastFrame));
		}
	}
	if(this->blending)
	{
		this->blendElapsed += deltaTime_;
		if(this->blendElapsed >= BlendSeconds) this->blending = false;
	}
	this->poseDirty = true;
}

/*
A bone of a motion at a frame, in the parts the pose is made of: the offset, the rotation, the inverse of the parent's scale it cancels, and its own scale; local = translate(T) * scale(inverse compensation) * rotation * scale(S).
Layers are mixed part by part (a matrix with that scale in front of the rotation is not a rotation times a scale: pulling it apart to mix two of them gave wrong rotations and scales for the bones that have the compensation, observed: the legs
and feet of the skates in the leg log).
*/
struct BoneSample
{
	glm::vec3 translation;
	glm::quat rotation;
	glm::vec3 inverseCompensation;
	glm::vec3 scale;
};

static BoneSample sampleBone(const STH2005::MotionBone& rBone, float frame_)
{
	glm::vec3 angles = sampleVec3(rBone.rotation, frame_);
	BoneSample sample;
	sample.translation = sampleVec3(rBone.translation, frame_);
	glm::vec3 parentScale = sampleVec3(rBone.compensation, frame_);
	bool compensate = ONEScaleMode == 0 && parentScale.x > 1e-4f && parentScale.y > 1e-4f && parentScale.z > 1e-4f;
	/*
	Observed on SHADOW.MTP (docs/STH2005_PLAYER_REFERENCE.md, 13 and 13a): the three channels before a bone's nine are the scale of its parent (97% of the samples over all motions), and the translation tracks of
	a bone already hold its offset multiplied by that scale (sh_jump_a: the head's y goes from 1.35 to 3.82 = 1.35 x the chest's 2.83; the foot's z from 2.53 to 7.34 = 2.53 x the shin's 2.91). A hierarchy that scales the offsets by the parent's
	scale would count it twice (observed: models stretched without limit), and ignoring the scales leaves the offsets stretched with nothing to stretch the bones (observed: the body stretches less, the skate's feet float off the shins).
	Hypothesis, unverified: the offset is divided by the parent's scale first (the hierarchy then multiplies it back), and the bone cancels the parent's scale in its own orientation and shape (Maya's segment scale compensate):
	local = T / S_parent * S_parent^-1 * R * S. Bones without the channels (the first of a skeleton) are unaffected.
	*/
	sample.inverseCompensation = compensate ? 1.0f / parentScale : glm::vec3(1.0f);
	if(compensate) sample.translation /= parentScale;
	glm::mat4 rotation(1.0f);
	for(const char* pAxis = EulerOrders[ONEEulerOrder % 6]; *pAxis != '\0'; pAxis++)
	{
		glm::vec3 axis(*pAxis == 'x' ? 1.0f : 0.0f, *pAxis == 'y' ? 1.0f : 0.0f, *pAxis == 'z' ? 1.0f : 0.0f);
		rotation = glm::rotate(rotation, *pAxis == 'x' ? angles.x : *pAxis == 'y' ? angles.y : angles.z, axis);
	}
	sample.rotation = glm::quat_cast(rotation);
	sample.scale = ONEScaleMode == 1 ? glm::vec3(1.0f) : sampleVec3(rBone.scale, frame_);
	return sample;
}

static glm::mat4 composeBone(const BoneSample& rSample_)
{
	glm::mat4 transform = glm::translate(glm::mat4(1.0f), rSample_.translation);
	transform = glm::scale(transform, rSample_.inverseCompensation);
	transform *= glm::mat4_cast(rSample_.rotation);
	return glm::scale(transform, rSample_.scale);
}

static BoneSample mixSamples(const BoneSample& rFrom_, const BoneSample& rTo_, float t_)
{
	BoneSample mixed;
	mixed.translation = glm::mix(rFrom_.translation, rTo_.translation, t_);
	mixed.rotation = glm::slerp(rFrom_.rotation, rTo_.rotation, t_);
	mixed.inverseCompensation = glm::mix(rFrom_.inverseCompensation, rTo_.inverseCompensation, t_);
	mixed.scale = glm::mix(rFrom_.scale, rTo_.scale, t_);
	return mixed;
}

static glm::mat4 boneLocalTransform(const STH2005::MotionBone& rBone, float frame_)
{
	return composeBone(sampleBone(rBone, frame_));
}

void ONEInstance::buildPose()
{
	this->poseDirty = false;
	STH2005::ONE& rArchive = oneArchives[this->archiveId];
	RenderWareMesh& rMesh = rArchive.meshes[this->meshIndex];

	this->framePose.resize(rMesh.frames.size());
	for(size_t f = 0; f < rMesh.frames.size(); f++)
	{
		this->framePose[f] = rMesh.frames[f].localTransform;
	}

	for(ONEMotion& rActive : this->motions)
	{
		if(!this->poseLayers.empty() && !rActive.attachedPart) continue;
		const STH2005::Motion& rMotion = rArchive.motions[rActive.motionIndex];
		if(rActive.boneFramesMesh != this->meshIndex)
		{
			rActive.boneFrames.assign(rMotion.bones.size(), -1);
			for(size_t b = 0; b < rMotion.bones.size(); b++)
			{
				for(size_t f = 0; f < rMesh.frames.size(); f++)
				{
					if(rMesh.frames[f].name == rMotion.bones[b].name) rActive.boneFrames[b] = static_cast<int32_t>(f);
				}
			}
			rActive.boneFramesMesh = this->meshIndex;
		}

		for(size_t b = rActive.attachedPart ? 1 : 0; b < rMotion.bones.size(); b++)
		{
			if(rActive.boneFrames[b] < 0) continue;
			const STH2005::MotionBone& rBone = rMotion.bones[b];
			this->framePose[rActive.boneFrames[b]] = boneLocalTransform(rBone, rActive.frame);
		}
	}

	if(!this->poseLayers.empty())
	{
		if(this->layerBoneFramesMesh != this->meshIndex)
		{
			this->layerBoneFrames.clear();
			this->layerBoneFramesMesh = this->meshIndex;
		}
		std::vector<float> total(rMesh.frames.size(), 0.0f);
		std::vector<BoneSample> mixed(rMesh.frames.size());
		for(const PoseLayer& rLayer : this->poseLayers)
		{
			if(rLayer.weight <= 0.0f) continue;
			const STH2005::Motion& rMotion = rArchive.motions[rLayer.motionIndex];
			std::vector<int32_t>& rBoneFrames = this->layerBoneFrames[rLayer.motionIndex];
			if(rBoneFrames.size() != rMotion.bones.size())
			{
				rBoneFrames.assign(rMotion.bones.size(), -1);
				for(size_t b = 0; b < rMotion.bones.size(); b++)
				{
					for(size_t f = 0; f < rMesh.frames.size(); f++)
					{
						if(rMesh.frames[f].name == rMotion.bones[b].name) rBoneFrames[b] = static_cast<int32_t>(f);
					}
				}
			}
			for(size_t b = 0; b < rMotion.bones.size(); b++)
			{
				if(rBoneFrames[b] < 0) continue;
				size_t frame = static_cast<size_t>(rBoneFrames[b]);
				BoneSample sample = sampleBone(rMotion.bones[b], rLayer.frame);
				mixed[frame] = total[frame] == 0.0f ? sample : mixSamples(mixed[frame], sample, rLayer.weight / (total[frame] + rLayer.weight));
				total[frame] += rLayer.weight;
			}
		}
		for(size_t f = 0; f < mixed.size(); f++)
		{
			if(total[f] > 0.0f) this->framePose[f] = composeBone(mixed[f]);
		}
	}

	if(this->blending && this->blendFrom.size() == this->framePose.size())
	{
		float t = std::min(this->blendElapsed / BlendSeconds, 1.0f);
		for(size_t f = 0; f < this->framePose.size(); f++)
		{
			this->framePose[f] = mixTransforms(this->blendFrom[f], this->framePose[f], t);
		}
	}
}

/*
Binds the instance's shader, uploads the mesh if needed and draws every atomic, mirroring SAInstance::draw.
Each frame's world matrix is its parent's world matrix times its local matrix, rBase_ being the parent of roots.
The local matrix is framePose[i] when the instance has a pose, otherwise the rest localTransform.
A textured submesh has its texture bound to unit 0 (the shader's baseTexture) and hasTexture set; with pOverrideTexture every textured submesh takes that one.
A skinned atomic sets skinned and uploads world[skin bone frame] * inverse bind matrix per skin bone to the shared bone
buffer (Renderer::boneBuffer, binding 1); its vertices are placed by those alone. Others use model = world[frame].
*/
void ONEInstance::draw(const glm::mat4& rBase_)
{
	Shader& rShader = shaders[this->shaderId];
	renderStats.program(rShader.id);
	glUseProgram(rShader.id);
	STH2005::ONE& rArchive = oneArchives[this->archiveId];
	RenderWareMesh& rMesh = rArchive.meshes[this->meshIndex];
	rMesh.use();
	if(this->poseDirty) this->buildPose();
	const TextureBinding* pBinding = this->bindingIndex == STH2005::NoIndex ? nullptr : &rArchive.bindings[this->bindingIndex];
	glm::mat4 base = glm::rotate(rBase_, glm::radians(RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));

	static std::vector<glm::mat4> world;
	world.resize(rMesh.frames.size());
	for(size_t i = 0; i < rMesh.frames.size(); i++)
	{
		RenderWareFrame& rFrame = rMesh.frames[i];
		const glm::mat4& rLocal = this->framePose.empty() ? rFrame.localTransform : this->framePose[i];
		world[i] = (rFrame.parent < 0 ? base : world[rFrame.parent]) * rLocal;
	}

	for(size_t a = 0; a < rMesh.atomics.size(); a++)
	{
		RenderWareAtomic& rAtomic = rMesh.atomics[a];
		bool skinned = !rAtomic.skinBoneFrames.empty();
		glUniform1i(SkinnedLocation, skinned);
		renderStats.uniformUploads++;
		glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(world[rAtomic.frame]));
		if(skinned)
		{
			if(rAtomic.skinBoneFrames.size() > MaxSkinBones)
			{
				throw std::runtime_error(std::format("ONEInstance: atomic has {} skin bones, the bone buffer holds {}.", rAtomic.skinBoneFrames.size(), MaxSkinBones));
			}
			static std::vector<glm::mat4> bones;
			bones.resize(rAtomic.skinBoneFrames.size());
			for(size_t b = 0; b < bones.size(); b++)
			{
				bones[b] = world[rAtomic.skinBoneFrames[b]] * rAtomic.inverseBindMatrices[b];
			}
			renderStats.boneUploads++;
			glNamedBufferSubData(renderer.boneBuffer, 0, bones.size() * sizeof(glm::mat4), bones.data());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, renderer.boneBuffer);
		}
		for(size_t s = 0; s < rAtomic.submeshes.size(); s++)
		{
			RenderWareSubmesh& rSubmesh = rAtomic.submeshes[s];
			PoolId textureId = pBinding ? pBinding->submeshTextureIds[a][s] : UninitializedPoolId;
			bool hasTexture = textureId != UninitializedPoolId;
			if(hasTexture)
			{
				GLuint glTexture = this->pOverrideTexture ? this->pOverrideTexture->textureId : textures[textureId].textureId;
				renderStats.texture(glTexture);
				glBindTextureUnit(0, glTexture);
			}
			glUniform1i(HasTextureLocation, hasTexture);
			rSubmesh.mesh.draw();
		}
	}
}

/* The key's archive has to be the instance's: an instance uses one archive. */
static void checkArchive(PoolId archiveId_, const STH2005::ArchiveKey& rKey_)
{
	if(oneArchives.getName(archiveId_) != rKey_.path)
	{
		throw std::runtime_error(std::format("ONEInstance: {} is not the archive of this instance ({}).", rKey_.path, oneArchives.getName(archiveId_)));
	}
}

/*
Switches to another mesh of the archive (built from the key's recipe if it is not yet). The pose is dropped (it is per
frame of the old mesh) and the texture binding is rebuilt for the new mesh against the current texture dictionary.
*/
void ONEInstance::changeActiveMesh(const STH2005::ModelKey& rKey_)
{
	checkArchive(this->archiveId, rKey_.archive);
	STH2005::ONE& rArchive = oneArchives[this->archiveId];
	uint32_t meshIndex = rArchive.useMesh(rKey_);
	uint32_t bindingIndex = this->textureDictionaryIndex == STH2005::NoIndex ? STH2005::NoIndex : rArchive.useBinding(meshIndex, this->textureDictionaryIndex);
	this->meshIndex = meshIndex;
	this->bindingIndex = bindingIndex;
	this->framePose.clear();
	this->blending = false;
}

/*
Replaces one part of the active mesh (rPrevious_, as listed in the mesh's recipe) with another (rDesired_), e.g. one
hand with a different one, without listing every combination as a mesh key. The mesh with that part switched is built
on first use and reused afterwards. Like changeActiveMesh, the pose is dropped and the binding rebuilt: the textures the
new part's materials name have to be in the current texture dictionary.
*/
void ONEInstance::switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_)
{
	STH2005::ONE& rArchive = oneArchives[this->archiveId];
	uint32_t meshIndex = rArchive.switchPart(this->meshIndex, rPrevious_, rDesired_);
	uint32_t bindingIndex = this->textureDictionaryIndex == STH2005::NoIndex ? STH2005::NoIndex : rArchive.useBinding(meshIndex, this->textureDictionaryIndex);
	this->meshIndex = meshIndex;
	this->bindingIndex = bindingIndex;
	this->framePose.clear();
}

/* Switches to another texture dictionary (a .TXD entry of the archive, decoded on first use) for the active mesh. */
void ONEInstance::changeActiveTexture(const STH2005::TextureKey& rKey_)
{
	checkArchive(this->archiveId, rKey_.archive);
	STH2005::ONE& rArchive = oneArchives[this->archiveId];
	uint32_t dictionaryIndex = rArchive.useTextureDictionary(rKey_.entry);
	uint32_t bindingIndex = rArchive.useBinding(this->meshIndex, dictionaryIndex);
	this->textureDictionaryIndex = dictionaryIndex;
	this->bindingIndex = bindingIndex;
}

/* Gives back the instance's user of its archive; the archive removes its meshes and textures when no instance uses it. */
void ONEInstance::destroy()
{
	Loading::releaseONE(this->archiveId);
}
}
