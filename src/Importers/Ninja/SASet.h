#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace Ninja
{
/*
One object of an SADX stage's SET file (SetNNNNx.bin), see readSAPlacements. type is the object's index in the stage's object list (the 84 entries of
_Stg01.rel for Emerald Coast, whose first 32 are shared; see tools/genSAEntityKeys.py), flags the top 4 bits of the record's u16 (meaning unknown: 0 or 8 on Set0100S, 54 of its 788 records have 8), rotation its three angles in BAMS (65536 = a full turn), position and scale
as stored; the scale holds the object's parameters as floats (the first records of Set0100S have 0, 100, 0), whose meaning depends on the type.
*/
struct SAPlacement
{
	uint16_t type;
	uint8_t flags;
	glm::ivec3 rotation;
	glm::vec3 position;
	glm::vec3 scale;
};

/* Reads a SET file into its records, in file order. Throws if the file does not follow the layout described in SASet.cpp. */
std::vector<SAPlacement> readSAPlacements(const std::vector<uint8_t>& rBytes_);
}
