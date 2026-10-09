#include "BABehaviors.h"
#include "Globals.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <format>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

void BABehaviors::init()
{
	this->tagSolid = this->addTag(BATagSolid);
	this->tagPickup = this->addTag(BATagPickup);
	this->tagHazard = this->addTag(BATagHazard);
	this->tagDeath = this->addTag(BATagDeath);
	this->tagBody = this->addTag(BATagBody);

	this->none = this->add(BABehaviorNone, {});
	this->solid = this->add(BABehaviorSolid, BABehavior{{this->tagSolid}, {}, {}});
	this->pickup = this->add(BABehaviorPickup, BABehavior{{this->tagPickup}, {}, {}});
	this->hazard = this->add(BABehaviorHazard, BABehavior{{this->tagHazard}, {}, {}});
	this->death = this->add(BABehaviorDeath, BABehavior{{this->tagDeath}, {}, {}});
	this->passable = this->add(BABehaviorPassable, {});
	this->object = this->add(BABehaviorObject, {});
	this->body = this->add(BABehaviorBody, BABehavior{{this->tagBody}, {this->tagSolid.id}, {}});

	/* The built-ins as data, so that the editor lists them and customising one (a file entry with its name replaces its rules in place) and reverting it work as for a script's behaviour. */
	for(PoolId id : {this->none, this->solid, this->pickup, this->hazard, this->death, this->passable, this->object, this->body})
	{
		const BABehavior& rBehavior = Globals::Game::behaviors[id];
		BoundAreaBehaviorData data;
		data.name = Globals::Game::behaviors.names[id.id];
		for(PoolId tag : rBehavior.tags) data.tags.push_back(Globals::Game::baTags.names[tag.id]);
		for(uint32_t tagId : rBehavior.availableCollisions) data.collidesWith.push_back(Globals::Game::baTags.names[tagId]);
		this->boundAreas.scriptBehaviors.push_back(std::move(data));
	}
}

PoolId BABehaviors::addTag(const BATagKey& rKey_)
{
	if(Globals::Game::baTags.nameToId.contains(rKey_.name))
	{
		throw std::runtime_error(std::format("BABehaviors: a tag named {} exists already.", rKey_.name));
	}
	return Globals::Game::baTags.add(rKey_.name);
}

PoolId BABehaviors::add(const BABehaviorKey& rKey_, BABehavior behavior_)
{
	if(Globals::Game::behaviors.nameToId.contains(rKey_.name))
	{
		throw std::runtime_error(std::format("BABehaviors: a behaviour named {} exists already.", rKey_.name));
	}
	return Globals::Game::behaviors.add(rKey_.name, std::move(behavior_));
}

PoolId BABehaviors::findTag(const char* name_) const
{
	auto found = Globals::Game::baTags.nameToId.find(name_);
	return found != Globals::Game::baTags.nameToId.end() ? found->second : UninitializedPoolId;
}

PoolId BABehaviors::find(const char* name_) const
{
	auto found = Globals::Game::behaviors.nameToId.find(name_);
	return found != Globals::Game::behaviors.nameToId.end() ? found->second : UninitializedPoolId;
}

PoolId BABehaviors::idOfTag(const BATagKey& rKey_) const
{
	PoolId id = this->findTag(rKey_.name);
	if(id == UninitializedPoolId)
	{
		throw std::runtime_error(std::format("BABehaviors: there is no tag named {}.", rKey_.name));
	}
	return id;
}

PoolId BABehaviors::idOf(const BABehaviorKey& rKey_) const
{
	PoolId id = this->find(rKey_.name);
	if(id == UninitializedPoolId)
	{
		throw std::runtime_error(std::format("BABehaviors: there is no behaviour named {}.", rKey_.name));
	}
	return id;
}

void BABehaviors::resolve(Physics::Collision& rCollision_, const BARemap& rRemap_) const
{
	PoolId fallback = rRemap_.fallback;
	auto behaviorOf = [&](uint32_t raw_) -> PoolId
	{
		for(const auto& rEntry : rRemap_.entries)
		{
			if(rEntry.first == raw_) return rEntry.second;
		}
		return fallback;
	};

	rCollision_.fallbackBehavior = fallback;
	const Physics::CollisionMesh& rMesh = rCollision_.mesh;
	if(!rMesh.triangleAttributes.empty())
	{
		for(uint32_t value : rMesh.triangleAttributes)
		{
			if(!rCollision_.behaviorByValue.contains(value)) rCollision_.behaviorByValue.emplace(value, behaviorOf(value));
		}
	}
	else
	{
		for(const Physics::CollisionPiece& rPiece : rMesh.pieces)
		{
			if(!rCollision_.behaviorByValue.contains(rPiece.flags)) rCollision_.behaviorByValue.emplace(rPiece.flags, behaviorOf(rPiece.flags));
		}
	}
	for(Physics::CollisionShape& rShape : rCollision_.shapes)
	{
		rShape.behavior = behaviorOf(rShape.attr);
	}
}

namespace
{
/* One remap of the file: names are looked up now, so a wrong name fails at startup and resolve only compares ids. */
BARemap readRemap(const BABehaviors& rBehaviors_, const nlohmann::json& rFile_, const char* name_)
{
	const nlohmann::json& rRemap = rFile_.at(name_);
	auto behaviorNamed = [&](const std::string& rName_) -> PoolId
	{
		PoolId id = rBehaviors_.find(rName_.c_str());
		if(id == UninitializedPoolId)
		{
			throw std::runtime_error(std::format("BABehaviors: remap {} names the behaviour {}, which does not exist.", name_, rName_));
		}
		return id;
	};

	BARemap remap;
	remap.fallback = behaviorNamed(rRemap.at("fallback").get<std::string>());
	for(const auto& [rRaw, rBehavior] : rRemap.at("values").items())
	{
		size_t used = 0;
		uint32_t raw = static_cast<uint32_t>(std::stoul(rRaw, &used, 0));
		if(used != rRaw.size()) throw std::runtime_error(std::format("BABehaviors: remap {} has the raw value \"{}\", which is not a number.", name_, rRaw));
		for(const auto& rEntry : remap.entries)
		{
			if(rEntry.first == raw) throw std::runtime_error(std::format("BABehaviors: remap {} lists the raw value {} twice.", name_, rRaw));
		}
		remap.entries.emplace_back(raw, behaviorNamed(rBehavior.get<std::string>()));
	}
	return remap;
}
}

void BABehaviors::loadRemaps(const std::string& path_)
{
	std::ifstream stream(path_);
	if(!stream) throw std::runtime_error(std::format("BABehaviors: can't read {}.", path_));
	nlohmann::json file;
	try
	{
		file = nlohmann::json::parse(stream);
		this->remapShadowLevel = readRemap(*this, file, "ShadowLevel");
		this->remapSADXLevel = readRemap(*this, file, "SADXLevel");
		this->remapSADXShape = readRemap(*this, file, "SADXShape");
		this->remapShadowShape = readRemap(*this, file, "ShadowShape");
	}
	catch(const nlohmann::json::exception& rError)
	{
		throw std::runtime_error(std::format("BABehaviors: {} is wrong: {}", path_, rError.what()));
	}
	catch(const std::logic_error& rError)
	{
		throw std::runtime_error(std::format("BABehaviors: {} is wrong: {}", path_, rError.what()));
	}
}

PoolId BABehaviors::applyBoundArea(size_t index_)
{
	return this->applyBoundAreaData(this->boundAreas.behaviors.at(index_));
}

PoolId BABehaviors::applyBoundAreaData(const BoundAreaBehaviorData& rData_)
{
	auto tagNamed = [&](const std::string& rName_) -> PoolId
	{
		PoolId id = this->findTag(rName_.c_str());
		if(id == UninitializedPoolId) throw std::runtime_error(std::format("BABehaviors: the bound area behaviour {} names the tag {}, which does not exist.", rData_.name, rName_));
		return id;
	};

	BABehavior behavior;
	for(const std::string& rTag : rData_.tags) behavior.tags.push_back(tagNamed(rTag));
	for(const std::string& rTag : rData_.collidesWith) behavior.availableCollisions.insert(tagNamed(rTag).id);
	for(const BoundAreaRule& rRule : rData_.onTouch)
	{
		auto function = Globals::Game::boundAreaFunctions.nameToId.find(rRule.function);
		for(const std::string& rTag : rRule.tags)
		{
			uint32_t tagId = tagNamed(rTag).id;
			if(function != Globals::Game::boundAreaFunctions.nameToId.end()) behavior.availableInteractions[tagId].push_back(BAInteraction::script(Globals::Game::boundAreaFunctions[function->second].scriptFunction));
		}
	}

	PoolId id = this->find(rData_.name.c_str());
	if(id == UninitializedPoolId) return Globals::Game::behaviors.add(rData_.name.c_str(), std::move(behavior));
	Globals::Game::behaviors[id] = std::move(behavior);
	return id;
}

const BoundAreaBehaviorData* BABehaviors::scriptBehaviorNamed(const std::string& rName_) const
{
	for(const BoundAreaBehaviorData& rData : this->boundAreas.scriptBehaviors)
	{
		if(rData.name == rName_) return &rData;
	}
	return nullptr;
}

namespace
{
/* Replaces every use of a tag name in the rules of the behaviours. */
void renameTagIn(std::vector<BoundAreaBehaviorData>& rBehaviors_, const std::string& rName_, const std::string& rNewName_)
{
	auto rename = [&](std::vector<std::string>& rNames_)
	{
		for(std::string& rName : rNames_)
		{
			if(rName == rName_) rName = rNewName_;
		}
	};
	for(BoundAreaBehaviorData& rBehavior : rBehaviors_)
	{
		rename(rBehavior.tags);
		rename(rBehavior.collidesWith);
		for(BoundAreaRule& rRule : rBehavior.onTouch) rename(rRule.tags);
	}
}

bool isFixedTag(const BoundAreaData& rData_, const std::string& rName_)
{
	return std::find(rData_.fixedTags.begin(), rData_.fixedTags.end(), rName_) != rData_.fixedTags.end();
}
}

void BABehaviors::renameBoundAreaTag(const std::string& rName_, const std::string& rNewName_)
{
	auto listed = std::find(this->boundAreas.tags.begin(), this->boundAreas.tags.end(), rName_);
	if(listed == this->boundAreas.tags.end() || isFixedTag(this->boundAreas, rName_)) throw std::runtime_error(std::format("BABehaviors: the tag {} is not one the editor made, so it can't be renamed.", rName_));
	if(rNewName_.empty()) throw std::runtime_error("BABehaviors: a tag needs a name.");
	Globals::Game::baTags.rename(this->findTag(rName_.c_str()), rNewName_.c_str());
	*listed = rNewName_;
	renameTagIn(this->boundAreas.behaviors, rName_, rNewName_);
	renameTagIn(this->boundAreas.scriptBehaviors, rName_, rNewName_);
}

void BABehaviors::removeBoundAreaTag(const std::string& rName_)
{
	auto listed = std::find(this->boundAreas.tags.begin(), this->boundAreas.tags.end(), rName_);
	if(listed == this->boundAreas.tags.end() || isFixedTag(this->boundAreas, rName_)) throw std::runtime_error(std::format("BABehaviors: the tag {} is not one the editor made, so it can't be removed.", rName_));
	PoolId id = this->findTag(rName_.c_str());
	auto& rBehaviors = Globals::Game::behaviors;
	for(uint32_t b = 0; b < rBehaviors.objects.size(); b++)
	{
		if(!rBehaviors.alive[b]) continue;
		const BABehavior& rBehavior = rBehaviors.objects[b];
		bool used = rBehavior.availableCollisions.contains(id.id) || rBehavior.availableInteractions.contains(id.id) || std::find(rBehavior.tags.begin(), rBehavior.tags.end(), id) != rBehavior.tags.end();
		if(used) throw std::runtime_error(std::format("BABehaviors: the tag {} is used by the behaviour {}.", rName_, rBehaviors.names[b]));
	}
	Globals::Game::baTags.remove(id);
	this->boundAreas.tags.erase(listed);
}

void BABehaviors::renameBoundAreaBehavior(size_t index_, const std::string& rNewName_)
{
	BoundAreaBehaviorData& rData = this->boundAreas.behaviors.at(index_);
	if(this->scriptBehaviorNamed(rData.name) != nullptr) throw std::runtime_error(std::format("BABehaviors: {} is built in or made by a script, so it can't be renamed (customise it to change its rules).", rData.name));
	if(rNewName_.empty()) throw std::runtime_error("BABehaviors: a behaviour needs a name.");
	Globals::Game::behaviors.rename(this->find(rData.name.c_str()), rNewName_.c_str());
	rData.name = rNewName_;
}

void BABehaviors::removeBoundAreaBehavior(size_t index_)
{
	const BoundAreaBehaviorData& rData = this->boundAreas.behaviors.at(index_);
	if(this->scriptBehaviorNamed(rData.name) != nullptr) throw std::runtime_error(std::format("BABehaviors: {} is built in or made by a script, so it can't be removed (revert it).", rData.name));
	PoolId id = this->find(rData.name.c_str());
	uint32_t shapes = 0;
	auto& rCollisions = Globals::Physics::collisions;
	for(uint32_t c = 0; c < rCollisions.objects.size(); c++)
	{
		if(!rCollisions.alive[c]) continue;
		for(const Physics::CollisionShape& rShape : rCollisions.objects[c].shapes)
		{
			if(rShape.behavior == id) shapes++;
		}
	}
	if(shapes > 0) throw std::runtime_error(std::format("BABehaviors: {} is used by {} shapes: give them another behaviour first.", rData.name, shapes));
	Globals::Game::behaviors.remove(id);
	this->boundAreas.behaviors.erase(this->boundAreas.behaviors.begin() + index_);
}

size_t BABehaviors::customizeScriptBehavior(size_t index_)
{
	BoundAreaBehaviorData data = this->boundAreas.scriptBehaviors.at(index_);
	for(size_t i = 0; i < this->boundAreas.behaviors.size(); i++)
	{
		if(this->boundAreas.behaviors[i].name == data.name) return i;
	}
	this->boundAreas.behaviors.push_back(std::move(data));
	return this->boundAreas.behaviors.size() - 1;
}

void BABehaviors::revertBoundAreaBehavior(size_t index_)
{
	const BoundAreaBehaviorData* pScript = this->scriptBehaviorNamed(this->boundAreas.behaviors.at(index_).name);
	if(pScript == nullptr) throw std::runtime_error("BABehaviors: this behaviour is neither built in nor made by a script, so there is nothing to revert to.");
	this->applyBoundAreaData(*pScript);
	this->boundAreas.behaviors.erase(this->boundAreas.behaviors.begin() + index_);
}

PoolId BABehaviors::addBoundAreaTag(const std::string& rName_)
{
	if(rName_.empty()) throw std::runtime_error("BABehaviors: a tag needs a name.");
	PoolId id = this->addTag(BATagKey{rName_.c_str()});
	this->boundAreas.tags.push_back(rName_);
	return id;
}

size_t BABehaviors::addBoundAreaBehavior(const std::string& rName_)
{
	if(rName_.empty()) throw std::runtime_error("BABehaviors: a behaviour needs a name.");
	if(this->find(rName_.c_str()) != UninitializedPoolId) throw std::runtime_error(std::format("BABehaviors: a behaviour named {} exists already.", rName_));
	this->boundAreas.behaviors.push_back(BoundAreaBehaviorData{rName_, {}, {}, {}});
	this->applyBoundArea(this->boundAreas.behaviors.size() - 1);
	return this->boundAreas.behaviors.size() - 1;
}

namespace
{
std::vector<std::string> readNames(const nlohmann::json& rJson_, const char* key_)
{
	std::vector<std::string> names;
	if(rJson_.contains(key_)) rJson_.at(key_).get_to(names);
	return names;
}

nlohmann::json boundAreaJson(const BoundAreaData& rData_)
{
	nlohmann::json file = nlohmann::json::object();
	file["tags"] = rData_.tags;
	file["behaviors"] = nlohmann::json::array();
	for(const BoundAreaBehaviorData& rBehavior : rData_.behaviors)
	{
		nlohmann::json entry = {{"name", rBehavior.name}, {"tags", rBehavior.tags}, {"collidesWith", rBehavior.collidesWith}, {"onTouch", nlohmann::json::array()}};
		for(const BoundAreaRule& rRule : rBehavior.onTouch) entry["onTouch"].push_back({{"function", rRule.function}, {"tags", rRule.tags}});
		file["behaviors"].push_back(entry);
	}
	return file;
}
}

void BABehaviors::loadBoundAreas(const std::string& path_)
{
	std::ifstream stream(path_);
	if(!stream) return;
	try
	{
		nlohmann::json file = nlohmann::json::parse(stream);
		BoundAreaData data;
		data.tags = readNames(file, "tags");
		for(const nlohmann::json& rEntry : file.value("behaviors", nlohmann::json::array()))
		{
			BoundAreaBehaviorData behavior;
			behavior.name = rEntry.at("name").get<std::string>();
			behavior.tags = readNames(rEntry, "tags");
			behavior.collidesWith = readNames(rEntry, "collidesWith");
			for(const nlohmann::json& rRule : rEntry.value("onTouch", nlohmann::json::array()))
			{
				behavior.onTouch.push_back(BoundAreaRule{rRule.at("function").get<std::string>(), readNames(rRule, "tags")});
			}
			data.behaviors.push_back(std::move(behavior));
		}

		/*
		A tag that exists already (a script's, or a built-in one) is the same tag and is not the file's to rename or delete. A behaviour name must be new, except the name of a built-in behaviour or one a script made: the
		file's entry is that behaviour customised, and replaces the script's rules in place.
		*/
		for(const std::string& rTag : data.tags)
		{
			if(this->findTag(rTag.c_str()) == UninitializedPoolId) this->addTag(BATagKey{rTag.c_str()});
			else this->boundAreas.fixedTags.push_back(rTag);
		}
		this->boundAreas.tags = std::move(data.tags);
		this->boundAreas.behaviors = std::move(data.behaviors);
		for(size_t i = 0; i < this->boundAreas.behaviors.size(); i++)
		{
			const BoundAreaBehaviorData& rBehavior = this->boundAreas.behaviors[i];
			if(this->find(rBehavior.name.c_str()) != UninitializedPoolId && this->scriptBehaviorNamed(rBehavior.name) == nullptr) throw std::runtime_error(std::format("the behaviour name {} is taken.", rBehavior.name));
			this->applyBoundArea(i);
			for(const BoundAreaRule& rRule : rBehavior.onTouch)
			{
				if(!Globals::Game::boundAreaFunctions.nameToId.contains(rRule.function)) std::cerr << std::format("BoundAreas: behaviour {} names the onTouch function {}, which is not registered.", rBehavior.name, rRule.function) << std::endl;
			}
		}
	}
	catch(const nlohmann::json::exception& rError)
	{
		throw std::runtime_error(std::format("BABehaviors: {} is wrong: {}", path_, rError.what()));
	}
	catch(const std::logic_error& rError)
	{
		throw std::runtime_error(std::format("BABehaviors: {} is wrong: {}", path_, rError.what()));
	}
}

#ifdef EDITOR
void BABehaviors::saveBoundAreas() const
{
	std::string text = boundAreaJson(this->boundAreas).dump(2);
	for(const std::filesystem::path& rPath : {std::filesystem::path("Assets/External/Collision/BoundAreas.json"), std::filesystem::path(SF_SOURCE_DIR) / "External/Collision/BoundAreas.json"})
	{
		std::filesystem::create_directories(rPath.parent_path());
		std::ofstream out(rPath);
		if(!out) throw std::runtime_error(std::format("BABehaviors: can't write {}.", rPath.string()));
		out << text;
	}
}
#endif
