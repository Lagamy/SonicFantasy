#pragma once 
#include <cstdint>
#include <string>
#include "PoolId.h"

inline std::string shaderFolderPath = "Assets/Shaders/"; 
inline std::string textureFolderPath = "Assets/External/Textures/";
inline std::string modelFolderPath = "Assets/External/Models/";
inline std::string shadowDiscPath = "Disks/Shadow2005.iso";
inline std::string sadxDiscPath = "Disks/SADX.iso";
inline const uint32_t UninitializedId = std::numeric_limits<uint32_t>::max();  
inline const PoolId UninitializedPoolId = PoolId{UninitializedId, UninitializedId};