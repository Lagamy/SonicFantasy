#include "EffectBalls.h"
#include "GPU_Globals.h"
#include "GPU_API.h"
#include "Globals.h"
#include "API.h"
#include "ResourceKeys.h"
#include "ResourceLoader.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
/*
Where the balls sit (live measurement, tools/shadowEffectLog.py, two recordings on flat ground): the ball's world matrix is the player's yaw turn with the translation player position + 1.6 ahead + 1.2 up, for both balls A and B, in every behavior (Jump, JumpDash, Fall, Landing). It does not follow the animation: its rotation stays the plain yaw
(hypothesis: on a slope it follows the player's whole orientation, as the entity's transform does; only flat ground was recorded). The player's forward is the local -z of the entity's transform (observed: yaw 1.564 and motion towards -x).
*/

/*
The measured offset: 1.6 ahead of the player (live measurement, see above). It is used for both balls again, with the root frame of the mesh left out (see the draw): the ball B's -13.09 along z put its body far ahead of the character, and in the Fall far below him (observed by the user: the dash ball too far forward, the Fall ball too low, the jump ball A too far back at 0.8).
*/
constexpr float BallForward = 1.6f;
constexpr float BallUp = 1.2f;

/*
The measured offsets are from the original's player position, which is the body's middle (a capsule of radius 5, observed); the port's entity position is at the feet (hypothesis, see addShadowShape), so every ball is lifted by the radius.
*/
constexpr float BodyCenterHeight = 5.0f;

/*
The tint of the model record (EFD record of Jump_ballA and Jump_ballB, disc file: bytes ff ff 80 00 at +0x38, an alpha then three colour bytes; hypothesis for the order of the three: red 255, green 128, blue 0).
*/
static const glm::vec3 BallTint(1.0f, 128.0f / 255.0f, 0.0f);

/*
The fade in: MatAlpha(0, 0) then MatAlpha(255, 8) in both records' command lists (disc file), alpha constant after that. Hypothesis: the 8 is in 60 Hz frames and the ramp is linear.
*/
constexpr float FadeInSeconds = 8.0f / 60.0f;

/*
The fade out after the stop (live measurement, tools/shadowEffectLog.py, two recordings of both balls): alpha falls linearly from 1 to 0 in 8 ticks of 60 Hz, 32/255 per tick, then the object is gone. The fade in was measured the same way: 0 at creation to 1 after 8 ticks, linear.
*/
constexpr float FadeOutSeconds = 8.0f / 60.0f;

/*
The Fall's ball (live measurement, one sample of 2 ticks, flat ground, tools/shadowEffectLog.py): its matrix is the player's yaw turn then a turn of -90 degrees about X, at the player's position moved 0.78 behind and 2.0 down. A different offset from the dash's balls, not joint 0's
matrix moved by the decompile's (0, -5, 0) (hypothesis for the decompile's reading; the measured one is used until a longer Fall is recorded).
*/
constexpr float FallBallBBehind = 0.78f;
/*
The measured 2.0 down is one sample; with it the Fall ball looked a bit too high (observed by the user), so 3.0 is a guess to be moved by what the user sees.
*/
constexpr float FallBallBDown = 3.0f;
constexpr float FallBallBPitchDegrees = -90.0f;

static const char* const UvaNames[2] = {"EFFCHRBALLA.UVA", "EFFCHRBALLB.UVA"};

void EffectBalls::loadMesh(Mesh& rMesh_, bool ballB_)
{
	PoolId shaderId = shaders.getIdByName("STH2005");
	PoolId instanceId = Loading::addONEInstance(shaderId, ballB_ ? EffectBallBMesh : EffectBallAMesh, &CommonEffectTextures);
	rMesh_.uvAnimations = Renderware::readUvaDictionary(oneArchives[oneInstances[instanceId].archiveId].readFile(UvaNames[ballB_ ? 1 : 0]));
	rMesh_.instanceId = instanceId;
}

void EffectBalls::setActive(PoolId entityId_, Kind kind_, bool on_)
{
	auto found = std::find_if(this->balls.begin(), this->balls.end(), [&](const Ball& rBall_) { return rBall_.entityId == entityId_ && rBall_.kind == kind_ && rBall_.stoppedFor < 0.0f; });
	if(!on_)
	{
		if(found != this->balls.end()) found->stoppedFor = 0.0f;
		return;
	}
	if(found != this->balls.end()) return;

	Mesh& rMesh = this->meshes[kind_ == JumpBallA ? 0 : 1];
	if(rMesh.instanceId == UninitializedPoolId) this->loadMesh(rMesh, kind_ != JumpBallA);
	this->balls.push_back(Ball{entityId_, kind_, 0.0f});
}

void EffectBalls::tick(float deltaTime_)
{
	for(Ball& rBall : this->balls)
	{
		rBall.age += deltaTime_;
		if(rBall.stoppedFor >= 0.0f) rBall.stoppedFor += deltaTime_;
	}
	std::erase_if(this->balls, [](const Ball& rBall_) { return !entityExists(rBall_.entityId) || rBall_.stoppedFor >= FadeOutSeconds; });
}

/*
The UV offset of an animation at a time: linear between the keys, looping over the duration (hypothesis: the .UVA says nothing about looping; the ball's offset ends at v = 3.0 against 0.0 at the start, a whole number of
repeats of a wrapping texture, so the loop has no jump). Only the offset is used: the rotation, scale and skew of both balls' keys are neutral (disc file).
*/
static glm::vec2 sampleOffset(const Renderware::UvAnimation& rAnimation_, float time_)
{
	if(rAnimation_.keys.empty()) return glm::vec2(0.0f);
	float time = rAnimation_.duration > 0.0f ? std::fmod(time_, rAnimation_.duration) : 0.0f;
	for(size_t k = 0; k + 1 < rAnimation_.keys.size(); k++)
	{
		const Renderware::UvAnimKey& rFrom = rAnimation_.keys[k];
		const Renderware::UvAnimKey& rTo = rAnimation_.keys[k + 1];
		if(time < rTo.time)
		{
			float span = rTo.time - rFrom.time;
			return glm::mix(rFrom.offset, rTo.offset, span > 0.0f ? (time - rFrom.time) / span : 0.0f);
		}
	}
	return rAnimation_.keys.back().offset;
}

/*
The matrix the ball's mesh is drawn from: the entity's transform, the RenderWareYawDegrees turn every ONE model gets (the offsets are measured in the model's own space, where forward is -z), then the measured offset. Without the turn the offset
lands sideways of the character (observed in the port). False if the entity is gone.
*/
static bool ballBase(const PoolId entityId_, EffectBalls::Kind kind_, glm::mat4& rOut_)
{
	if(!entityExists(entityId_)) return false;
	const Entity& rEntity = Globals::Game::entities[entityId_];
	glm::mat4 base = glm::rotate(rEntity.transform, glm::radians(RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));

	if(kind_ == EffectBalls::FallBallB)
	{
		rOut_ = glm::translate(base, glm::vec3(0.0f, BodyCenterHeight - FallBallBDown, FallBallBBehind));
		rOut_ = glm::rotate(rOut_, glm::radians(FallBallBPitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
	}
	else rOut_ = glm::translate(base, glm::vec3(0.0f, BodyCenterHeight + BallUp, -BallForward));
	return true;
}

/*
Draws every ball as its own additive draw (source alpha, destination one) so each can feed an emissive mask later: depth tested, not written, no culling. The mesh is drawn like ONEInstance::draw does an unskinned model (each atomic by
its frame's world matrix, the texture of its binding), with the shader's uvOffset set from the submesh's UV animation and effectTint from the tint and the fade in and out, which are put back to no change after the draw.
*/
void EffectBalls::draw()
{
	if(this->balls.empty()) return;
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);

	static std::vector<glm::mat4> world;
	for(const Ball& rBall : this->balls)
	{
		glm::mat4 base;
		if(!ballBase(rBall.entityId, rBall.kind, base)) continue;
		const Mesh& rBallMesh = this->meshes[rBall.kind == JumpBallA ? 0 : 1];
		ONEInstance& rInstance = oneInstances[rBallMesh.instanceId];
		STH2005::ONE& rArchive = oneArchives[rInstance.archiveId];
		RenderWareMesh& rMesh = rArchive.meshes[rInstance.meshIndex];
		rMesh.use();
		const TextureBinding& rBinding = rArchive.bindings[rInstance.bindingIndex];

		Shader& rShader = shaders[rInstance.shaderId];
		glUseProgram(rShader.id);
		glUniform4f(EffectTintLocation, BallTint.r, BallTint.g, BallTint.b, std::min(rBall.age / FadeInSeconds, 1.0f) * (rBall.stoppedFor < 0.0f ? 1.0f : 1.0f - rBall.stoppedFor / FadeOutSeconds));
		glUniform1i(SkinnedLocation, 0);

		/*
		The mesh's root frame is left out: the ball is placed by its object matrix alone (hypothesis: the original's model effect writes that matrix over the clump's root frame, so the -13.09 of ball B's root frame is not applied; the measured Fall offset of 2.0 down would put the mesh 11 to 20 below the player otherwise, which the user saw as too low; not found in the decompile yet).
		*/
		world.resize(rMesh.frames.size());
		for(size_t i = 0; i < rMesh.frames.size(); i++)
		{
			world[i] = rMesh.frames[i].parent < 0 ? base : world[rMesh.frames[i].parent] * rMesh.frames[i].localTransform;
		}
		for(size_t a = 0; a < rMesh.atomics.size(); a++)
		{
			RenderWareAtomic& rAtomic = rMesh.atomics[a];
			glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(world[rAtomic.frame]));
			for(size_t s = 0; s < rAtomic.submeshes.size(); s++)
			{
				RenderWareSubmesh& rSubmesh = rAtomic.submeshes[s];
				PoolId textureId = rBinding.submeshTextureIds[a][s];
				bool hasTexture = textureId != UninitializedPoolId;
				if(hasTexture) glBindTextureUnit(0, textures[textureId].textureId);
				glUniform1i(HasTextureLocation, hasTexture);

				glm::vec2 offset(0.0f);
				for(const Renderware::UvAnimation& rAnimation : rBallMesh.uvAnimations)
				{
					if(rAnimation.name == rSubmesh.uvAnimName) offset = sampleOffset(rAnimation, rBall.age);
				}
				glUniform2f(UvOffsetLocation, offset.x, offset.y);
				rSubmesh.mesh.draw();
			}
		}
		glUniform2f(UvOffsetLocation, 0.0f, 0.0f);
		glUniform4f(EffectTintLocation, 1.0f, 1.0f, 1.0f, 1.0f);
	}

	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* The instances stay in the pool and the archive with them: Renderer::destroy clears the archives right after, as for every other model. */
void EffectBalls::destroy()
{
	this->balls.clear();
	for(Mesh& rMesh : this->meshes) rMesh = Mesh();
}
}
