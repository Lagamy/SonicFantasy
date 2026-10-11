#include "SonicTrail.h"
#include "GPU_API.h"
#include "Globals.h"
#include "API.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
/*
The tube (dump of the original's template object 0x80bf56c8, model 0x80bf56a0, vertices at 0x80bf5580): 12 vertices, index i on the hexagon point i / 2 and at x = 8 (even i) or x = 0 (odd i); the hexagon of circumradius 4 in the yz plane, points
(y, z) = (4, 0), (2, -3.464), (-2, -3.464), (-4, 0), (-2, 3.464), (2, 3.464). The original draws one 14-index strip; the six quads are generated here (the same surface).
Texture coordinates (dump, 8-bit fixed point, hypothesis for the divisor 255 as in DashSpheres): u = 0 at x = 0 and 239 at x = 8, v = (135 - 30 y) / 255.
*/
constexpr float TubeLength = 8.0f;
constexpr float TubeRadius = 4.0f;
constexpr int HexagonPoints = 6;
constexpr float UEnd = 239.0f / 255.0f;

/*
The trail task's life (decompile FUN_8059c148): the tube is fixed on the first tick, shown for counters 1 to 20, and it ends at once when its length over 8 is 5.0 or more.
*/
constexpr int TrailTicks = 20;
constexpr float MaxScale = 5.0f;

/*
The texture of each counter, first list (dump 0x807542f4, bit 3 of the task's flag block clear): counter 1 s_dash01, 2 s_dash02, 3 to 11 s_dash03, 12 to 20 s_dash04 to s_dash12.
*/
static int textureForCounter(int counter_)
{
	if(counter_ <= 2) return counter_ - 1;
	if(counter_ <= 11) return 2;
	return counter_ - 9;
}

/*
The twelve textures (dump, CMPR 16x16 decoded by a scratchpad script; every row is the same, no texel is transparent, green is 0): red and blue of each of the 16 columns, u from left to right. s_dash01 .. s_dash12.
*/
constexpr int RampWidth = 16;
constexpr uint8_t Ramps[12][RampWidth][2] = {
	{{0, 0}, {0, 0}, {0, 8}, {0, 16}, {0, 24}, {0, 32}, {0, 40}, {0, 49}, {0, 57}, {0, 57}, {0, 65}, {0, 74}, {0, 82}, {0, 90}, {0, 98}, {0, 98}},
	{{0, 106}, {0, 117}, {0, 128}, {0, 139}, {0, 148}, {0, 156}, {0, 164}, {0, 172}, {0, 180}, {0, 191}, {0, 202}, {0, 213}, {0, 222}, {0, 230}, {0, 238}, {0, 246}},
	{{0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}, {0, 255}},
	{{0, 255}, {0, 255}, {0, 246}, {0, 246}, {0, 246}, {0, 246}, {0, 238}, {0, 238}, {0, 238}, {0, 238}, {0, 238}, {0, 230}, {0, 230}, {0, 230}, {0, 221}, {0, 221}},
	{{0, 222}, {0, 222}, {0, 222}, {0, 213}, {0, 213}, {0, 213}, {0, 205}, {0, 205}, {0, 205}, {0, 205}, {0, 205}, {0, 197}, {0, 197}, {0, 197}, {0, 197}, {0, 197}},
	{{0, 189}, {0, 189}, {0, 189}, {8, 189}, {8, 189}, {8, 189}, {8, 189}, {8, 189}, {8, 184}, {8, 184}, {8, 184}, {16, 180}, {16, 180}, {16, 180}, {16, 180}, {16, 180}},
	{{16, 180}, {16, 180}, {16, 180}, {16, 180}, {16, 180}, {16, 172}, {16, 172}, {16, 172}, {16, 172}, {16, 172}, {16, 172}, {16, 172}, {16, 164}, {16, 164}, {16, 164}, {16, 164}},
	{{16, 164}, {16, 164}, {16, 156}, {16, 156}, {16, 156}, {16, 156}, {16, 156}, {16, 156}, {16, 148}, {16, 148}, {16, 148}, {16, 148}, {16, 139}, {16, 139}, {16, 139}, {8, 131}},
	{{8, 131}, {8, 131}, {8, 131}, {8, 131}, {8, 123}, {8, 123}, {8, 123}, {8, 123}, {8, 123}, {8, 123}, {8, 115}, {8, 115}, {8, 115}, {8, 115}, {8, 106}, {8, 106}},
	{{8, 106}, {8, 106}, {8, 98}, {8, 98}, {8, 98}, {8, 98}, {8, 98}, {8, 90}, {8, 90}, {8, 90}, {8, 90}, {8, 82}, {8, 82}, {8, 82}, {8, 82}, {8, 74}},
	{{8, 74}, {8, 74}, {8, 74}, {8, 65}, {8, 65}, {8, 65}, {8, 65}, {8, 57}, {8, 57}, {8, 57}, {8, 49}, {8, 49}, {8, 46}, {8, 46}, {8, 46}, {0, 41}},
	{{0, 41}, {0, 32}, {0, 32}, {0, 32}, {0, 24}, {0, 24}, {0, 24}, {0, 24}, {0, 16}, {0, 16}, {0, 16}, {0, 8}, {0, 8}, {0, 8}, {0, 0}, {0, 0}}};

/* One ramp as a 16x1 texture (the rows of the original's are equal): linear filter, no mipmaps (hypothesis: the original's default filter). */
static Texture makeRamp(const uint8_t (&rRamp_)[RampWidth][2])
{
	Texture texture(RampWidth, 1, 4);
	for(int x = 0; x < RampWidth; x++)
	{
		unsigned char* pPixel = texture.pixels.data() + static_cast<size_t>(x) * 4;
		pPixel[0] = rRamp_[x][0];
		pPixel[1] = 0;
		pPixel[2] = rRamp_[x][1];
		pPixel[3] = 255;
	}
	texture.filter = GL_LINEAR;
	texture.mipmaps = false;
	return texture;
}

void SonicTrail::load()
{
	std::vector<Vertex> vertices;
	const float hexagonY[HexagonPoints] = {4.0f, 2.0f, -2.0f, -4.0f, -2.0f, 2.0f};
	const float hexagonZ[HexagonPoints] = {0.0f, -3.464f, -3.464f, 0.0f, 3.464f, 3.464f};
	for(int i = 0; i < 2 * HexagonPoints; i++)
	{
		bool far = i % 2 == 0;
		Vertex vertex;
		vertex.position = glm::vec3(far ? TubeLength : 0.0f, hexagonY[i / 2], hexagonZ[i / 2]);
		vertex.normal = glm::vec3(0.0f, hexagonY[i / 2], hexagonZ[i / 2]) / TubeRadius;
		vertex.uv = glm::vec2(far ? UEnd : 0.0f, (135.0f - 30.0f * hexagonY[i / 2]) / 255.0f);
		vertex.color = glm::vec4(1.0f);
		vertices.push_back(vertex);
	}

	/* Every triangle is turned, if need be, so that its normal points away from the axis (counter-clockwise seen from outside, the port's front face). */
	std::vector<uint32_t> indices;
	auto triangle = [&](uint32_t a_, uint32_t b_, uint32_t c_)
	{
		glm::vec3 a = vertices[a_].position;
		glm::vec3 b = vertices[b_].position;
		glm::vec3 c = vertices[c_].position;
		glm::vec3 outward = glm::vec3(0.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f);
		if(glm::dot(glm::cross(b - a, c - a), outward) < 0.0f) std::swap(b_, c_);
		indices.insert(indices.end(), {a_, b_, c_});
	};
	for(int k = 0; k < HexagonPoints; k++)
	{
		uint32_t farA = static_cast<uint32_t>(2 * k);
		uint32_t nearA = farA + 1;
		uint32_t farB = static_cast<uint32_t>(2 * ((k + 1) % HexagonPoints));
		uint32_t nearB = farB + 1;
		triangle(farA, nearA, farB);
		triangle(nearA, nearB, farB);
	}

	this->mesh.verticies = vertices;
	this->mesh.indicies = indices;
	this->mesh.create();
	for(int i = 0; i < TextureCount; i++)
	{
		this->textures[i] = makeRamp(Ramps[i]);
		this->textures[i].create();
	}
	this->loaded = true;
}

void SonicTrail::spawn(PoolId entityId_, const glm::vec3& rOffset_)
{
	if(!entityExists(entityId_)) return;
	if(!this->loaded) this->load();

	Trail trail;
	trail.entityId = entityId_;
	trail.offset = rOffset_;
	trail.start = Globals::Game::entities[entityId_].position + rOffset_;
	trail.model = glm::mat4(1.0f);
	this->trails.push_back(trail);
}

void SonicTrail::tick()
{
	for(Trail& rTrail : this->trails)
	{
		rTrail.counter++;
		if(rTrail.counter != 1) continue;
		if(!entityExists(rTrail.entityId))
		{
			rTrail.counter = TrailTicks + 1;
			continue;
		}

		/*
		Counter 1 (decompile FUN_8059c148): the new point P1 is the entity's centre now, the tube runs from it toward the old point: yaw = atan2(-dz, dx), pitch = asin(dy / length) of the delta old - P1, drawn at P1 turned about y then z and scaled by
		length / 8 along its x. A zero-length segment draws nothing; a length / 8 of 5.0 or more ends the trail at once.
		*/
		glm::vec3 p1 = Globals::Game::entities[rTrail.entityId].position + rTrail.offset;
		glm::vec3 delta = rTrail.start - p1;
		float length = glm::length(delta);
		if(length < 1e-6f || length / TubeLength >= MaxScale)
		{
			rTrail.counter = TrailTicks + 1;
			continue;
		}
		float yaw = std::atan2(-delta.z, delta.x);
		float pitch = std::asin(std::clamp(delta.y / length, -1.0f, 1.0f));
		glm::mat4 model = glm::translate(glm::mat4(1.0f), p1);
		model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
		model = glm::rotate(model, pitch, glm::vec3(0.0f, 0.0f, 1.0f));
		rTrail.model = glm::scale(model, glm::vec3(length / TubeLength, 1.0f, 1.0f));
	}
	std::erase_if(this->trails, [](const Trail& rTrail_) { return rTrail_.counter > TrailTicks; });
}

/*
Every trail is its own draw with the STH2005 shader unskinned (the sphere's path): the texture of its counter on unit 0, the tint white (the material's diffuse is 0xffffffff), additive blend (source alpha, destination one; the alpha is 1), depth tested,
not written. The tint and blend are put back after the draws.
*/
void SonicTrail::draw()
{
	if(this->trails.empty()) return;
	Shader& rShader = getShaderByName("STH2005");
	glUseProgram(rShader.id);
	glUniform1i(SkinnedLocation, 0);
	glUniform1i(HasTextureLocation, 1);
	glUniform2f(UvOffsetLocation, 0.0f, 0.0f);
	glUniform4f(EffectTintLocation, 1.0f, 1.0f, 1.0f, 1.0f);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDepthMask(GL_FALSE);

	for(const Trail& rTrail : this->trails)
	{
		if(rTrail.counter < 1 || rTrail.counter > TrailTicks) continue;
		glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(rTrail.model));
		glBindTextureUnit(0, this->textures[textureForCounter(rTrail.counter)].textureId);
		this->mesh.draw();
	}

	glDepthMask(GL_TRUE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void SonicTrail::destroy()
{
	this->trails.clear();
	if(!this->loaded) return;
	this->mesh.destroy();
	for(Texture& rTexture : this->textures) rTexture.destroy();
	this->loaded = false;
}
}
