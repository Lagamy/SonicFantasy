#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Renderware
{
/* One key of a UV animation: the UV transform at time (seconds). */
struct UvAnimKey
{
	float time;
	float theta;
	glm::vec2 scale;
	float skew;
	glm::vec2 offset;
};

/* A named UV animation (RenderWare UV animation dictionary entry): keys sorted by time, the last one at duration. */
struct UvAnimation
{
	std::string name;
	float duration;
	std::vector<UvAnimKey> keys;
};

/*
Reads a RenderWare UV animation dictionary (.uva) from memory, see Uva.cpp. A material names its animation in the
material's UV animation plugin (RwMaterial::uvAnimName).
*/
std::vector<UvAnimation> readUvaDictionary(const std::vector<uint8_t>& rBytes_);
}
