#include "Renderer.h"
#include "GPU_Globals.h"
#include "GPU_API.h"
#include "Globals.h"
#include "API.h"
#include "Shader.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>

namespace Graphics 
{
void Renderer::init() 
{ 
// IMPORTANT: GLAD must be initialized AFTER OpenGL context creation
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD");
    }
	// Enable sRGB framebuffer
    glEnable(GL_FRAMEBUFFER_SRGB);
	// Enable depth test 
	glEnable(GL_DEPTH_TEST);
	// Enable back-face culling
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	/*
	Standard alpha blending (source alpha over the framebuffer), computed in linear space since the framebuffer is sRGB.
	Nothing is sorted: translucent surfaces blend correctly only over what was drawn before them (levels first, then
	entities, in pool order).
	*/
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glViewport(0, 0, Globals::App::windowWidth, Globals::App::windowHeight);

	shaders.add("Default", "Default");

	glCreateBuffers(1, &this->boneBuffer);
	glNamedBufferStorage(this->boneBuffer, MaxSkinBones * sizeof(glm::mat4), nullptr, GL_DYNAMIC_STORAGE_BIT);

	/*
	Debug floor: a grey 40 x 40 quad in the XZ plane at y = 0, centered on the origin, as a reference
	for model offsets. Wound counter-clockwise seen from above, so it is visible from +Y and culled from below.
	*/
	BasicMesh& rFloor = basicMeshes[basicMeshes.add("Floor")];
	const float half = 20.0f;
	const glm::vec3 up(0.0f, 1.0f, 0.0f);
	const glm::vec4 grey(0.3f, 0.3f, 0.3f, 1.0f);
	rFloor.verticies = {
		Vertex{{-half, 0.0f, -half}, up, {0.0f, 0.0f}, grey},
		Vertex{{-half, 0.0f,  half}, up, {0.0f, 1.0f}, grey},
		Vertex{{ half, 0.0f,  half}, up, {1.0f, 1.0f}, grey},
		Vertex{{ half, 0.0f, -half}, up, {1.0f, 0.0f}, grey}
	};
	rFloor.indicies = {0, 1, 2, 0, 2, 3};
	rFloor.create();

	collisionView.init();
}

void Renderer::run(Camera& rCamera_) 
{ 
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// bind cameras UBO to binding = 0 (matches shader); must happen before drawing
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, rCamera_.UBOId);

	/* The debug floor is not drawn while a level is shown. */
	// Shader& rDefaultShader = getShaderByName("Default");
	// glUseProgram(rDefaultShader.id);
	// glm::mat4 identity(1.0f);
	// glUniformMatrix4fv(rDefaultShader.modelLocation, 1, GL_FALSE, glm::value_ptr(identity));
	// getBasicMeshByName("Floor").draw();

	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(Globals::Game::levels.alive[i]) Globals::Game::levels.objects[i].draw(rCamera_);
	}

	/* Entities out of the view or farther than the far plane (the render distance) are not drawn. */
	std::array<glm::vec4, 6> planes = rCamera_.frustumPlanes();
	float maxDistance = rCamera_.farPlane;
	Globals::Game::spatialIndex.rebuildIfDirty();
	if(Globals::Game::spatialIndex.built)
	{
		/* The static entities come from the spatial index, already inside the view; the dynamic ones are walked and culled by draw. */
		static std::vector<uint32_t> visible;
		visible.clear();
		Globals::Game::spatialIndex.queryVisible(planes, rCamera_.position, maxDistance, visible);
		for(uint32_t index : visible)
		{
			Globals::Game::entities.objects[index].draw(planes, rCamera_.position, maxDistance);
		}
		Globals::Game::spatialIndex.pruneDynamic();
		for(PoolId entityId : Globals::Game::spatialIndex.dynamicEntities)
		{
			if(entityExists(entityId)) Globals::Game::entities.objects[entityId.id].draw(planes, rCamera_.position, maxDistance);
		}
	}
	else
	{
		for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
		{
			if(Globals::Game::entities.alive[i]) Globals::Game::entities.objects[i].draw(planes, rCamera_.position, maxDistance);
		}
	}

	collisionView.draw(rCamera_);
}
  
void Renderer::destroy() 
{
	collisionView.destroy();
	glDeleteBuffers(1, &this->boneBuffer); 
	oneArchives.clear();
	basicMeshes.clear(); 
	textures.clear();
	shaders.clear(); 
	saMeshes.clear();
	relModules.clear(); 
}

}