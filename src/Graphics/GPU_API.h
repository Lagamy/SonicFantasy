#pragma once 
#include "BasicMesh.h"
#include "SAInstance.h"
#include "Texture.h"
#include "TextureSet.h"
#include "TextureBinding.h"
#include "Shader.h"
#include "SAMesh.h"
#include "ONE.h"
#include "ONEInstance.h"
#include "SALevelInstance.h"
#include "ONELevelInstance.h"
#include "PoolId.h"
#include "SAMdl.h"
#include "Constants.h"

namespace Graphics
{
    PoolId addBasicMesh(const char* name_);
    PoolId addTexture(const char* name_, uint32_t width_, uint32_t height_, uint32_t channels_ );
    PoolId addShader(const char* name_);
    PoolId addSAMesh(const char* name_);
    PoolId addTextureSet(const char* name_);

    BasicMesh& getBasicMesh(PoolId id_);
    Texture& getTexture(PoolId id_);
    TextureSet& getTextureSet(PoolId id_);
    Shader& getShader(PoolId id_);
    SAMesh& getSAMesh(PoolId id_);
    SAInstance& getSAInstance(PoolId id_); 
    ONEInstance& getONEInstance(PoolId id_);
    SALevelInstance& getSALevelInstance(PoolId id_);
    ONELevelInstance& getONELevelInstance(PoolId id_);

    PoolId getBasicMeshId(const char* name_);
    PoolId getTextureId(const char* name_);
    PoolId getTextureSetId(const char* name_);
    PoolId getShaderId(const char* name_);
    PoolId getSAMeshId(const char* name_);

    BasicMesh& getBasicMeshByName(const char* name_);
    Texture& getTextureByName(const char* name_);
    Shader& getShaderByName(const char* name_);
    SAMesh& getSAMeshByName(const char* name_);
}