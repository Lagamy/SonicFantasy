#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "PoolId.h"
#include "BasicMesh.h"
#include "Texture.h"

namespace Graphics
{
/*
The blue spheres of SADX Sonic's homing attack burst and spin dash aura (docs/SADX_SONIC_REFERENCE.md "Geometry, texture and draw state of the aura / burst" and "Burst and aura timing": what is decompile, dump or hypothesis).
One mesh serves both: a UV sphere of radius 4.5 (10 meridians, rings at 0, +-30 and +-60 degrees, poles) whose texture coordinates are a plain projection of the position (s from y, t from x), drawn with one of two 16x16 textures that run from black to blue
in t. Nothing is scrolled: the effect is the sphere's alpha falling by a constant material (the original's FUN_8060f6b4: alpha offset -counter/10 for the burst, -counter/5 for the aura).
A Burst is made once, stays where it was made, starts at scale 3.0 and loses 0.4 a tick for 10 ticks. An Aura is made every tick while he rolls fast or flies a homing attack: it is drawn at the entity's position of the previous tick (the original's lag), keeps the offset it was made with and lives 2 ticks (the original's 5; observed, user: the afterimage lasts too long and shows as copies).
A Charge is made every tick while the spin dash charges (decompile FUN_8059e488): the same sphere flattened and tilted into an ellipsoid in his frame, at his position of that tick, 5 ticks; drawn in one flat colour (observed, user; the decompile has the gradient textures). The aura is drawn in that flat colour too (observed, user: the roll after the release); only the burst keeps the gradient.
`draw` (Renderer::run, after the effect balls) draws every sphere on its own, textured, unlit, additive (hypothesis: the blend is not set by the original's tasks, the black half must vanish), no depth write.
*/
struct DashSpheres
{
	enum Kind
	{
		Burst,
		Aura,
		Charge
	};

	/*
	Makes one sphere. The burst's position is the entity's position plus offset_ at once; the aura's is the entity's position of the previous tick plus offset_ plus a random vector of length 0.05 to 0.4 in a random direction (the decompile's aura has 1 to 3, observed user: fuzzy; the charge's is 0.2 to 1.2); drawn at full tint (the other spheres' is the material's grey).
	The charge's is the same, not lagged (decompile).
	heading_ turns the aura's gradient (its model x axis points along the heading, as the original draws it in the player's frame); the burst keeps the world axes. The charge is drawn in the frame of heading_ and rUp_ (the ground normal or the air's tilt).
	Loads the mesh and textures on the first call.
	*/
	void spawn(PoolId entityId_, Kind kind_, const glm::vec3& rOffset_, float heading_, const glm::vec3& rUp_ = glm::vec3(0.0f, 1.0f, 0.0f));

	/* One gameplay tick: ages every sphere, removes the finished ones and those whose entity is gone. */
	void tick();

	void draw();
	void destroy();

private:
	struct Sphere
	{
		PoolId entityId;
		Kind kind;
		/* The entity's position as last sampled (the aura draws from it, then samples again; the charge's is the current one), the offset fixed at the making, where it is drawn this tick. */
		glm::vec3 base;
		glm::vec3 offset;
		glm::vec3 center;
		float heading;
		glm::vec3 up;
		/* Which of the two textures (the original picks the second with probability one half, decompile). */
		bool textureB;
		int counter = 0;
		float scale = 1.0f;
		float alpha = 1.0f;
	};

	void load();

	std::vector<Sphere> spheres;
	BasicMesh mesh;
	Texture textureA = Texture(1, 1, 4);
	Texture textureB = Texture(1, 1, 4);
	Texture solidA = Texture(1, 1, 4);
	Texture solidB = Texture(1, 1, 4);
	bool loaded = false;
};
}
