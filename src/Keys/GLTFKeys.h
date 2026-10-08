#pragma once
#include <string>
#include <vector>

/*
A level made of glTF files (engine "gltf" of a level file), as it is after the import: the level's name, whose folder Assets/Levels/<levelName>/ (the level's own, next to <levelName>.json) holds the visual (Models/<levelName>_*.chunk.model.json with
their textures, made by the editor's import from a .gltf/.glb) and the collision files (Collision/<name>.collision.json), and the names of the collision files that make up its collision. Unlike the disc keys these are
strings, not static data: they are only used while the level is made.
*/
struct GLTFLevelKey
{
	std::string levelName;
	std::vector<std::string> collisions;

	std::string folder() const { return "Assets/Levels/" + this->levelName; }
};
