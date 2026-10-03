#include "TextureSet.h"
#include "GPU_Globals.h"

namespace Graphics
{
/*
Removes the set's textures from the Texture pool (which deletes them from the GPU) and its bindings from the binding pool.
Called by PoolMap::remove, when the last user of the set is gone.
*/
void TextureSet::destroy()
{
	for(PoolId textureId : this->ordered)
	{
		textures.remove(textureId);
	}
	for(const auto& rBinding : this->saBindings)
	{
		textureBindings.remove(rBinding.second);
	}
}
}
