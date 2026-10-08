#pragma once
#include "PoolId.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include <functional>
#include <string>

/*
The one place levels are made. loadLevel reads Assets/Levels/<name>/<name>.json (a level is a folder, copied from Levels/ at build: the file, and beside it the level's own Models, Textures, Collision and Prefabs), makes the Level with the engine the file names
("sadx": an SADX land table; "sth2005": a Shadow stage, "gltf": glTF files), gives it its instance, collision and placed entities, and returns it. The file format is in
docs/ENGINE_USAGE.md. Generated tables (entity tables, type names, stage entities) are not in the file: it names them and LevelLoader.cpp holds the registry of those names.
A file that is missing or wrong, or a name the registry does not know, throws std::runtime_error with the level's name; a level that fails halfway is removed again.
Loading a name twice reuses the keys made the first time (instances keep pointers into them).
*/
namespace Loading
{
    PoolId loadLevel(const std::string& name_);

#ifdef EDITOR
    /*
    Writes the level's edits into its file (Assets/Levels/<name>/<name>.json, and Levels/<name>/<name>.json of the source tree) under the file name given (a name other than the level's copies the level's whole folder to the new name first), copying the rest of the level's file as it is: an "edits" object with
    "moved" (a level's own entity by index, its "position" and the "source" position the game's data gives) and "added" (an entity of "prefab" at "position"). loadLevel applies them after it has made the level's
    own entities; no edits removes the object. Throws std::runtime_error if a file can't be read or written.
    */
    void saveLevelEdits(PoolId levelId_, const std::string& fileName_);

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
        std::string stageEntities;
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

    /* Calls the function for every entity key of the generated entity tables the registry names (and of the stage entities), each table in turn: the source of the prefabs (Prefab.h). The keys live for the whole run. */
    void forEachSAEntityKey(const std::function<void(const SAEntityKey&)>& rFunction_);
    void forEachONEEntityKey(const std::function<void(const STH2005::EntityKey&)>& rFunction_);
}
