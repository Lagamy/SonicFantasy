#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "PoolId.h"

namespace Renderware
{
/* The textures of one dictionary as Texture pool ids: in dictionary order, and by the name inside the file. */
struct TxdTextures
{
	std::vector<PoolId> ids;
	std::unordered_map<std::string, PoolId> byName;
};

/*
Reads a RenderWare texture dictionary (.txd) of GameCube native textures from memory and adds every texture to the
Texture pool under its name, already uploaded to the GPU (needs the GL context).
Supported formats (see Txd.cpp): C4 and C8 with an RGB565 or RGB5A3 palette, RGB565, RGB5A3 and CMPR, one mip level;
anything else throws.
The caller owns the returned ids (STH2005::ONE keeps them per dictionary and removes the textures with the archive).
*/
TxdTextures importTxd(const std::vector<uint8_t>& rBytes_);
}
