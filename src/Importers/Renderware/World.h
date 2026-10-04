#pragma once
#include <cstdint>
#include <vector>
#include "RenderWareWorld.h"

namespace Renderware
{
/*
Reads a RenderWare world from memory (a stage's .RG1: a name chunk followed by the World chunk) into a
Graphics::RenderWareWorld: one sector per atomic sector (leaf of the BSP tree) that has geometry, with one submesh per
mesh of the sector, each with the color and base texture name of its material.
Only the GameCube native sector data of the .RG1 files is supported (see World.cpp); a collision .BSP, whose sectors
hold plain data, throws. GPU upload happens when a level streams the sector in.
*/
Graphics::RenderWareWorld readWorld(const std::vector<uint8_t>& rBytes_);
}
