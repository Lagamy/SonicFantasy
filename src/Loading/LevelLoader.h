#pragma once
#include "PoolId.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include <functional>
#include <string>

/*
The one place levels are made. loadLevel reads Assets/Levels/<name>/<name>.json (a level is a folder, copied from Levels/ at build: the file, and beside it the level's own Models, Textures, Collision and Prefabs), makes the Level with the engine the file names
("sadx": an SADX land table; "sth2005": a Shadow stage, "gltf": glTF files), gives it its instance, collision and placed entities, and returns it. The file format is in
docs/ENGINE_USAGE.md. Generated tables (entity tables, type names) are not in the file: it names them and LevelLoader.cpp holds the registry of those names.
A file that is missing or wrong, or a name the registry does not know, throws std::runtime_error with the level's name; a level that fails halfway is removed again.
Loading a name twice reuses the keys made the first time (instances keep pointers into them).
*/
namespace Loading
{
    PoolId loadLevel(const std::string& name_);

#ifdef EDITOR
    /*
    Writes the prefab's Changed file (Assets/External/Prefabs/<prefab name>/Changed/prefab.json, and External/Prefabs/... of the source tree) from Prefab::changed, or removes it when that is empty. Called by the prefab edit functions (Prefab.h) after a Global edit.
    Throws std::runtime_error if a file can't be written.
    */
    void saveChangedPrefab(PoolId prefabId_);

    /*
    Writes the level's edits into its file (Assets/Levels/<name>/<name>.json, and Levels/<name>/<name>.json of the source tree) under the file name given (a name other than the level's copies the level's whole folder to the new name first), copying the rest of the level's file as it is: an "edits" object with
    "moved" (a level's own entity by index, its "position" and the "source" position the game's data gives) and "added" (an entity of "prefab" at "position"). loadLevel applies them after it has made the level's
    own entities; no edits removes the object. Throws std::runtime_error if a file can't be read or written.
    */
    void saveLevelEdits(PoolId levelId_, const std::string& fileName_);

    /*
    File > Save Source Game Level: makes the level as it is now (the level file's source with its edits and the prefab edits of the level on top) the level's source, in the level's own folder. The file gets a "source" object (the format of "edits" without "prefabs", with the "added"
    entities included) and loses its "edits"; each prefab's edits go to Assets/Levels/<name>/SourceGamePrefabs/<prefab name>/prefab.json (and Levels/ of the source tree), the file removed when the prefab has none. loadLevel applies the source before it marks the level's
    entities as the source, so Restore Source Game Level and later edits are from it. The loaded level is not changed (the caller loads it again). Throws std::runtime_error if a file can't be read or written.
    */
    void saveSourceLevel(PoolId levelId_);

    /*
    What the editor's File > New level asks for; a field left empty is not written. "sadx" needs landTableRel, landTableOffset and textures (the .gvm); "sth2005" needs sections (comma separated) and textures (the texture .one) with txd; "gltf" needs model (a .gltf/.glb) and may have collisions (comma separated .gltf/.glb files); both are imported into the level's folder (GLTFImport.h), the files are not needed afterwards,
    and uses none of the disc fields below it. placementFiles is comma separated. The tables are names LevelLoader.cpp knows. Everything else of the format (skippedTextures, skippedEntries, position) is written by hand in the file.
    */
    struct NewLevelFile
    {
        std::string name;
        std::string engine = "sadx";
        std::string shader = "SA";
        std::string landTableRel;
        std::string landTableOffset;
        std::string textures;
        std::string txd;
        std::string sections;
        std::string placementFiles;
        std::string entityTable;
        std::string typeNames;
        std::string model;
        std::string collisions;
    };

    /*
    Makes the level's folder and writes Assets/Levels/<name>/<name>.json and Levels/<name>/<name>.json of the source tree from the fields; loadLevel can then load it (it is not loaded here, so a wrong disc path shows when it is). Throws std::runtime_error
    if the name is empty or has anything but letters, digits, '_' and '-', if a level of that name already exists, if a required field of the engine is empty or the engine is not "sadx" / "sth2005" / "gltf", or if a file can't be written.
    */
    void createLevelFile(const NewLevelFile& rLevel_);

    /*
    File menu, glTF levels (isGLTFLevel; the others throw std::runtime_error("the level is not a glTF level.")). swapGLTFLevelModel: reads the glTF, replaces the level's chunks in its folder (GLTFImport.h) and its live visual.
    addGLTFLevelCollision: reads the glTF into a new collision file, adds its name to "collisions" of the level file and its triangles to the live collision (returns the name). swapGLTFLevelCollision: overwrites the named
    collision file (one of listGLTFLevelCollisions) with the triangles of another glTF, and rebuilds the live collision if the file is in use. activeGLTFCollisions: the names now in the collision. A throw leaves what was done before it.
    */
    void swapGLTFLevelModel(PoolId levelId_, const std::string& gltfPath_);
    std::string addGLTFLevelCollision(PoolId levelId_, const std::string& gltfPath_);
    void swapGLTFLevelCollision(PoolId levelId_, const std::string& name_, const std::string& gltfPath_);
    std::vector<std::string> activeGLTFCollisions(PoolId levelId_);
    bool isGLTFLevel(PoolId levelId_);
#endif

    /*
    Gives the prefabs built from a key what is not in the key: their group (Prefab::group, from Assets/External/Prefabs/Groups.json), for the Shadow objects Prefab::layout, variants and loopMotion (from Assets/External/Prefabs/STH2005/Placement.json,
    written by tools/genEntityKeys.py) and for the SADX objects Prefab::rotation and variantField (from Assets/External/Prefabs/SADX/Placement.json, written by tools/genSAEntityKeys.py). The files are described in ENGINE_USAGE.md. A missing or bad file is printed to stderr and the prefabs keep StaticObjects / Single / the usual rotation. Once, after registerPrefabs; before any level or entity is made.
    */
    void loadPrefabData();

    /*
    Reads the Changed file of every prefab built from a key (Assets/External/Prefabs/<game>/<KEY>/Changed/prefab.json, e.g. SADX/SPRING) into Prefab::changed: the same object as a level file's edits.prefabs entry (see ENGINE_USAGE.md). A missing file is no changes; a bad one is
    printed to stderr and skipped. Once, after registerPrefabs and the scripts (a behaviour is named); before any level or entity is made.
    */
    void loadChangedPrefabs();

    /*
    Reads the level's Source Game Level prefab files (Assets/Levels/<level>/SourceGamePrefabs/<prefab name>/prefab.json, e.g. .../SADX/SPRING/prefab.json: the same object as a Changed file) into Prefab::source of every prefab (all others are emptied). A missing file is no edits; a bad one is printed
    to stderr and skipped. loadLevel calls it before the level's entities are made (after the level's External prefabs exist).
    */
    void loadSourcePrefabs(const std::string& levelName_);

    /* Calls the function for every entity key of the generated entity tables the registry names (and the ones a stage's own code creates), each table in turn: the source of the prefabs (Prefab.h). The keys live for the whole run. */
    void forEachSAEntityKey(const std::function<void(const SAEntityKey&)>& rFunction_);
    void forEachONEEntityKey(const std::function<void(const STH2005::EntityKey&)>& rFunction_);
}
