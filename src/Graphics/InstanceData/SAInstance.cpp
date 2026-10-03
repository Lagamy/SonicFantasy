#include "SAInstance.h"
#include "GPU_Globals.h"
#include "GPU_API.h"
#include "ResourceLoader.h"
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
The value of a channel at frame_. Hypothesis, unverified: linear interpolation between keys, BAMS included (no
shortest-path wrap); before the first key the first value holds; after the last key the value moves back to the first
key's over the frames left to frameCount_, so the loop closes.
*/
static glm::vec3 sampleKeys(const std::vector<SAKeyframe>& rKeys_, float frame_, uint32_t frameCount_)
{
	if(frame_ <= rKeys_.front().frame)
	{
		return rKeys_.front().value;
	}
	for(size_t k = 0; k + 1 < rKeys_.size(); k++)
	{
		const SAKeyframe& rA = rKeys_[k];
		const SAKeyframe& rB = rKeys_[k + 1];
		if(frame_ < rB.frame)
		{
			return glm::mix(rA.value, rB.value, (frame_ - rA.frame) / static_cast<float>(rB.frame - rA.frame));
		}
	}
	const SAKeyframe& rLast = rKeys_.back();
	float span = static_cast<float>(frameCount_) - rLast.frame;
	if(span <= 0.0f)
	{
		return rLast.value;
	}
	return glm::mix(rLast.value, rKeys_.front().value, (frame_ - rLast.frame) / span);
}

/*
Advances the motion by deltaTime_ (looping over its frame count) and fills nodePose. Each node takes its channels from
the motion and the rest value for the channels the motion doesn't key. Hypothesis, unverified: a keyed channel
applies even when the node's flags skip that part at rest (the flag is cleared for the pose).
*/
void SAInstance::update(float deltaTime_)
{
	if(this->currentSAAnimation == UninitializedPoolId) return;
	SAMotion& rMotion = saMotions[this->currentSAAnimation];
	SAMesh& rSAMesh = saMeshes[this->saMeshId];
	if(rMotion.frameCount == 0) return;

	this->animationFrame = std::fmod(this->animationFrame + deltaTime_ * SAMotionFramesPerSecond, static_cast<float>(rMotion.frameCount));
	this->nodePose.resize(rSAMesh.nodes.size());
	for(size_t i = 0; i < rSAMesh.nodes.size(); i++)
	{
		const SANode& rNode = rSAMesh.nodes[i];
		const SAMotionNode& rTrack = rMotion.nodes[i];
		glm::vec3 position = rNode.restPosition;
		glm::vec3 rotation = glm::vec3(rNode.restRotation);
		glm::vec3 scale = rNode.restScale;
		uint32_t flags = rNode.flags;
		if(!rTrack.positions.empty())
		{
			position = sampleKeys(rTrack.positions, this->animationFrame, rMotion.frameCount);
			flags &= ~NinjaNoPosition;
		}
		if(!rTrack.rotations.empty())
		{
			rotation = sampleKeys(rTrack.rotations, this->animationFrame, rMotion.frameCount);
			flags &= ~NinjaNoRotate;
		}
		if(!rTrack.scales.empty())
		{
			scale = sampleKeys(rTrack.scales, this->animationFrame, rMotion.frameCount);
			flags &= ~NinjaNoScale;
		}
		this->nodePose[i] = composeNinjaTransform(position, rotation, scale, flags);
	}
}

/*
Binds the instance's shader, uploads the mesh if needed and draws every submesh of every node.
Each node's world matrix is its parent's world matrix times its local matrix, rBase_ being the parent of roots.
The local matrix is nodePose[i] when the instance has a pose, otherwise the rest localTransform.
Relies on SAMesh storing parents before children.
A submesh with a texture id in the instance's TextureBinding has it bound to unit 0 (the shader's baseTexture) and hasTexture set.
*/
void SAInstance::draw(const glm::mat4& rBase_)
{
	Shader& rShader = shaders[this->shaderId];
	glUseProgram(rShader.id);
	SAMesh& rSAMesh = saMeshes[this->saMeshId];
	rSAMesh.use();
	const TextureBinding* pBinding = this->textureBindingId == UninitializedPoolId ? nullptr : &textureBindings[this->textureBindingId];
	std::vector<glm::mat4> world(rSAMesh.nodes.size());

	for(size_t i = 0; i < rSAMesh.nodes.size(); i++)
	{
		SANode& rNode = rSAMesh.nodes[i];
		const glm::mat4& rLocal = this->nodePose.empty() ? rNode.localTransform : this->nodePose[i];
		world[i] = (rNode.parent < 0 ? rBase_ : world[rNode.parent]) * rLocal;

		if(rNode.submeshes.empty()) continue;
		glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(world[i]));
		for(size_t s = 0; s < rNode.submeshes.size(); s++)
		{
			SASubmesh& rSubmesh = rNode.submeshes[s];
			PoolId textureId = pBinding ? pBinding->submeshTextureIds[i][s] : UninitializedPoolId;
			bool hasTexture = textureId != UninitializedPoolId;
			if(hasTexture)
			{
				glBindTextureUnit(0, textures[textureId].textureId);
			}
			glUniform1i(HasTextureLocation, hasTexture);
			rSubmesh.mesh.draw();
		}
	}
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
