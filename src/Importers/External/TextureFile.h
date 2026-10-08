#pragma once
#include <string>
#include "Texture.h"

/*
Our own texture files for assets that are not on a disc: a plain PNG (any size, read as RGBA8; rows stored top first as stb gives them, not flipped; v = 0 is that first row, as for the disc textures: the importers do not flip v and Texture::create uploads row 0 first) plus an optional sidecar <file>.png.json with
"filter" ("nearest"|"linear"), "wrap" ("clamp"|"repeat"|"mirror") and "mipmaps" (bool); a field or the file left out keeps the Texture default. Both functions throw std::runtime_error naming the file.
*/
namespace External
{
/* Reads the image (PNG or JPEG: stb_image decodes both) and its sidecar into a Texture that is not uploaded yet (call create()). */
Graphics::Texture readTexture(const std::string& rPngPath_);

/* Writes the PNG and always the sidecar. The texture must still hold its pixels (create() clears them) in 4 channels. */
void writeTexture(const std::string& rPngPath_, const Graphics::Texture& rTexture_);
}
