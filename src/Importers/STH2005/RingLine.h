#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "Placement.h"

namespace STH2005
{
/*
The positions of the rings one RING placement record stands for (type 16 of the registry in main.dol), in the placement file's own space (the game's space,
see Placement). Throws if the record's parameters are not the 20 bytes the ring object reads, or if its shape is not one of those implemented (see RingLine.cpp).
*/
std::vector<glm::vec3> readRingPositions(const Placement& rPlacement_);
}
