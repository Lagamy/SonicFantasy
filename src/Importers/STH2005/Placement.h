#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace STH2005
{
/*
One object of a Shadow the Hedgehog stage's placement file (stgNNNN_cmn.dat, _nrm.dat, _hrd.dat, _ds1.dat), see readPlacements.
position and rotation are the file's own values (the game's space, before RenderWareYawDegrees; rotation in degrees).
type is the id of the object's class (external: the registry of 286 object descriptors in main.dol at 0x8052b080, each with a name and an id), flags and flagsB the two words in front of it, subType the u16
after it (meaning unknown); params are the bytes this record owns in the file's parameter block (their layout depends on the type).
*/
struct Placement
{
	glm::vec3 position;
	glm::vec3 rotation;
	uint32_t flags;
	uint32_t flagsB;
	uint16_t type;
	uint16_t subType;
	std::vector<uint8_t> params;
};

/* Reads a stage placement file into its records, in file order. Throws if the file does not follow the layout described in Placement.cpp. */
std::vector<Placement> readPlacements(const std::vector<uint8_t>& rBytes_);
}
