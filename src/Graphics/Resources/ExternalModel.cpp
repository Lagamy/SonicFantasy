#include "ExternalModel.h"
#include "GPU_Globals.h"
#include "RenderStats.h"
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
void ExternalModel::draw(PoolId shaderId_, bool skinned_, const glm::mat4& rTransform_, Texture* pOverrideTexture_) const
{
	Shader& rShader = shaders[shaderId_];
	renderStats.program(rShader.id);
	glUseProgram(rShader.id);
	if(skinned_) glUniform1i(SkinnedLocation, 0);
	renderStats.uniformUploads++;
	glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(rTransform_));
	ExternalPools& rOwn = this->inProject ? externalProject : externalLevel;
	for(const ExternalSubmesh& rSubmesh : this->submeshes)
	{
		bool hasTexture = rSubmesh.textureId != UninitializedPoolId;
		if(hasTexture)
		{
			Texture& rTexture = pOverrideTexture_ ? *pOverrideTexture_ : (rSubmesh.textureInProject ? externalProject : externalLevel).textures[rSubmesh.textureId];
			renderStats.texture(rTexture.textureId);
			glBindTextureUnit(0, rTexture.textureId);
		}
		glUniform1i(HasTextureLocation, hasTexture);
		rOwn.meshes[rSubmesh.meshId].draw();
	}
}
}
