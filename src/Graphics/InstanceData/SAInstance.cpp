#include "SAInstance.h"
#include "GPU_Globals.h"
#include "RenderStats.h"
#include "GPU_API.h"
#include "ResourceLoader.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
/*
Hypothesis, unverified: SADX motions play at 30 frames per second. Observed: at 60 (its game logic rate, speed 1) Sonic's
Alternate Stand (16 frames) looped too fast. The original may also scale the speed per motion; nothing here says so.
*/
constexpr float SAMotionFramesPerSecond = 30.0f;

/*
The value of a channel at frame_, as the game samples linear motions (external, decomp: SADX GC main.dol fn_800493E0
for the key search, fn_800488D0 for positions and scales, fn_80048ABC for rotations).
The pair is the last key whose frame is <= the whole frame and the key after it; past the last key both are the last
key, so its value holds (the loop doesn't blend back to the first key). Before the first key, keys 0 and 1 extrapolate.
Rotations go the short way: the difference between the two keys is cut to a signed 16-bit BAMS value, and the result
is truncated to a whole BAMS value.
*/
static glm::vec3 sampleKeys(const std::vector<SAKeyframe>& rKeys_, float frame_, bool rotation_)
{
	uint32_t wholeFrame = static_cast<uint32_t>(frame_);
	size_t k = 0;
	while(k + 1 < rKeys_.size() && rKeys_[k + 1].frame <= wholeFrame)
	{
		k++;
	}
	const SAKeyframe& rA = rKeys_[k];
	const SAKeyframe& rB = rKeys_[k + 1 < rKeys_.size() ? k + 1 : k];
	float t = frame_ - static_cast<float>(rA.frame);
	if(rB.frame > rA.frame)
	{
		t /= static_cast<float>(rB.frame - rA.frame);
	}

	if(!rotation_)
	{
		return rA.value + t * (rB.value - rA.value);
	}
	glm::vec3 value;
	for(int c = 0; c < 3; c++)
	{
		int32_t a = static_cast<int32_t>(rA.value[c]);
		int16_t difference = static_cast<int16_t>(static_cast<int32_t>(rB.value[c]) - a);
		value[c] = static_cast<float>(static_cast<int32_t>(static_cast<float>(a) + t * static_cast<float>(difference)));
	}
	return value;
}

/*
Samples every node of rMotion_ at frame_ into rOut_, the rest value filling a channel the motion doesn't key.
*/
static void sampleMotion(const SAMesh& rSAMesh_, const SAMotion& rMotion_, float frame_, std::vector<SANodeSample>& rOut_)
{
	rOut_.resize(rSAMesh_.nodes.size());
	for(size_t i = 0; i < rSAMesh_.nodes.size(); i++)
	{
		const SANode& rNode = rSAMesh_.nodes[i];
		const SAMotionNode& rTrack = rMotion_.nodes[i];
		SANodeSample& rSample = rOut_[i];
		rSample.position = rTrack.positions.empty() ? rNode.restPosition : sampleKeys(rTrack.positions, frame_, false);
		rSample.rotation = rTrack.rotations.empty() ? glm::vec3(rNode.restRotation) : sampleKeys(rTrack.rotations, frame_, true);
		rSample.scale = rTrack.scales.empty() ? rNode.restScale : sampleKeys(rTrack.scales, frame_, false);
	}
}

/* Frames a second the motion advances at now; a negative framesPerSecond plays it backwards. */
float SAInstance::playbackRate() const
{
	return this->framesPerSecond != 0.0f ? this->framesPerSecond : SAMotionFramesPerSecond;
}

/*
The frame after advancing frame_ by delta_ frames in a motion of frameCount_ frames: wrapped round (either way) when it loops, held at the last frame going forward or at the first going backwards when not.
*/
float advanceMotionFrame(float frame_, float delta_, uint32_t frameCount_, bool loop_)
{
	const float advanced = frame_ + delta_;
	const float count = static_cast<float>(frameCount_);
	if(!loop_) return std::clamp(advanced, 0.0f, count - 1.0f);
	const float wrapped = std::fmod(advanced, count);
	return wrapped < 0.0f ? wrapped + count : wrapped;
}

/*
Advances the motion by deltaTime_ (looping over its frame count, or holding the end it runs to with loop false; a negative rate runs it backwards); nodePose is rebuilt by draw (buildPose) if the instance is drawn.
While a blend runs the factor advances instead, and the motion stays at its first frame; the tick the factor reaches 1 ends the blend and the motion plays from the next one. A motion playing backwards
starts at its last frame when the blend ends (external, decomp FUN_804cf4f4 mode 6: the frame is set to the count less one as the blend completes, the blend itself having gone to the first key).
*/
void SAInstance::update(float deltaTime_)
{
	if(this->currentSAAnimation == UninitializedPoolId) return;
	SAMotion& rMotion = saMotions[this->currentSAAnimation];
	if(rMotion.frameCount == 0) return;

	if(this->blendRate > 0.0f)
	{
		this->blendFactor += deltaTime_ * this->blendRate;
		if(this->blendFactor >= 1.0f)
		{
			this->cancelBlend();
			if(this->playbackRate() < 0.0f) this->animationFrame = static_cast<float>(rMotion.frameCount - 1);
		}
		this->poseDirty = true;
		return;
	}
	this->animationFrame = advanceMotionFrame(this->animationFrame, deltaTime_ * this->playbackRate(), rMotion.frameCount, this->loop);
	this->poseDirty = true;
}

/*
Starts a blend from the pose the instance shows now to the first frame of the motion that is played next, over 1 / rate_ seconds (a blend already running is taken as it stands). Called before
the motion and the mesh are switched (the snapshot needs the old ones); with no motion playing there is nothing to blend from and it does nothing. The caller cancels it (cancelBlend) if the new
motion is for another model.
*/
void SAInstance::beginBlend(float rate_)
{
	if(this->currentSAAnimation == UninitializedPoolId) return;
	std::vector<SANodeSample> snapshot;
	this->samplePose(snapshot);
	this->blendFrom = std::move(snapshot);
	this->blendFactor = 0.0f;
	this->blendRate = rate_;
}

void SAInstance::cancelBlend()
{
	this->blendRate = 0.0f;
	this->blendFactor = 0.0f;
}

/*
Fills rOut_ with the pose of every node now: the motion at animationFrame, and during a blend each channel going from blendFrom to the motion's first frame by blendFactor, positions and scales
linearly, rotations the short way round (the difference cut to a signed 16-bit BAMS value, as sampleKeys does). External (decomp, FUN_804cea34 and FUN_804cf4f4 mode 2): the game blends
through a two-key motion of the old pose and the new motion's first key.
*/
void SAInstance::samplePose(std::vector<SANodeSample>& rOut_) const
{
	sampleMotion(saMeshes[this->saMeshId], saMotions[this->currentSAAnimation], this->animationFrame, rOut_);
	if(this->blendRate <= 0.0f || this->blendFrom.size() != rOut_.size()) return;
	const float t = this->blendFactor;
	for(size_t i = 0; i < rOut_.size(); i++)
	{
		SANodeSample& rSample = rOut_[i];
		const SANodeSample& rFrom = this->blendFrom[i];
		for(int c = 0; c < 3; c++)
		{
			int32_t from = static_cast<int32_t>(rFrom.rotation[c]);
			int16_t difference = static_cast<int16_t>(static_cast<int32_t>(rSample.rotation[c]) - from);
			rSample.rotation[c] = static_cast<float>(static_cast<int32_t>(static_cast<float>(from) + t * static_cast<float>(difference)));
		}
		rSample.position = rFrom.position + t * (rSample.position - rFrom.position);
		rSample.scale = rFrom.scale + t * (rSample.scale - rFrom.scale);
	}
}

/*
buildPose fills nodePose: each node takes its channels from the motion (samplePose) and the rest value for the channels the motion doesn't key. External (decomp: SADX GC main.dol fn_80049638):
while a motion plays, the node's NoPosition / NoRotate / NoScale flags are ignored, so translate, rotate and scale
always apply, keyed or not.
*/
void SAInstance::buildPose()
{
	this->poseDirty = false;
	SAMesh& rSAMesh = saMeshes[this->saMeshId];
	static std::vector<SANodeSample> samples;
	this->samplePose(samples);
	this->nodePose.resize(rSAMesh.nodes.size());
	for(size_t i = 0; i < rSAMesh.nodes.size(); i++)
	{
		uint32_t flags = rSAMesh.nodes[i].flags & ~(NinjaNoPosition | NinjaNoRotate | NinjaNoScale);
		this->nodePose[i] = composeNinjaTransform(samples[i].position, samples[i].rotation, samples[i].scale, flags);
	}
}

/*
Binds the instance's shader, uploads the mesh if needed and draws every submesh of every node.
Each node's world matrix is its parent's world matrix times its local matrix, rBase_ being the parent of roots.
The local matrix is nodePose[i] when the instance has a pose, otherwise the rest localTransform.
Relies on SAMesh storing parents before children.
A submesh with a texture id in the instance's TextureBinding has it bound to unit 0 (the shader's baseTexture) and hasTexture set,
with the wrap of its material set on the texture before each draw (a texture used by materials that disagree is set again per submesh). With pOverrideTexture every textured submesh takes that one.
*/
void SAInstance::draw(const glm::mat4& rBase_)
{
	Shader& rShader = shaders[this->shaderId];
	renderStats.program(rShader.id);
	glUseProgram(rShader.id);
	SAMesh& rSAMesh = saMeshes[this->saMeshId];
	rSAMesh.use();
	if(this->poseDirty) this->buildPose();
	const TextureBinding* pBinding = this->textureBindingId == UninitializedPoolId ? nullptr : &textureBindings[this->textureBindingId];
	/* The world matrices live in a vector kept between calls: a heap allocation per entity per frame cost more than the math. */
	static std::vector<glm::mat4> world;
	world.resize(rSAMesh.nodes.size());

	/* Back-face culling is on for the whole renderer; a double sided submesh (SASubmesh::doubleSided) turns it off for its draw only. */
	bool cullDisabled = false;

	for(size_t i = 0; i < rSAMesh.nodes.size(); i++)
	{
		SANode& rNode = rSAMesh.nodes[i];
		const glm::mat4& rLocal = this->nodePose.empty() ? rNode.localTransform : this->nodePose[i];
		world[i] = (rNode.parent < 0 ? rBase_ : world[rNode.parent]) * rLocal;

		if(rNode.submeshes.empty()) continue;
		renderStats.uniformUploads++;
		glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(world[i]));
		for(size_t s = 0; s < rNode.submeshes.size(); s++)
		{
			SASubmesh& rSubmesh = rNode.submeshes[s];
			PoolId textureId = pBinding ? pBinding->submeshTextureIds[i][s] : UninitializedPoolId;
			bool hasTexture = textureId != UninitializedPoolId;
			if(hasTexture)
			{
				Texture& rTexture = this->pOverrideTexture ? *this->pOverrideTexture : textures[textureId];
				GLuint glTexture = rTexture.textureId;
				renderStats.texture(glTexture);
				glBindTextureUnit(0, glTexture);
				GLint wrapS = rSubmesh.clampU ? GL_CLAMP_TO_EDGE : rSubmesh.flipU ? GL_MIRRORED_REPEAT : GL_REPEAT;
				GLint wrapT = rSubmesh.clampV ? GL_CLAMP_TO_EDGE : rSubmesh.flipV ? GL_MIRRORED_REPEAT : GL_REPEAT;
				if(rTexture.appliedWrapS != wrapS)
				{
					glTextureParameteri(glTexture, GL_TEXTURE_WRAP_S, wrapS);
					rTexture.appliedWrapS = wrapS;
				}
				if(rTexture.appliedWrapT != wrapT)
				{
					glTextureParameteri(glTexture, GL_TEXTURE_WRAP_T, wrapT);
					rTexture.appliedWrapT = wrapT;
				}
			}
			glUniform1i(HasTextureLocation, hasTexture);
			if(rSubmesh.doubleSided != cullDisabled)
			{
				if(rSubmesh.doubleSided) glDisable(GL_CULL_FACE);
				else glEnable(GL_CULL_FACE);
				cullDisabled = rSubmesh.doubleSided;
			}
			rSubmesh.mesh.draw();
		}
	}
	if(cullDisabled) glEnable(GL_CULL_FACE);
}

/* Gives back the instance's users of its TextureSet, motion and mesh; each is removed when no instance uses it. */
void SAInstance::destroy()
{
	if(this->textureSetId != UninitializedPoolId)
	{
		Loading::releaseTextureSet(this->textureSetId);
	}
	if(this->currentSAAnimation != UninitializedPoolId)
	{
		Loading::releaseSAMotion(this->currentSAAnimation);
	}
	Loading::releaseSAMesh(this->saMeshId);
}
}
