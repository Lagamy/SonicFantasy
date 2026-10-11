#pragma once
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include "PoolId.h"
#include "BasicMesh.h"
#include "Texture.h"

namespace Graphics
{
/*
The blue trail of SADX Sonic's roll and homing flight (docs/SADX_SONIC_REFERENCE.md "Trail geometry and draw state": what is decompile, dump or hypothesis).
One Trail is one tick's displacement of his body centre: a hexagonal tube of radius 4 laid from the entity's position of this tick (plus the offset given to `spawn`) to its position one tick before (the offset's position at the making), stretched to that
length, fixed in the world and living 20 ticks. The texture is a brightness ramp along the tube that changes every tick (the original's s_dash01..12: lead-in, full blue for nine ticks, fade). Made every tick, the trails form a continuous ribbon.
Not ported: the far ring's re-aim on the second tick (the tube stays on its two points), the second texture list (the original's variant flag, selected by player bits not decoded).
`draw` (Renderer::run, after the dash spheres) draws every trail unlit and additive (external format reading of the material word), no depth write.
*/
struct SonicTrail
{
	/* Starts a trail at the entity's position plus rOffset_ now; its other end is the same point on the next `tick`. Loads the mesh and textures on the first call. */
	void spawn(PoolId entityId_, const glm::vec3& rOffset_);

	/* One gameplay tick (after the physics): fixes the new trails' tubes, ages every trail, removes the finished ones. */
	void tick();

	void draw();
	void destroy();

private:
	static constexpr int TextureCount = 12;

	struct Trail
	{
		PoolId entityId;
		glm::vec3 offset;
		glm::vec3 start;
		glm::mat4 model;
		int counter = 0;
	};

	void load();

	std::vector<Trail> trails;
	BasicMesh mesh;
	std::array<Texture, TextureCount> textures = {Texture(1, 1, 4), Texture(1, 1, 4), Texture(1, 1, 4), Texture(1, 1, 4), Texture(1, 1, 4), Texture(1, 1, 4),
		Texture(1, 1, 4), Texture(1, 1, 4), Texture(1, 1, 4), Texture(1, 1, 4), Texture(1, 1, 4), Texture(1, 1, 4)};
	bool loaded = false;
};
}
