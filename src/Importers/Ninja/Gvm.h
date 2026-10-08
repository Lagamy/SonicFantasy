#pragma once
#include <cstdint>
#include <vector>
#include "PoolId.h"

namespace Ninja
{
/*
Reads a GameCube .gvm texture archive (GVR textures) from memory, adds every texture to the Texture pool under its
entry name, already uploaded to the GPU (needs the GL context), and registers them in a new TextureSet called
setName_ in archive order (ordered), with their entry names (names). An instance of an SA mesh gets its textures from it (Entity::setSAGPUInstance).
Hypothesis, unverified: a Ninja material's texture id is the position in the archive (every texture id used by Sonic's
model in ModelsMiles.rel is below the 16 textures of sonic.gvm, and the decoded images look like the parts that use them).
Only mip level 0 is read. Only CMPR and RGB5A3 textures are supported, the only formats in sonic.gvm; anything else throws.
*/
void importGvm(const char* setName_, const std::vector<uint8_t>& rBytes_);

/* Reads a single .gvr texture file (an optional GBIX header, then GVRT; same formats as importGvm) into the Texture pool under name_, uploaded to the GPU, and returns its id. Throws like importGvm. */
PoolId importGvr(const char* name_, const std::vector<uint8_t>& rBytes_);
}
