#pragma once
#include <cstdint>
#include <vector>
#include "SAAnimation.h"

namespace Ninja
{
/*
Reads the Ninja motion (NJS_MOTION) at offset_ for a model of nodeCount_ nodes. Pointers inside rBytes_ must be file
offsets, as in a relocated .rel. Only position, rotation and scale channels with linear interpolation are supported;
anything else throws.
*/
Graphics::SAMotion readSAMotion(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t nodeCount_, bool bigEndian_);
}
