#include "DashTrail.h"
#include "GPU_API.h"
#include "Globals.h"
#include "API.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
/* Shadow's trail (decompile): RGB bytes from the character table (0x7f, 0x40, 0x00), the tube radius of the creation call FUN_800a2d54(fx, 5.0) (hypothesis for the radius chain), and the step between ring points. */
static const glm::vec3 TrailColor(127.0f / 255.0f, 64.0f / 255.0f, 0.0f);
constexpr float TrailRadius = 5.0f;
constexpr float RingStepDegrees = 36.0f;

/*
The original samples the player's position (player+0, live-checked as the position; the decompile passes the player pointer to FUN_802de4b4). The port's Entity::position is the model origin at his feet, and the
game's body position is taken as the capsule's center (hypothesis, Shadow's Changed file External/Prefabs/STH2005/Shadow/Changed/prefab.json: 5.0 above the feet), so the samples are lifted by that. Observed by the user: at the feet the ribbon did not reach his head.
*/
constexpr float BodyCenterHeight = 5.0f;

void DashTrail::init()
{
	glCreateBuffers(1, &this->VBO);
	glCreateVertexArrays(1, &this->VAO);
	glVertexArrayAttribFormat(this->VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
	glVertexArrayAttribFormat(this->VAO, 3, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, color));
	glEnableVertexArrayAttrib(this->VAO, 0);
	glEnableVertexArrayAttrib(this->VAO, 3);
	glVertexArrayAttribBinding(this->VAO, 0, 0);
	glVertexArrayAttribBinding(this->VAO, 3, 0);
	glVertexArrayVertexBuffer(this->VAO, 0, this->VBO, 0, sizeof(Vertex));
}

void DashTrail::setActive(PoolId entityId_, bool on_)
{
	for(Trail& rTrail : this->trails)
	{
		if(rTrail.attached && rTrail.entityId == entityId_)
		{
			if(!on_) rTrail.attached = false;
			return;
		}
	}
	if(on_) this->trails.push_back(Trail{entityId_, true, 255, {}});
}

/*
The sample the original pushes (FUN_802de4b4): the direction is the normalised vector from the new position to the previous newest sample ((0, 0, -1) for the first), the offset is the part of world up
perpendicular to it, normalised and scaled by the radius. A zero length direction (no movement) keeps the previous one, and a direction along up takes any perpendicular: the original does not handle these (it would
divide by zero), so both are the port's own guard.
*/
static void pushSample(std::vector<DashTrail::Sample>& rSamples_, const glm::vec3& rPosition_, uint32_t capacity_)
{
	glm::vec3 direction(0.0f, 0.0f, -1.0f);
	if(!rSamples_.empty())
	{
		glm::vec3 back = rSamples_.back().position - rPosition_;
		direction = glm::length(back) > 1e-6f ? glm::normalize(back) : rSamples_.back().direction;
	}
	glm::vec3 up(0.0f, 1.0f, 0.0f);
	glm::vec3 perpendicular = up - direction * glm::dot(up, direction);
	if(glm::length(perpendicular) < 1e-6f) perpendicular = glm::cross(direction, glm::vec3(1.0f, 0.0f, 0.0f));
	if(rSamples_.size() >= capacity_) rSamples_.erase(rSamples_.begin());
	rSamples_.push_back(DashTrail::Sample{rPosition_, glm::normalize(perpendicular) * TrailRadius, direction, 0.0f});
}

/*
Alphas of the original's update (FUN_802de314): the newest sample 0, the others the master alpha, the AlphaRampSamples oldest rising from the tail by AlphaRampStep per sample while they stay under the master alpha.
The order of the three writes where they overlap (a trail shorter than the ramp) is a hypothesis.
*/
static void setAlphas(std::vector<DashTrail::Sample>& rSamples_, int masterAlpha_)
{
	for(DashTrail::Sample& rSample : rSamples_) rSample.alpha = static_cast<float>(masterAlpha_);
	for(uint32_t i = 0; i < DashTrail::AlphaRampSamples && i < rSamples_.size(); i++)
	{
		rSamples_[i].alpha = static_cast<float>(std::min(masterAlpha_, static_cast<int>(i) * DashTrail::AlphaRampStep));
	}
	if(!rSamples_.empty()) rSamples_.back().alpha = 0.0f;
}

void DashTrail::tick()
{
	for(Trail& rTrail : this->trails)
	{
		if(rTrail.attached && !entityExists(rTrail.entityId)) rTrail.attached = false;
		if(rTrail.attached) pushSample(rTrail.samples, Globals::Game::entities[rTrail.entityId].position + glm::vec3(0.0f, BodyCenterHeight, 0.0f), TrailSamples);
		else rTrail.masterAlpha -= FadePerTick;
		setAlphas(rTrail.samples, std::max(rTrail.masterAlpha, 0));
	}
	std::erase_if(this->trails, [](const Trail& rTrail_) { return rTrail_.masterAlpha <= 0; });
}

/*
The tube (FUN_802ddf64): each sample gives RingPoints points around its position, the first at the sample's offset and each next one turned RingStepDegrees about the sample's direction (ten steps = a full circle);
two consecutive samples are joined by a triangle strip alternating between them, here written as plain triangles. Untextured, vertex colour (the belt colour, the sample's alpha), additive (source alpha, destination one), no
culling; depth tested but not written (hypothesis: the original's depth state is not read).
*/
void DashTrail::draw()
{
	this->vertices.clear();
	for(const Trail& rTrail : this->trails)
	{
		if(rTrail.samples.size() < 2) continue;
		std::vector<std::array<glm::vec3, RingPoints>> rings(rTrail.samples.size());
		for(uint32_t i = 0; i < rTrail.samples.size(); i++)
		{
			const Sample& rSample = rTrail.samples[i];
			glm::quat step = glm::angleAxis(glm::radians(RingStepDegrees), rSample.direction);
			glm::vec3 offset = rSample.offset;
			for(uint32_t k = 0; k < RingPoints; k++)
			{
				rings[i][k] = rSample.position + offset;
				offset = step * offset;
			}
		}
		for(uint32_t i = 1; i < rTrail.samples.size(); i++)
		{
			glm::vec4 previousColor(TrailColor, rTrail.samples[i - 1].alpha / 255.0f);
			glm::vec4 currentColor(TrailColor, rTrail.samples[i].alpha / 255.0f);
			for(uint32_t k = 0; k + 1 < RingPoints; k++)
			{
				Vertex a{rings[i - 1][k], previousColor};
				Vertex b{rings[i][k], currentColor};
				Vertex c{rings[i - 1][k + 1], previousColor};
				Vertex d{rings[i][k + 1], currentColor};
				this->vertices.insert(this->vertices.end(), {a, b, c, b, d, c});
			}
		}
	}
	if(this->vertices.empty()) return;

	glNamedBufferData(this->VBO, this->vertices.size() * sizeof(Vertex), this->vertices.data(), GL_STREAM_DRAW);
	Shader& rShader = getShaderByName("Default");
	glUseProgram(rShader.id);
	glm::mat4 identity(1.0f);
	glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(identity));
	glBindVertexArray(this->VAO);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(this->vertices.size()));
	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void DashTrail::destroy()
{
	glDeleteVertexArrays(1, &this->VAO);
	glDeleteBuffers(1, &this->VBO);
	this->trails.clear();
}
}
