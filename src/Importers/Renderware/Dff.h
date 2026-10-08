#pragma once
#include <cstdint>
#include <vector>
#include "PoolId.h"
#include "RenderWareMesh.h"

namespace Renderware
{
/*
Reads a RenderWare clump (.dff) from memory into a Graphics::RenderWareMesh.
First pass: frames (with their HAnim node id as boneId), generic (non-native) geometry with positions,
normals and the first UV set, one submesh per material with the material's color and base texture name, and the Skin
plugin (bone indices and weights per vertex, inverse bind matrices, each skin bone's frame through the HAnim hierarchy).
BinMesh and MatFX are skipped; the material's UV animation name (plugin 0x135) is kept on the submesh (the animation itself is in a .UVA, see Uva.h). The texture itself comes from a .txd (see Txd.h).
GPU upload happens later, in RenderWareMesh::use.
*/
Graphics::RenderWareMesh readDff(const std::vector<uint8_t>& rBytes_);
}
