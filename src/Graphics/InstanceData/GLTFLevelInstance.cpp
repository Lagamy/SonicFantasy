#include "GLTFLevelInstance.h"
#include "LevelStreaming.h"
#include "GPU_Globals.h"
#include "RenderStats.h"
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
void GLTFLevelInstance::draw(const glm::mat4& rBase_, const Camera& rCamera_)
{
	Shader& rShader = shaders[this->shaderId];
	renderStats.program(rShader.id);
	glUseProgram(rShader.id);
	glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(rBase_));

	std::array<glm::vec4, 6> planes = rCamera_.frustumPlanes();
	for(GLTFLevelSubmesh& rSubmesh : this->submeshes)
	{
		glm::vec3 center = glm::vec3(rBase_ * glm::vec4(rSubmesh.center, 1.0f));
		LevelPieceAction action = levelPieceAction(rCamera_, planes, center, rSubmesh.radius);
		if(action == LevelPieceAction::Skip) continue;
		if(action == LevelPieceAction::Unload)
		{
			rSubmesh.mesh.destroy();
			continue;
		}
		if(!rSubmesh.mesh.alive) rSubmesh.mesh.create();
		if(action != LevelPieceAction::Draw) continue;

		bool hasTexture = rSubmesh.textureId != UninitializedPoolId;
		if(hasTexture)
		{
			renderStats.texture(textures[rSubmesh.textureId].textureId);
			glBindTextureUnit(0, textures[rSubmesh.textureId].textureId);
		}
		glUniform1i(HasTextureLocation, hasTexture);
		renderStats.levelPiecesDrawn++;
		rSubmesh.mesh.draw();
	}
}

/* Frees the submeshes' GPU buffers and removes the textures the instance made. */
void GLTFLevelInstance::destroy()
{
	for(GLTFLevelSubmesh& rSubmesh : this->submeshes) rSubmesh.mesh.destroy();
	this->submeshes.clear();
	for(PoolId id : this->textureIds) textures.remove(id);
	this->textureIds.clear();
}
}
