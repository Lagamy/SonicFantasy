#pragma once
#include "PoolId.h"
#include <string>
#include <vector>
#include <glm/glm.hpp>

/*
The map: which levels sit side by side in one space and where (docs/TODO.md, level map). It is above Level: a level keeps its own local coordinates and files, and the map holds each level's place (MapLevel) in the one space. The file is Map.json
(Assets/Map.json, copied from the source tree's Map.json; the editor writes both): {"levels": [{"name", "position": [x, y, z], "rotation": [x, y, z]}]}, rotation in degrees (Level::mapRotation). A missing file or an empty list is a map of no levels: every
level loads alone, as before the map existed.
Loading::loadMap loads a level and, if it is in the map, every other level of the map beside it (the map's own load, automatic); Loading::loadLevel gives each level its place (Level::setMapTransform). active is the name of the level the editor edits
(activeLevelId, API.h) and the Level tab shows; loaded are the levels loadMap made beside the active one, which the next loadMap removes first.
*/
struct MapLevel
{
	std::string name;
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 rotation = glm::vec3(0.0f);
};

struct Map
{
	std::vector<MapLevel> levels;
	std::string active;
	std::vector<PoolId> loaded;

	/* Reads Assets/Map.json; a missing file leaves the map empty, a wrong one is reported on stderr and leaves it empty. */
	void read();
	/* The entry of a level name, nullptr if the map has none. */
	const MapLevel* find(const std::string& rName_) const;
	/* Adds or replaces the entry of the name. */
	void set(const MapLevel& rLevel_);
	/* Removes the entry of the name, if there is one. */
	void remove(const std::string& rName_);
#ifdef EDITOR
	/* Writes Map.json to Assets/ and to the source tree. Throws std::runtime_error if a file can't be written. */
	void write() const;
#endif
};
