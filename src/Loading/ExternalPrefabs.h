#pragma once
#include "ExternalAssets.h"
#include "PoolId.h"
#include <string>
#include <vector>

/*
The External prefabs of the editor's Prefabs tab (PrefabKind::External, Prefab.h), in two scopes like the External assets: Project ("Global" in the editor; every level) in Assets/External/Prefabs/Global/prefabs.json, and Level (the open level's) in
Assets/Levels/<levelName>/prefabs.json, both mirrored into External/Prefabs/Global/ and Levels/<levelName>/ of the source tree. A file is {"folders": ["a", "a/b"], "prefabs": [{"name": "Crate", "folder": "a", "mesh": choice, "texture": choice}]}; folders lists
every folder (so an empty one is kept), a prefab's folder is "" at the top, mesh and texture are written only when set ({"kind": "external", "name": ...}, as in the level file; Disc has no meaning here). A missing file is an empty scope.
A prefab name is unique over its scope (the pool name is "Global/<name>" or "Level/<name>"). EDITOR only: the game does not read or write them.
*/
namespace Loading
{
/* Reads the scope's file and registers its prefabs (addExternalPrefab). Project: once, after registerPrefabs. Level: when a level is opened (loadLevel, before the level's edits are applied, which may name them); levelName_ is the level. A bad entry is printed to stderr and skipped. */
void loadExternalPrefabs(ExternalScope scope_, const std::string& levelName_ = std::string());

/* The level is gone: forgets the Level scope (its folders and name) and removes its prefabs from the pool (removeLevelPrefabs). Called by Level::destroy. */
void unloadLevelPrefabs();

/* The scope's folders, every level of the path ("a", "a/b"), sorted. */
const std::vector<std::string>& externalPrefabFolders(ExternalScope scope_);

/*
A new empty prefab / folder in the folder parent_ of the scope ("" = the top), saved at once. A name is letters, digits, '_', '-', ' ' (a prefab's is also what the entity is called). Throws std::runtime_error, changing nothing, if the name is bad or taken, the
parent folder does not exist, or (Level scope) no level is open or the file can't be written. createExternalPrefab returns the new prefab.
*/
PoolId createExternalPrefab(ExternalScope scope_, const std::string& folder_, const std::string& name_);
void createExternalPrefabFolder(ExternalScope scope_, const std::string& parent_, const std::string& name_);

/* Writes the scope's folders and prefabs (called after every change, including setPrefabMesh / setPrefabTexture of an External prefab). Throws std::runtime_error if the file can't be written. */
void saveExternalPrefabs(ExternalScope scope_);
}
