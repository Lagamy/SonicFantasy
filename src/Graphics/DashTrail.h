#pragma once
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "PoolId.h"

namespace Graphics
{
/*
The ribbon that Shadow's jump dash leaves (the original's Eff::Belt, decoded from the decompile: docs/STH2005_PLAYER_REFERENCE.md "Dash ribbon"; the port's numbers are marked there as decompile or hypothesis).
Each trail follows one entity: every gameplay tick (`tick`, called by GameplaySystem::tick after the physics) it pushes a sample at the entity's position into a ring of TrailSamples (the oldest is dropped when it is
full), and sets the sample alphas. `setActive(entity, false)` detaches it (the original's leave): no more samples, the master alpha loses FadePerTick every tick and the trail is removed at 0, so it fades out where it is.
`draw` (Renderer::run, after the entities) draws every trail as an untextured, vertex coloured, additive tube without culling or depth writes.
The colour and radius are Shadow's (character table colour, the 5.0 of the creation call).
*/
struct DashTrail
{
	static constexpr uint32_t TrailSamples = 60;
	static constexpr uint32_t RingPoints = 11;
	static constexpr int FadePerTick = 8;
	static constexpr int AlphaRampStep = 32;
	static constexpr uint32_t AlphaRampSamples = 9;

	struct Sample
	{
		glm::vec3 position;
		glm::vec3 offset;
		glm::vec3 direction;
		float alpha;
	};

	void init();

	/* Starts a trail for the entity, or does nothing if it already has an attached one (the original creates it only when none exists). Detaches it with on_ false. */
	void setActive(PoolId entityId_, bool on_);

	/* One gameplay tick: pushes the samples, updates the alphas, removes the faded trails. */
	void tick();

	void draw();
	void destroy();

private:
	struct Trail
	{
		PoolId entityId;
		bool attached;
		int masterAlpha;
		std::vector<Sample> samples;
	};

	struct Vertex
	{
		glm::vec3 position;
		glm::vec4 color;
	};

	std::vector<Trail> trails;
	std::vector<Vertex> vertices;
	GLuint VAO = 0;
	GLuint VBO = 0;
};
}
