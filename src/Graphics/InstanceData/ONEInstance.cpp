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
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
/*
Hypothesis, unverified: Shadow's motions play at 60 frames per second (its game logic rate), at speed 1.
*/
constexpr float ONEMotionFramesPerSecond = 60.0f;

/*
Hypothesis, unverified: the Euler angles of a motion (and of a .BON) are applied Rz * Ry * Rx (X first, as the SA code
does), which reproduces every DFF frame rotation of SH.BON (observed, error below 3e-4). Ry * Rz * Rx fits the rest
pose just as well, since y is about 0 there, but the motions use non-zero y. Set this to true to try the other one.
*/
constexpr bool ONEEulerYZX = false;

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
			rActive = motion;
			return;
		}
	}
	this->motions.push_back(motion);
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
			float advanced = rActive.frame + deltaTime_ * ONEMotionFramesPerSecond;
			rActive.frame = rActive.loop ? std::fmod(advanced, static_cast<float>(rMotion.lastFrame)) : std::min(advanced, static_cast<float>(rMotion.lastFrame));
		}
	}
	this->poseDirty = true;
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
			glm::vec3 rotation = sampleVec3(rBone.rotation, rActive.frame);
			glm::mat4 transform = glm::translate(glm::mat4(1.0f), sampleVec3(rBone.translation, rActive.frame));
			if(ONEEulerYZX)
			{
				transform = glm::rotate(transform, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
				transform = glm::rotate(transform, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
				transform = glm::rotate(transform, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
			}
			else
			{
				transform = glm::rotate(transform, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
				transform = glm::rotate(transform, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
				transform = glm::rotate(transform, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
			}
			this->framePose[rActive.boneFrames[b]] = glm::scale(transform, sampleVec3(rBone.scale, rActive.frame));
		}
	}
}

/*
Binds the instance's shader, uploads the mesh if needed and draws every atomic, mirroring SAInstance::draw.
Each frame's world matrix is its parent's world matrix times its local matrix, rBase_ being the parent of roots.
The local matrix is framePose[i] when the instance has a pose, otherwise the rest localTransform.
A textured submesh has its texture bound to unit 0 (the shader's baseTexture) and hasTexture set.
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
				renderStats.texture(textures[textureId].textureId);
				glBindTextureUnit(0, textures[textureId].textureId);
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
