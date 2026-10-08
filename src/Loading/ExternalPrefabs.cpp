#include "ExternalPrefabs.h"
#include "Prefab.h"
#include "Globals.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>

namespace Loading
{
namespace
{
namespace fs = std::filesystem;
using OrderedJson = nlohmann::ordered_json;

/* Folders of each scope (Project, Level) and the level the Level scope belongs to ("" = none). The prefabs themselves live in Globals::Game::prefabs. */
std::vector<std::string> projectFolders;
std::vector<std::string> levelFolders;
std::string levelName;

std::vector<std::string>& foldersOf(ExternalScope scope_)
{
	return scope_ == ExternalScope::Project ? projectFolders : levelFolders;
}

/* The scope's file under a root: the game's copy ("Assets/...") and the source tree's (what the next build copies back). */
fs::path fileOf(ExternalScope scope_, bool inSource_)
{
	if(scope_ == ExternalScope::Project) return (inSource_ ? fs::path(SF_SOURCE_DIR) / "External" : fs::path("Assets/External")) / "Prefabs" / "Global" / "prefabs.json";
	return (inSource_ ? fs::path(SF_SOURCE_DIR) / "Levels" : fs::path("Assets/Levels")) / levelName / "prefabs.json";
}

/* A name that becomes a prefab's name or a folder's: letters, digits, '_', '-' and ' ' (a '/' would be a path). */
void requireName(const std::string& rName_)
{
	if(rName_.empty()) throw std::runtime_error("the name is empty.");
	for(char c : rName_)
	{
		if(!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != ' ') throw std::runtime_error("a name can only have letters, digits, '_', '-' and spaces.");
	}
}

void requireScope(ExternalScope scope_)
{
	if(scope_ == ExternalScope::Level && levelName.empty()) throw std::runtime_error("no level is open for the Level folder.");
}

/* Only External choices have a meaning for a prefab (a disc model needs the instance of a disc entity); anything else reads as Default. */
OrderedJson writeChoice(const std::string& rName_)
{
	return OrderedJson{{"kind", "external"}, {"name", rName_}};
}
}

const std::vector<std::string>& externalPrefabFolders(ExternalScope scope_)
{
	return foldersOf(scope_);
}

void saveExternalPrefabs(ExternalScope scope_)
{
	requireScope(scope_);
	bool inLevel = scope_ == ExternalScope::Level;
	std::vector<const Prefab*> prefabs;
	for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
	{
		if(!Globals::Game::prefabs.alive[i]) continue;
		const Prefab& rPrefab = Globals::Game::prefabs.objects[i];
		if(rPrefab.kind == PrefabKind::External && rPrefab.inLevel == inLevel) prefabs.push_back(&rPrefab);
	}
	std::sort(prefabs.begin(), prefabs.end(), [](const Prefab* a_, const Prefab* b_) { return a_->folder != b_->folder ? a_->folder < b_->folder : a_->name < b_->name; });

	OrderedJson file;
	std::vector<std::string> folders = foldersOf(scope_);
	std::sort(folders.begin(), folders.end());
	file["folders"] = folders;
	file["prefabs"] = OrderedJson::array();
	for(const Prefab* pPrefab : prefabs)
	{
		OrderedJson entry = {{"name", pPrefab->name}, {"folder", pPrefab->folder}};
		if(pPrefab->level.mesh.kind == MeshChoiceKind::External) entry["mesh"] = writeChoice(pPrefab->level.mesh.name);
		if(pPrefab->level.texture.kind == TextureChoiceKind::External) entry["texture"] = writeChoice(pPrefab->level.texture.name);
		file["prefabs"].push_back(entry);
	}

	std::string text = file.dump(2);
	for(bool inSource : {false, true})
	{
		fs::path path = fileOf(scope_, inSource);
		fs::create_directories(path.parent_path());
		std::ofstream out(path);
		if(!out) throw std::runtime_error(std::format("can't write {}.", path.string()));
		out << text;
	}
}

void loadExternalPrefabs(ExternalScope scope_, const std::string& levelName_)
{
	if(scope_ == ExternalScope::Level) levelName = levelName_;
	std::vector<std::string>& rFolders = foldersOf(scope_);
	rFolders.clear();
	std::ifstream stream(fileOf(scope_, false));
	if(!stream) return;
	try
	{
		OrderedJson file = OrderedJson::parse(stream);
		for(const OrderedJson& rFolder : file.value("folders", OrderedJson::array())) rFolders.push_back(rFolder.get<std::string>());
		for(const OrderedJson& rEntry : file.value("prefabs", OrderedJson::array()))
		{
			try
			{
				MeshChoice mesh;
				TextureChoice texture;
				if(rEntry.contains("mesh")) mesh = MeshChoice{MeshChoiceKind::External, rEntry.at("mesh").at("name").get<std::string>()};
				if(rEntry.contains("texture")) texture = TextureChoice{TextureChoiceKind::External, rEntry.at("texture").at("name").get<std::string>()};
				std::string folder = rEntry.value("folder", std::string());
				if(!folder.empty() && std::find(rFolders.begin(), rFolders.end(), folder) == rFolders.end()) rFolders.push_back(folder);
				addExternalPrefab(scope_ == ExternalScope::Level, rEntry.at("name").get<std::string>(), folder, mesh, texture);
			}
			catch(const std::exception& rError)
			{
				std::fprintf(stderr, "External prefab skipped (%s): %s\n", fileOf(scope_, false).string().c_str(), rError.what());
			}
		}
	}
	catch(const std::exception& rError)
	{
		std::fprintf(stderr, "External prefabs not read (%s): %s\n", fileOf(scope_, false).string().c_str(), rError.what());
	}
	std::sort(rFolders.begin(), rFolders.end());
}

void unloadLevelPrefabs()
{
	levelName.clear();
	levelFolders.clear();
	removeLevelPrefabs();
}

PoolId createExternalPrefab(ExternalScope scope_, const std::string& folder_, const std::string& name_)
{
	requireScope(scope_);
	requireName(name_);
	std::vector<std::string>& rFolders = foldersOf(scope_);
	if(!folder_.empty() && std::find(rFolders.begin(), rFolders.end(), folder_) == rFolders.end()) throw std::runtime_error(std::format("there is no folder '{}'.", folder_));
	PoolId id = addExternalPrefab(scope_ == ExternalScope::Level, name_, folder_, MeshChoice{}, TextureChoice{});
	try
	{
		saveExternalPrefabs(scope_);
	}
	catch(...)
	{
		Globals::Game::prefabs.remove(id);
		throw;
	}
	return id;
}

void createExternalPrefabFolder(ExternalScope scope_, const std::string& parent_, const std::string& name_)
{
	requireScope(scope_);
	requireName(name_);
	std::vector<std::string>& rFolders = foldersOf(scope_);
	if(!parent_.empty() && std::find(rFolders.begin(), rFolders.end(), parent_) == rFolders.end()) throw std::runtime_error(std::format("there is no folder '{}'.", parent_));
	std::string path = parent_.empty() ? name_ : parent_ + "/" + name_;
	if(std::find(rFolders.begin(), rFolders.end(), path) != rFolders.end()) throw std::runtime_error(std::format("the folder '{}' exists.", path));
	rFolders.push_back(path);
	try
	{
		saveExternalPrefabs(scope_);
	}
	catch(...)
	{
		rFolders.pop_back();
		throw;
	}
	std::sort(rFolders.begin(), rFolders.end());
}
}
