#pragma once
#include "ExternalModel.h"
#include "ModelFile.h"
#include <cstdint>
#include <string>

/*
Finds the external assets (files in Assets/External, formats in Importers/External/ModelFile.h and TextureFile.h) and registers them in two separate sets of pools (Graphics::externalProject and
externalLevels, ExternalModel.h): the project's, "Global" in the editor (Assets/External/Models|Textures) and the opened level's (Assets/Levels/<levelName>/Models|Textures, next to the level's file). Disc assets are never involved. Custom vertex layouts go to Graphics::vertexLayouts and stay for the run, so meshes never point at a dead layout.
*/
namespace Loading
{
/*
Reads the folders into the pools at the start of a level open (the EDITOR build's loadLevel calls it): the project's once, the level's when that level has no pools yet; the level's pools then become the active level scope
(Graphics::externalLevel()). Pools of other loaded levels are kept, so the entities that hold their models stay valid (forgetExternalAssets drops them with the level).
Nothing is watched or rescanned: an asset the editor imports is registered at once by importExternalTexture / importExternalModel, so a file put into the folders by hand is read at the next start (project) or
the next time that level is opened after it was unloaded. Textures are read before models. A file that fails to read is printed to stderr and skipped (one bad file must not stop a level from opening). Needs the GL context.
*/
void scanExternalAssets(const std::string& levelName_);

/* The level is destroyed (Level::destroy): its pools are freed and, if they were the active scope, the scope is empty again. Entities of that level are gone by then. */
void forgetExternalAssets(const std::string& levelName_);

/* Project = Assets/External/Models|Textures, Level = the folder of the level of the last scan (needs one). */
enum class ExternalScope
{
	Project,
	Level
};

/*
The editor imports an asset: the files are written to the scope's folder and the asset is registered in its pools, so it is usable and listed at once. A texture must still hold its 4 channel pixels and its name must
be new in the scope (a texture in use by models can't be replaced); a model replaces one of the same name (its texture names are resolved as in a scan, a missing one is printed). The name becomes the file name (no
slashes). The files are also copied into the source tree (External/ or Levels/<level>/, as the glTF import does), so a build does not lose them. Throws std::runtime_error, with nothing registered, if a file can't be written or copied. Needs the GL context.
*/
void importExternalTexture(const std::string& name_, Graphics::Texture texture_, ExternalScope scope_);
void importExternalModel(External::ModelData model_, ExternalScope scope_);

/* Counts every change of the pools (scan, import): a list built from them (the Assets tab) is stale when it differs from the value it was built at. */
uint32_t externalRevision();

/* The model/texture named so, the level's first, then the project's; nullptr if there is none. A pointer dies when its asset is replaced or its level is destroyed (forgetExternalAssets). It looks in the active level scope only. */
const Graphics::ExternalModel* findExternalModel(const std::string& name_);
Graphics::Texture* findExternalTexture(const std::string& name_);
}
