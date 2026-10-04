#include "ONELevelInstance.h"
#include "LevelStreaming.h"
#include "ONEInstance.h"
#include "GPU_Globals.h"
#include "RenderStats.h"
#include "ResourceLoader.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
/*
Streams the pieces by the camera (levelPieceAction) and draws them with the instance's shader, like an unskinned atomic of
ONEInstance::draw: model is the base rotated by RenderWareYawDegrees (the vertices are in the world's own space, so there is
one matrix for the whole level), skinned is off, and a textured submesh has its texture bound to unit 0 with hasTexture set.
A piece that is Load or Draw has its GPU buffers created if they are not there, a Draw one is drawn, an Unload one
gives its buffers back (BasicMesh::destroy; the CPU data stays).
*/
void ONELevelInstance::draw(const glm::mat4& rBase_, const Camera& rCamera_)
{
	Shader& rShader = shaders[this->shaderId];
	renderStats.program(rShader.id);
	glUseProgram(rShader.id);
	glm::mat4 base = glm::rotate(rBase_, glm::radians(RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
	glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(base));
	glUniform1i(SkinnedLocation, 0);

	std::array<glm::vec4, 6> planes = rCamera_.frustumPlanes();
	for(ONELevelPiece& rPiece : this->pieces)
	{
		glm::vec3 center = glm::vec3(base * glm::vec4(rPiece.center, 1.0f));
		LevelPieceAction action = levelPieceAction(rCamera_, planes, center, rPiece.radius);

		if(action == LevelPieceAction::Skip) continue;
		for(ONELevelSubmesh& rSubmesh : rPiece.submeshes)
		{
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
}

/* Frees the pieces' GPU buffers and gives back the instance's user of the texture archive. */
void ONELevelInstance::destroy()
{
	for(ONELevelPiece& rPiece : this->pieces)
	{
		for(ONELevelSubmesh& rSubmesh : rPiece.submeshes)
		{
			rSubmesh.mesh.destroy();
		}
	}
	this->pieces.clear();
	Loading::releaseONE(this->archiveId);
}
}
