#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "RwChunk.h"

namespace Renderware
{
/* A RenderWare material as the importers use it: its color and its base texture name (empty if untextured). */
struct RwMaterial
{
	glm::vec4 color;
	std::string textureName;
};

/* Reads a MaterialList chunk (of a clump's geometry or of a world), see RwMaterial.cpp. */
std::vector<RwMaterial> readMaterialList(const std::vector<uint8_t>& rBytes_, const Chunk& rList_);
}
