#include "Map.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>
#include <stdexcept>

namespace
{
using Json = nlohmann::ordered_json;

glm::vec3 readVector(const Json& rValue_)
{
	return glm::vec3(rValue_.at(0).get<float>(), rValue_.at(1).get<float>(), rValue_.at(2).get<float>());
}

Json writeVector(const glm::vec3& rValue_)
{
	return Json::array({rValue_.x, rValue_.y, rValue_.z});
}
}

void Map::read()
{
	this->levels.clear();
	std::ifstream stream("Assets/Map.json");
	if(!stream) return;
	try
	{
		Json file = Json::parse(stream);
		for(const Json& rEntry : file.at("levels"))
		{
			MapLevel level;
			level.name = rEntry.at("name").get<std::string>();
			if(rEntry.contains("position")) level.position = readVector(rEntry.at("position"));
			if(rEntry.contains("rotation")) level.rotation = readVector(rEntry.at("rotation"));
			this->levels.push_back(level);
		}
	}
	catch(const std::exception& rError)
	{
		fprintf(stderr, "Assets/Map.json not read, no map: %s\n", rError.what());
		this->levels.clear();
	}
}

const MapLevel* Map::find(const std::string& rName_) const
{
	for(const MapLevel& rLevel : this->levels)
	{
		if(rLevel.name == rName_) return &rLevel;
	}
	return nullptr;
}

void Map::set(const MapLevel& rLevel_)
{
	for(MapLevel& rLevel : this->levels)
	{
		if(rLevel.name == rLevel_.name)
		{
			rLevel = rLevel_;
			return;
		}
	}
	this->levels.push_back(rLevel_);
}

void Map::remove(const std::string& rName_)
{
	std::erase_if(this->levels, [&](const MapLevel& rLevel_) { return rLevel_.name == rName_; });
}

#ifdef EDITOR
void Map::write() const
{
	Json levels = Json::array();
	for(const MapLevel& rLevel : this->levels) levels.push_back({{"name", rLevel.name}, {"position", writeVector(rLevel.position)}, {"rotation", writeVector(rLevel.rotation)}});
	std::string text = Json{{"levels", levels}}.dump(2);
	for(const std::string& rPath : {std::string("Assets/Map.json"), std::string(SF_SOURCE_DIR "/Map.json")})
	{
		std::ofstream out(rPath);
		if(!out) throw std::runtime_error(std::format("can't write {}.", rPath));
		out << text;
	}
}
#endif
