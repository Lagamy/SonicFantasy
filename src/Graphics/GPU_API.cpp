#include "GPU_API.h"
#include "GPU_Globals.h"
#include "SAInstance.h"
#include "Texture.h"
#include <format>
#include <stdexcept>

namespace Graphics 
{
    PoolId addBasicMesh(const char* name_)
    {
        return basicMeshes.add(name_); 
    } 

    PoolId addTexture(const char* name_, uint32_t width_, uint32_t height_, uint32_t channels_)
    {
        return textures.add(name_, width_, height_, channels_); 
    }

    PoolId addShader(const char* name_)
    {
        return shaders.add(name_, name_);
    }

    PoolId addSAMesh(const char* name_)
    {
        return Ninja::importSAMdl(name_, std::format("{}{}.sa1mdl", modelFolderPath, name_).c_str()); 
    }

    PoolId addTextureSet(const char* name_)
    {
        return textureSets.add(name_);
    }

    SAInstance& getSAInstance(PoolId id_)
    {
        return saInstances[id_];
    } 

    ONEInstance& getONEInstance(PoolId id_)
    {
        return oneInstances[id_];
    }

    SALevelInstance& getSALevelInstance(PoolId id_)
    {
        return saLevelInstances[id_];
    }

    ONELevelInstance& getONELevelInstance(PoolId id_)
    {
        return oneLevelInstances[id_];
    }

    BasicMesh& getBasicMesh(PoolId id_)
    {
        return basicMeshes[id_]; 
    }

    Texture& getTexture(PoolId id_)
    {
        return textures[id_]; 
    } 

    TextureSet& getTextureSet(PoolId id_)
    {
        return textureSets[id_];
    }

    Shader& getShader(PoolId id_)
    {
        return shaders[id_];
    }

    SAMesh& getSAMesh(PoolId id_)
    {
        return saMeshes[id_];
    }

    PoolId getBasicMeshId(const char* name_)
    {
        return basicMeshes.getIdByName(name_); 
    }

    PoolId getTextureId(const char* name_)
    {
        return textures.getIdByName(name_);
    }

    PoolId getTextureSetId(const char* name_)
    {
        return textureSets.getIdByName(name_);
    }

    PoolId getShaderId(const char* name_)
    {
        return shaders.getIdByName(name_);
    }

    PoolId getSAMeshId(const char* name_)
    {
        return saMeshes.getIdByName(name_);
    }


    BasicMesh& getBasicMeshByName(const char* name_)
    {
        return basicMeshes[basicMeshes.getIdByName(name_)];
    }

    Texture& getTextureByName(const char* name_)
    {
        return textures[textures.getIdByName(name_)];
    }

    Shader& getShaderByName(const char* name_)
    {
        return shaders[shaders.getIdByName(name_)];
    }

    SAMesh& getSAMeshByName(const char* name_)
    {
        return saMeshes[saMeshes.getIdByName(name_)];
    }
}