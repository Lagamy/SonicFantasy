#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Ninja
{
/*
One collision/visual piece of a land table: the file offset of its root NJS_OBJECT (import it with importSAModel), its
surface flags and its bounding sphere (center and radius, which levels cull by).
*/
struct SALandTableCol
{
	uint32_t modelOffset;
	uint32_t flags;
	glm::vec3 center;
	float radius;
};

/*
textureNames is the land table's texlist: the name of the texture each Ninja texture id stands for (empty if the table
has none). A name is looked up in the TextureSet the level uses (TextureSet::names).
*/
struct SALandTable
{
	std::vector<SALandTableCol> cols;
	std::vector<std::string> textureNames;
};

/*
Reads the land table at offset_ of a relocated GameCube .rel module (pointers are file offsets, big-endian, see
relocateRel). Only reads the structure: the pieces' models are imported by the caller. Throws if a pointer or a count
points outside the file.
*/
SALandTable readSALandTable(const std::vector<uint8_t>& rBytes_, uint32_t offset_);
}
