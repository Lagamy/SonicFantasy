#include "PlayerLoader.h"
#include <nlohmann/json.hpp>
#include <deque>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace Loading
{
namespace
{
using Json = nlohmann::json;

/* The keys hold const char* and are used for the whole run, so the strings live here; a deque never moves them. */
std::deque<std::string> strings;
std::unique_ptr<SonicKeys> sonic;
std::unique_ptr<ShadowKeys> shadow;
/* Motions are kept in deques: the animation tables point at them. */
std::deque<SAMotionKey> sonicMotions;
std::deque<STH2005::MotionKey> shadowMotions;

const char* keep(const Json& rValue_)
{
	strings.push_back(rValue_.get<std::string>());
	return strings.back().c_str();
}

/* An offset is a number or a string in any base ("0x1B8D68"), like the level files'. */
uint32_t readOffset(const Json& rValue_)
{
	if(rValue_.is_string()) return static_cast<uint32_t>(std::stoul(rValue_.get<std::string>(), nullptr, 0));
	return rValue_.get<uint32_t>();
}

/* An optional string: nullptr when the key is absent. */
const char* keepOptional(const Json& rObject_, const char* pField_)
{
	return rObject_.contains(pField_) ? keep(rObject_.at(pField_)) : nullptr;
}

Json readFile(const std::string& rPath_)
{
	std::ifstream stream(rPath_);
	if(!stream) throw std::runtime_error(std::format("Players: can't read {}.", rPath_));
	try
	{
		return Json::parse(stream);
	}
	catch(const Json::exception& rError)
	{
		throw std::runtime_error(std::format("Players: {} is wrong: {}", rPath_, rError.what()));
	}
}

SAModelKey readSAModel(const Json& rObject_)
{
	return SAModelKey{keep(rObject_.at("rel")), readOffset(rObject_.at("offset"))};
}

void loadSonic(const std::string& rPath_)
{
	Json file = readFile(rPath_);
	auto keys = std::make_unique<SonicKeys>();
	keys->textures = SATextureSetKey{keep(file.at("textures"))};
	for(const auto& [name, rModel] : file.at("models").items()) keys->models.emplace(name, readSAModel(rModel));
	keys->mesh = keys->models.at("body");
	const Json& rIdle = file.at("idle");
	keys->idle = SAMotionKey{keep(rIdle.at("rel")), readOffset(rIdle.at("offset")), rIdle.at("nodes").get<uint32_t>()};
	for(const auto& [name, rAnimation] : file.at("animations").items())
	{
		sonicMotions.push_back(SAMotionKey{keep(rAnimation.at("rel")), readOffset(rAnimation.at("offset")), rAnimation.at("nodes").get<uint32_t>()});
		std::string modelName = rAnimation.at("model").get<std::string>();
		auto model = keys->models.find(modelName);
		if(model == keys->models.end()) throw std::runtime_error(std::format("animation '{}' names the unknown model '{}'", name, modelName));
		keys->animations.emplace(name, SonicAnimation{&sonicMotions.back(), &model->second});
	}
	sonic = std::move(keys);
}

STH2005::MotionKey readShadowMotion(const STH2005::ArchiveKey& rArchive_, const Json& rObject_)
{
	return STH2005::MotionKey{rArchive_, keep(rObject_.at("mtp")), keep(rObject_.at("motion")), keep(rObject_.at("bon")), rObject_.value("attachedPart", false)};
}

void loadShadow(const std::string& rPath_)
{
	Json file = readFile(rPath_);
	STH2005::ArchiveKey archive{keep(file.at("archive"))};
	auto keys = std::make_unique<ShadowKeys>();
	const Json& rModel = file.at("model");
	std::vector<STH2005::ModelPart> parts;
	for(const Json& rPart : rModel.at("parts")) parts.push_back(STH2005::ModelPart{keep(rPart.at("dff")), keepOptional(rPart, "bon"), keepOptional(rPart, "bone")});
	keys->mesh = STH2005::ModelKey{archive, keep(rModel.at("name")), parts};
	keys->textures = STH2005::TextureKey{archive, keep(file.at("textures"))};
	for(const Json& rMotion : file.at("idle")) keys->idle.push_back(readShadowMotion(archive, rMotion));
	for(const auto& [name, rAnimation] : file.at("animations").items())
	{
		shadowMotions.push_back(readShadowMotion(archive, rAnimation));
		keys->animations.emplace(name, ShadowAnimation{&shadowMotions.back(), rAnimation.at("loop").get<bool>()});
	}
	shadow = std::move(keys);
}
}

void loadPlayerKeys()
{
	const std::string folder = "Assets/External/Players/";
	try
	{
		loadSonic(folder + "Sonic.json");
	}
	catch(const Json::exception& rError)
	{
		throw std::runtime_error(std::format("Players: {}Sonic.json is wrong: {}", folder, rError.what()));
	}
	try
	{
		loadShadow(folder + "Shadow.json");
	}
	catch(const Json::exception& rError)
	{
		throw std::runtime_error(std::format("Players: {}Shadow.json is wrong: {}", folder, rError.what()));
	}
}

const SonicKeys& sonicKeys()
{
	if(!sonic) throw std::logic_error("Players: loadPlayerKeys has not run.");
	return *sonic;
}

const ShadowKeys& shadowKeys()
{
	if(!shadow) throw std::logic_error("Players: loadPlayerKeys has not run.");
	return *shadow;
}
}
