#pragma once
#include <vector>
#include "PoolId.h"

namespace Graphics
{
/*
A mesh's submeshes resolved against a texture source, built once and shared by every instance of that mesh with that
source. For an SAMesh the source is a TextureSet, which owns the binding (TextureSet::saBindings); for a Shadow mesh it
is a texture dictionary of its STH2005::ONE, which keeps the binding in STH2005::ONE::bindings.
submeshTextureIds[node or atomic][submesh] is the Texture pool id of that submesh, UninitializedPoolId if it has none.
*/
struct TextureBinding
{
	std::vector<std::vector<PoolId>> submeshTextureIds;
};
}
