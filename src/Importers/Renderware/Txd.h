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
Only C4 textures (4 bit palette indices) with an RGB565 or RGB5A3 palette are supported, the only kind in Shadow's
SHADOW.TXD; anything else throws.
The caller owns the returned ids (STH2005::ONE keeps them per dictionary and removes the textures with the archive).
*/
TxdTextures importTxd(const std::vector<uint8_t>& rBytes_);
}
