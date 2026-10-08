#include "PrefabThumbnails.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include "GPU_API.h"
#include "API.h"
#include "Prefab.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

namespace Graphics
{
namespace
{
/* Where the camera looks from, relative to the middle of the sphere: up and to the front (+z, hypothesis: which way the models face is not known), a little to the side. */
const glm::vec3 ViewDirection = glm::normalize(glm::vec3(0.6f, 0.45f, 1.0f));

/* The picture's background: linear values like the cubes' (the target is sRGB, see RenderTarget), a dark blue grey on the screen. */
const glm::vec4 Background(0.03f, 0.033f, 0.04f, 1.0f);

/* The cube's half size, and the radius of the sphere around it. */
constexpr float CubeHalf = 1.0f;
const float CubeRadius = CubeHalf * std::sqrt(3.0f);

/* The cube colours are linear values (the target is sRGB, they are encoded when written): about 0.2 is a dark colour on the screen. */
constexpr int CubeBlue = 0;
constexpr int CubeRed = 1;
constexpr int CubePurple = 2;
constexpr int CubeGrey = 3;

int cubeOf(const Prefab& rPrefab_)
{
	if(rPrefab_.hasCollision && rPrefab_.hasBehaviour) return CubePurple;
	if(rPrefab_.hasCollision) return CubeBlue;
	if(rPrefab_.hasBehaviour) return CubeRed;
	return CubeGrey;
}
}

void PrefabThumbnails::makeCube(int index_, const char* name_, const glm::vec3& rColor_)
{
	/*
	Six faces of four vertices, wound counter-clockwise seen from outside: each face is a normal n and two axes u, v with u x v = n, its corners n-u-v, n+u-v, n+u+v, n-u+v. The faces are shaded by a factor of the colour (a fixed light from the top
	front), so that the cube reads as a cube.
	*/
	struct Face
	{
		glm::vec3 n;
		glm::vec3 u;
		glm::vec3 v;
		float shade;
	};
	const Face faces[6] = {
		{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 1.0f},
		{{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 0.7f},
		{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, 1.6f},
		{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, 0.45f},
		{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 1.25f},
		{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}, 0.8f}
	};
	PoolId id = basicMeshes.add(name_);
	BasicMesh& rMesh = basicMeshes[id];
	for(const Face& rFace : faces)
	{
		uint32_t first = static_cast<uint32_t>(rMesh.verticies.size());
		glm::vec4 color(rColor_ * rFace.shade, 1.0f);
		const float us[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
		const float vs[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
		for(int corner = 0; corner < 4; corner++)
		{
			glm::vec3 position = (rFace.n + rFace.u * us[corner] + rFace.v * vs[corner]) * CubeHalf;
			rMesh.verticies.push_back(Vertex{position, rFace.n, {0.0f, 0.0f}, color});
		}
		rMesh.indicies.insert(rMesh.indicies.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
	}
	rMesh.create();
	this->cubeMeshes[index_] = id;
}

void PrefabThumbnails::init()
{
	this->target.create(Size, Size);
	this->camera = Camera(glm::vec3(0.0f));
	this->camera.updateProjection(1.0f);
	this->makeCube(CubeBlue, "PrefabCube/Blue", glm::vec3(0.003f, 0.012f, 0.2f));
	this->makeCube(CubeRed, "PrefabCube/Red", glm::vec3(0.2f, 0.01f, 0.01f));
	this->makeCube(CubePurple, "PrefabCube/Purple", glm::vec3(0.1f, 0.01f, 0.16f));
	this->makeCube(CubeGrey, "PrefabCube/Grey", glm::vec3(0.06f, 0.06f, 0.06f));
}

void PrefabThumbnails::beginFrame()
{
	this->madeThisFrame = 0;
}

GLuint PrefabThumbnails::get(PoolId prefabId_, bool prefetch_)
{
	this->usedThisFrame.insert(prefabId_.id);
	auto found = this->cache.find(prefabId_.id);
	if(found != this->cache.end()) return found->second;
	if(this->madeThisFrame >= (prefetch_ ? PerFrame / 2 : PerFrame)) return 0;
	this->madeThisFrame++;
	GLuint texture = this->render(prefabId_);
	this->cache.emplace(prefabId_.id, texture);
	return texture;
}

void PrefabThumbnails::endFrame()
{
	for(auto it = this->cache.begin(); it != this->cache.end();)
	{
		if(this->usedThisFrame.contains(it->first))
		{
			++it;
			continue;
		}
		glDeleteTextures(1, &it->second);
		it = this->cache.erase(it);
	}
	this->usedThisFrame.clear();
}

/*
Draws one prefab into the target and copies it. The entity (if the prefab has a model) is made at the origin by spawnPrefab with its own defaults, except that an SADX key with only variant models (no model of its own) uses its first variant; one that can't be made
(spawnPrefab reports why on stderr) is drawn as a cube. update(0) poses it at the start of its motion. The state it changes (the framebuffer, the viewport, the clear colour) is put back for the rest of the frame.
*/
GLuint PrefabThumbnails::render(PoolId prefabId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	PoolId entityId = UninitializedPoolId;
	if(rPrefab.hasModel)
	{
		SpawnParams params;
		params.what = "(thumbnail)";
		if(rPrefab.kind == PrefabKind::SAObject && rPrefab.saKey->model.rel == nullptr)
		{
			for(const SAModelKey& rVariant : rPrefab.saKey->variantModels)
			{
				if(rVariant.rel != nullptr)
				{
					params.pModel = &rVariant;
					break;
				}
			}
		}
		entityId = spawnPrefab(prefabId_, getShaderId(rPrefab.game == SA_NINJA ? "SA" : "STH2005"), params, &this->entities);
	}

	return this->draw(entityId, cubeOf(rPrefab));
}

/*
A model of a disc as a picture, with no prefab: source_ is a catalog source of the AssetPanel. SADX "<rel> @ 0x<offset>" is the Ninja model whose root NJS_OBJECT is at that offset of the .rel (importSAModel), drawn untextured (which texture archive goes with a
model is not known); Shadow "<archive> > <entry>.DFF" is a static model (a part with no .BON, see importModel), drawn with the .TXD of the same name in the same archive if textured_ (the caller found the entry there; hypothesis: that is how the stage objects are paired) and untextured if not.
The entity is made in the pool of its own and removed after the draw, like a prefab's. Returns 0 if it can't be drawn (a land table, which is not a model, another file type, or an importer throwing, which is reported on stderr).
*/
GLuint PrefabThumbnails::renderModel(int game_, const std::string& source_, bool textured_)
{
	PoolId entityId = UninitializedPoolId;
	try
	{
		PoolId shaderId = getShaderId(game_ == 0 ? "SA" : "STH2005");
		entityId = addEntity("(thumbnail)", glm::vec3(0.0f), &this->entities);
		if(game_ == 0)
		{
			size_t split = source_.find(" @ 0x");
			if(split == std::string::npos || source_.find("land table", split) != std::string::npos)
			{
				this->entities.remove(entityId);
				return 0;
			}
			SAModelKey key{this->keep(source_.substr(0, split)), static_cast<uint32_t>(std::stoul(source_.substr(split + 5), nullptr, 16))};
			this->entities[entityId].setSAGPUInstance(shaderId, key);
		}
		else
		{
			size_t split = source_.find(" > ");
			if(split == std::string::npos)
			{
				this->entities.remove(entityId);
				return 0;
			}
			const char* pArchive = this->keep(source_.substr(0, split));
			std::string entry = source_.substr(split + 3);
			STH2005::ModelKey key{{pArchive}, this->keep("(thumbnail)/" + source_), {{this->keep(entry), nullptr, nullptr}}};
			std::string textureEntry = entry.substr(0, entry.rfind('.')) + ".TXD";
			if(textured_) this->entities[entityId].setONEGPUInstance(shaderId, key, STH2005::TextureKey{{pArchive}, this->keep(textureEntry)});
			else this->entities[entityId].setONEGPUInstance(shaderId, key);
		}
	}
	catch(const std::exception& rError_)
	{
		std::fprintf(stderr, "PrefabThumbnails: %s: %s\n", source_.c_str(), rError_.what());
		if(entityId != UninitializedPoolId) this->entities.remove(entityId);
		return 0;
	}
	return this->draw(entityId, CubeGrey);
}

/* The text as a string that lives for the rest of the run (a mesh keeps the strings of the key it was made from); the same text gives the same string. */
const char* PrefabThumbnails::keep(const std::string& text_)
{
	return this->kept.insert(text_).first->c_str();
}

/*
Draws the entity (UninitializedPoolId: a cube of colour cube_ instead) into the target from the fixed direction at the distance where its bounding sphere fills the view, copies the target into a new texture, removes the entity from the pool and puts back the state
it changed (the framebuffer, the viewport, the clear colour).
*/
GLuint PrefabThumbnails::draw(PoolId entityId_, int cube_)
{
	PoolId entityId = entityId_;
	glm::vec3 center(0.0f);
	float radius = CubeRadius;
	if(entityId != UninitializedPoolId)
	{
		Entity& rEntity = this->entities[entityId];
		rEntity.update(0.0f);
		center = rEntity.bounds.center;
		radius = rEntity.bounds.radius > 0.0f ? rEntity.bounds.radius : 1.0f;
	}

	float distance = radius / std::sin(glm::radians(this->camera.fov) * 0.5f) * 1.05f;
	this->camera.position = center + ViewDirection * distance;
	this->camera.front = -ViewDirection;
	this->camera.nearPlane = std::max(distance - radius * 1.5f, 0.01f);
	this->camera.farPlane = distance + radius * 2.0f;
	this->camera.updateView();
	this->camera.updateProjection(1.0f);
	this->camera.updateUBO();

	this->target.bind();
	glClearColor(Background.r, Background.g, Background.b, Background.a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	if(entityId != UninitializedPoolId)
	{
		renderer.drawEntity(this->camera, this->entities[entityId]);
		this->entities.remove(entityId);
	}
	else
	{
		/* The cube, with the Default shader (vertex colours), drawn the way the debug floor was. */
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, this->camera.UBOId);
		Shader& rShader = getShaderByName("Default");
		glUseProgram(rShader.id);
		glm::mat4 identity(1.0f);
		glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(identity));
		basicMeshes[this->cubeMeshes[cube_]].draw();
	}

	GLuint texture = 0;
	glCreateTextures(GL_TEXTURE_2D, 1, &texture);
	glTextureStorage2D(texture, 1, GL_RGBA8, Size, Size);
	glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glCopyImageSubData(this->target.displayTexture, GL_TEXTURE_2D, 0, 0, 0, 0, texture, GL_TEXTURE_2D, 0, 0, 0, 0, Size, Size, 1);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, Globals::App::windowWidth, Globals::App::windowHeight);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	return texture;
}

void PrefabThumbnails::forgetExternal()
{
	const auto& rPrefabs = Globals::Game::prefabs;
	for(auto it = this->cache.begin(); it != this->cache.end();)
	{
		bool alive = it->first < rPrefabs.objects.size() && rPrefabs.alive[it->first];
		if(alive && rPrefabs.objects[it->first].kind != PrefabKind::External)
		{
			++it;
			continue;
		}
		glDeleteTextures(1, &it->second);
		it = this->cache.erase(it);
	}
}

void PrefabThumbnails::destroy()
{
	for(auto& rEntry : this->cache) glDeleteTextures(1, &rEntry.second);
	this->cache.clear();
	this->entities.clear();
	this->target.destroy();
	this->camera.destroy();
}
}
