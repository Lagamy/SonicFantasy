#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "PoolId.h"

namespace Graphics
{
/*
The textures of one imported .gvm archive (SADX), as Texture pool ids in the file's order, which is what a Ninja
material's texture id indexes (see importGvm), and their entry names in the same order (names), which a land table's
texlist looks them up by.
A set is named by its source path (e.g. "sonic.gvm") and loaded on demand when an instance with its SATextureSetKey is
created; users counts the instances using it, and at 0 the set removes its textures and bindings.
saBindings are the TextureBindings built for this set, one per SAMesh (mesh id, binding id).
Shadow's textures do not use sets: they belong to their STH2005::ONE.
*/
struct TextureSet
{
	std::vector<PoolId> ordered;
	std::vector<std::string> names;
	uint32_t users = 0;
	std::vector<std::pair<PoolId, PoolId>> saBindings;

	void destroy();
};
}
