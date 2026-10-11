#include "DashSpheres.h"
#include "GPU_API.h"
#include "Globals.h"
#include "API.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
/*
The sphere (dump of the original's object 0x80bf6108, model 0x80bf60e0: 52 vertices, positions at 0x80bf5c00): radius 4.5, a pole at y = -4.5 and one at +4.5, five rings of ten at y = -3.897, -2.25, 0, 2.25, 3.897 (latitudes 0, +-30, +-60 degrees from the equator),
meridian 0 on the +x side and the next ones turned toward -z. Triangles are generated here; the original's index lists were not decoded (the shape is the same).
*/
constexpr float SphereRadius = 4.5f;
constexpr int Meridians = 10;
constexpr int Rings = 5;
constexpr float RingLatitudeStepDegrees = 30.0f;

/*
Texture coordinates (dump, UV array at 0x80bf58c0, 8-bit fixed point): s = (y + 4.5) / 9 and t = (x + 4.5) / 9 of the 0..255 range, checked on the vertices of three rings (for example vertex 2 at (2.25, -3.897, 0) has (0x11, 0xbf), the pole (0xff, 0x7f)). The texture's
colour changes along t only, so the sphere is a gradient along its own x axis, black at -x and blue at +x.
*/
constexpr float TexelScale = 255.0f / 256.0f;

/*
The two textures (dump: s_ball_a pixels at 0x8117e8c0, s_ball_b at 0x8117e9e0, 16x16 CMPR, decoded by a scratchpad script; every row is one colour and no texel is transparent): row 0 is the top, t = 0. Red, green, blue per row.
*/
constexpr int TextureSize = 16;
constexpr uint8_t RowsA[TextureSize][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 8}, {0, 0, 16}, {0, 0, 24}, {0, 0, 43}, {0, 0, 62}, {0, 0, 82}, {0, 0, 123}, {0, 0, 123}, {0, 4, 147}, {0, 8, 172}, {0, 12, 197}, {0, 20, 224}, {0, 24, 238}, {0, 24, 238}};
constexpr uint8_t RowsB[TextureSize][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 8}, {0, 0, 32}, {0, 0, 32}, {2, 0, 43}, {8, 0, 65}, {8, 0, 90}, {8, 0, 90}, {10, 0, 103}, {16, 0, 131}, {16, 0, 148}, {16, 0, 161}, {16, 0, 175}, {16, 0, 189}};

/*
The model's first material (dump 0x80bf5700): diffuse 0xb2b2b2ff, the second colour 0xffffffff. Hypothesis: with the lights ignored the diffuse grey multiplies the texture, so every colour is drawn at 0xb2 / 255.
*/
constexpr float MaterialGrey = 178.0f / 255.0f;

/*
The burst (decompile FUN_8059dd30 / FUN_8059cc30): scale 3.0 at the making, minus 0.4 after every drawn tick; it is drawn at counters 0 to 10 and ends when the counter is past 9 at its tick, with the alpha offset -counter / 10. The scale goes
through zero and negative in the last ticks (3.0 - 0.4 * 9 = -0.6 at counter 10, where the alpha is 0 already); kept as the original has it, hypothesis that the draw takes a negative scale as it is.
*/
constexpr float BurstStartScale = 3.0f;
constexpr float BurstScaleStep = 0.4f;
constexpr int BurstTicks = 10;

/* The aura (decompile FUN_8059dfb0 / FUN_8059cec0): drawn at counters 0 to 5 with the alpha offset -counter / 5; ends at the tick where the counter is past 4. */
constexpr int AuraTicks = 2;
constexpr int ChargeTicks = 5;

/*
The random vector of the aura's and the charge sphere's making, rotated about z then x by angles of a whole turn at random. The charge's length is 0.2 + r (decompile FUN_8059e488). The decompile's aura (FUN_8059dfb0) has 1 + 2 r; the port gives it
a smaller one (observed, user: the aura after the release was fuzzy with the decompile's jitter, then still too loose with the charge's; conflict kept, values chosen by the user's request, not measured).
The aura's tint is full white instead of the material's grey 0xb2 (observed, user: the aura should be more opaque).
The charge's draw (FUN_8059d288): after the frame's rotation, translate (0, -1, 0), a turn of 0xe000 (-45 degrees) about z, scale (0.7, 1.1, 0.8): a flattened, tilted
ellipsoid. Hypotheses: the turn's axis is the model's z (Ninja rotZ) and its sign is the right-handed one of the port's matrices; the frame is the aura's heading turn after the ground normal's alignment (sf.orient's convention).
*/
constexpr float AuraMinLength = 0.05f;
constexpr float AuraLengthSpan = 0.35f;
constexpr float AuraGrey = 1.0f;
constexpr float ChargeMinLength = 0.2f;
constexpr float ChargeLengthSpan = 1.0f;
constexpr float ChargeDrop = -1.0f;
constexpr float ChargeTurnDegrees = -45.0f;
static const glm::vec3 ChargeScale = glm::vec3(0.7f, 1.1f, 0.8f);

/* A random number in [0, 1): the original's rand() / 32768, not its sequence (a fixed seed is not a goal). */
static float unit()
{
	return static_cast<float>(std::rand()) / (static_cast<float>(RAND_MAX) + 1.0f);
}

/* The 16x16 texture of one of the two row tables: linear filter, no mipmaps (hypothesis: the original's default filter), colour from the rows, alpha 255. */
static Texture makeTexture(const uint8_t (&rRows_)[TextureSize][3])
{
	Texture texture(TextureSize, TextureSize, 4);
	for(int y = 0; y < TextureSize; y++)
	{
		for(int x = 0; x < TextureSize; x++)
		{
			unsigned char* pPixel = texture.pixels.data() + (static_cast<size_t>(y) * TextureSize + x) * 4;
			pPixel[0] = rRows_[y][0];
			pPixel[1] = rRows_[y][1];
			pPixel[2] = rRows_[y][2];
			pPixel[3] = 255;
		}
	}
	texture.filter = GL_LINEAR;
	texture.mipmaps = false;
	return texture;
}

/*
The charge's and the aura's one colour: the last row of a table (the blue end of the gradient), as a 1x1 texture. Observed (user): the charge and the aura after the spin dash's release are one flat colour, transparent, not a gradient; the decompile's draw uses
the same textured sphere (a conflict, kept visible in docs/SADX_SONIC_REFERENCE.md). Which colour is unknown: hypothesis, the gradient's blue end.
*/
static Texture makeSolidTexture(const uint8_t (&rRows_)[TextureSize][3])
{
	Texture texture(1, 1, 4);
	texture.pixels[0] = rRows_[TextureSize - 1][0];
	texture.pixels[1] = rRows_[TextureSize - 1][1];
	texture.pixels[2] = rRows_[TextureSize - 1][2];
	texture.pixels[3] = 255;
	texture.filter = GL_LINEAR;
	texture.mipmaps = false;
	return texture;
}

void DashSpheres::load()
{
	std::vector<Vertex> vertices;
	auto point = [&](const glm::vec3& rPosition_)
	{
		Vertex vertex;
		vertex.position = rPosition_;
		vertex.normal = rPosition_ / SphereRadius;
		vertex.uv = glm::vec2((rPosition_.y + SphereRadius) / (2.0f * SphereRadius), (rPosition_.x + SphereRadius) / (2.0f * SphereRadius)) * TexelScale;
		vertex.color = glm::vec4(1.0f);
		vertices.push_back(vertex);
	};
	point(glm::vec3(0.0f, -SphereRadius, 0.0f));
	point(glm::vec3(0.0f, SphereRadius, 0.0f));
	for(int r = 0; r < Rings; r++)
	{
		float latitude = glm::radians((r - Rings / 2) * RingLatitudeStepDegrees);
		for(int m = 0; m < Meridians; m++)
		{
			float longitude = glm::radians(360.0f / Meridians * m);
			point(SphereRadius * glm::vec3(std::cos(latitude) * std::cos(longitude), std::sin(latitude), -std::cos(latitude) * std::sin(longitude)));
		}
	}

	/* Every triangle is turned, if need be, so that its normal points away from the center (counter-clockwise seen from outside, the port's front face). */
	std::vector<uint32_t> indices;
	auto triangle = [&](uint32_t a_, uint32_t b_, uint32_t c_)
	{
		glm::vec3 a = vertices[a_].position;
		glm::vec3 b = vertices[b_].position;
		glm::vec3 c = vertices[c_].position;
		if(glm::dot(glm::cross(b - a, c - a), a + b + c) < 0.0f) std::swap(b_, c_);
		indices.insert(indices.end(), {a_, b_, c_});
	};
	auto ringVertex = [](int r_, int m_) { return static_cast<uint32_t>(2 + r_ * Meridians + (m_ % Meridians)); };
	for(int m = 0; m < Meridians; m++)
	{
		triangle(0, ringVertex(0, m), ringVertex(0, m + 1));
		triangle(1, ringVertex(Rings - 1, m), ringVertex(Rings - 1, m + 1));
		for(int r = 0; r + 1 < Rings; r++)
		{
			triangle(ringVertex(r, m), ringVertex(r + 1, m), ringVertex(r, m + 1));
			triangle(ringVertex(r, m + 1), ringVertex(r + 1, m), ringVertex(r + 1, m + 1));
		}
	}

	this->mesh.verticies = vertices;
	this->mesh.indicies = indices;
	this->mesh.create();
	this->textureA = makeTexture(RowsA);
	this->textureA.create();
	this->textureB = makeTexture(RowsB);
	this->textureB.create();
	this->solidA = makeSolidTexture(RowsA);
	this->solidA.create();
	this->solidB = makeSolidTexture(RowsB);
	this->solidB.create();
	this->loaded = true;
}

void DashSpheres::spawn(PoolId entityId_, Kind kind_, const glm::vec3& rOffset_, float heading_, const glm::vec3& rUp_)
{
	if(!entityExists(entityId_)) return;
	if(!this->loaded) this->load();

	Sphere sphere;
	sphere.entityId = entityId_;
	sphere.kind = kind_;
	sphere.base = Globals::Game::entities[entityId_].position;
	sphere.offset = rOffset_;
	sphere.heading = heading_;
	sphere.up = rUp_;
	sphere.textureB = unit() <= 0.5f;
	if(kind_ == Burst) sphere.scale = BurstStartScale;
	else
	{
		float length = kind_ == Charge ? ChargeMinLength + ChargeLengthSpan * unit() : AuraMinLength + AuraLengthSpan * unit();
		glm::mat4 turn = glm::rotate(glm::mat4(1.0f), 2.0f * std::numbers::pi_v<float> * unit(), glm::vec3(0.0f, 0.0f, 1.0f));
		turn = glm::rotate(turn, 2.0f * std::numbers::pi_v<float> * unit(), glm::vec3(1.0f, 0.0f, 0.0f));
		sphere.offset += glm::vec3(turn * glm::vec4(0.0f, length, 0.0f, 1.0f));
	}
	sphere.center = sphere.base + sphere.offset;
	this->spheres.push_back(sphere);
}

void DashSpheres::tick()
{
	for(Sphere& rSphere : this->spheres)
	{
		rSphere.counter++;
		if(rSphere.kind == Burst)
		{
			rSphere.scale = BurstStartScale - BurstScaleStep * static_cast<float>(std::max(rSphere.counter - 1, 0));
			rSphere.alpha = 1.0f - static_cast<float>(rSphere.counter) / static_cast<float>(BurstTicks);
			continue;
		}
		/*
		The charge is drawn at the entity's position of this tick (decompile FUN_8059e488). The aura is drawn where the entity was at the previous tick: the position is copied after the draw (decompile FUN_8059dfb0). Its life is 2 ticks, shorter than the decompile's 5 (observed, user: the afterimage lasts too long and shows as copies).
		*/
		int lifeTicks = rSphere.kind == Charge ? ChargeTicks : AuraTicks;
		if(rSphere.kind == Charge)
		{
			if(entityExists(rSphere.entityId)) rSphere.base = Globals::Game::entities[rSphere.entityId].position;
			rSphere.center = rSphere.base + rSphere.offset;
		}
		else
		{
			rSphere.center = rSphere.base + rSphere.offset;
			if(entityExists(rSphere.entityId)) rSphere.base = Globals::Game::entities[rSphere.entityId].position;
		}
		rSphere.alpha = 1.0f - static_cast<float>(rSphere.counter) / static_cast<float>(lifeTicks);
	}
	std::erase_if(this->spheres, [](const Sphere& rSphere_)
	{
		return rSphere_.counter > (rSphere_.kind == Burst ? BurstTicks : rSphere_.kind == Charge ? ChargeTicks : AuraTicks) || (rSphere_.kind != Burst && !entityExists(rSphere_.entityId));
	});
}

/*
Every sphere is its own draw with the STH2005 shader unskinned (the effect balls' path): model = translate(center) * turn about y so that the model's x axis points along the heading (aura) or, for the charge, the frame (alignment of +y to the ground normal, then that turn) * translate(0, -1, 0) * turn about z * scale (0.7, 1.1, 0.8); the texture on unit 0; effectTint = the material grey
and the sphere's alpha; additive blend (source alpha, destination one), depth tested, not written, culled like the other models. The tint is put back to no change after the draw.
*/
void DashSpheres::draw()
{
	if(this->spheres.empty()) return;
	Shader& rShader = getShaderByName("STH2005");
	glUseProgram(rShader.id);
	glUniform1i(SkinnedLocation, 0);
	glUniform1i(HasTextureLocation, 1);
	glUniform2f(UvOffsetLocation, 0.0f, 0.0f);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDepthMask(GL_FALSE);

	for(const Sphere& rSphere : this->spheres)
	{
		float alpha = std::clamp(rSphere.alpha, 0.0f, 1.0f);
		if(alpha <= 0.0f) continue;
		glm::mat4 model = glm::translate(glm::mat4(1.0f), rSphere.center);
		if(rSphere.kind == Aura) model = glm::rotate(model, -rSphere.heading, glm::vec3(0.0f, 1.0f, 0.0f));
		if(rSphere.kind == Charge)
		{
			glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), rSphere.up);
			float axisLength = glm::length(axis);
			if(axisLength > 1e-6f) model = glm::rotate(model, std::atan2(axisLength, rSphere.up.y), axis / axisLength);
			else if(rSphere.up.y < 0.0f) model = glm::rotate(model, std::numbers::pi_v<float>, glm::vec3(1.0f, 0.0f, 0.0f));
			model = glm::rotate(model, -rSphere.heading, glm::vec3(0.0f, 1.0f, 0.0f));
			model = glm::translate(model, glm::vec3(0.0f, ChargeDrop, 0.0f));
			model = glm::rotate(model, glm::radians(ChargeTurnDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
			model = glm::scale(model, ChargeScale);
		}
		else model = glm::scale(model, glm::vec3(rSphere.scale));
		glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(model));
		float grey = rSphere.kind == Aura ? AuraGrey : MaterialGrey;
		glUniform4f(EffectTintLocation, grey, grey, grey, alpha);
		if(rSphere.kind != Burst) glBindTextureUnit(0, rSphere.textureB ? this->solidB.textureId : this->solidA.textureId);
		else glBindTextureUnit(0, rSphere.textureB ? this->textureB.textureId : this->textureA.textureId);
		this->mesh.draw();
	}

	glUniform4f(EffectTintLocation, 1.0f, 1.0f, 1.0f, 1.0f);
	glDepthMask(GL_TRUE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void DashSpheres::destroy()
{
	this->spheres.clear();
	if(!this->loaded) return;
	this->mesh.destroy();
	this->textureA.destroy();
	this->textureB.destroy();
	this->solidA.destroy();
	this->solidB.destroy();
	this->loaded = false;
}
}
