#pragma once
#include <string>
#include <vector>

/*
The editor's import of glTF files into a glTF level's folders (Assets/Levels/<levelName>/, formats in Importers/External/ModelFile.h, TextureFile.h and CollisionFile.h). The glTF is read once, here; the level is
read from the written files afterwards (addGLTFLevelInstance, addGLTFCollision). Everything is written to Assets/Levels/<levelName>/ (what the game reads) and mirrored into Levels/<levelName>/ of the source tree (what the next build copies
there). Needs no GL context. All functions throw std::runtime_error naming the file; a failed import leaves earlier files of the level as they were only if it failed before writing.
*/
namespace Loading
{
/* The grid cell (XZ, in the glTF's own units, metres) a chunk of the level's visual covers, and the most triangles one chunk may hold before its cell is split in halves along its longer side. 256 and 65536 are guesses, not measured. */
constexpr float ChunkCellSize = 256.0f;
constexpr size_t MaxChunkTriangles = 65536;

/*
Replaces the level's visual: the old chunks and their textures (files starting with "<levelName>_" in Models/ that end in .chunk.model.json/.bin, .png or .png.json) are deleted, then the glTF is split into chunks (the
triangles into the cell their centroid is in, a cell over MaxChunkTriangles split further), each written as <levelName>_<cx>_<cz>_<k>.chunk.model.json/.bin with one submesh per texture, and the glTF's images as
<levelName>_tex<i>.png. Returns the number of chunks.
*/
size_t importGLTFLevelModel(const std::string& levelName_, const std::string& gltfPath_);

/* Writes the glTF's triangles as Collision/<name>.collision.json/.bin; name = the file's stem (letters, digits, '_' and '-', the rest replaced by '_'), with _2, _3... added if taken. Returns the name. */
std::string importGLTFLevelCollision(const std::string& levelName_, const std::string& gltfPath_);

/* Overwrites Collision/<name>.collision.json/.bin (which must exist) with the glTF's triangles. */
void replaceGLTFLevelCollision(const std::string& levelName_, const std::string& name_, const std::string& gltfPath_);

/* The names of the collision files in the level's Collision/ folder, sorted. */
std::vector<std::string> listGLTFLevelCollisions(const std::string& levelName_);
}
