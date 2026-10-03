#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace STH2005
{
/*
One bone of a .BON skeleton. id matches the HAnim node id of the DFF frame it describes.
parent is an index into Bon::nodes, -1 for the root; parents come before children.
*/
struct BonNode
{
	uint32_t id;
	std::string name;
	int32_t parent;
	glm::vec3 translation;
	glm::vec3 rotation;
	glm::vec3 scale;
};

/* A .BON file from Shadow the Hedgehog: the named bone hierarchy that goes with one .DFF. */
struct Bon
{
	std::string name;
	std::vector<BonNode> nodes;
};

Bon readBon(const std::vector<uint8_t>& rBytes_);
}
