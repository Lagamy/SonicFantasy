#include "AssetPanel.h"
#include "Prefab.h"
#include "ExternalAssets.h"
#include "GPU_Globals.h"
#include "Globals.h"
#include "Level.h"
#include "API.h"
#include "Scripting.h"
#include "EntityBehavior.h"
#include "imgui.h"
#include <stb_image.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace Graphics
{
namespace
{
constexpr float TileSize = 72.0f;

/* How many times the rows on screen are also kept loaded above and below them (2 = about five times what is on screen stays loaded, so a small scroll does not unload and reload from the disc). */
constexpr int PrefetchRows = 2;
constexpr const char* CatalogPath = "Assets/Catalog/AssetCatalog.tsv";
constexpr const char* FolderIconPath = "Assets/Catalog/folder_icon.png";
constexpr const char* ScriptsPath = "Assets/Scripts";
constexpr const char* ScriptFolders[2] = {"sadx", "shadow"};
constexpr const char* SourceScriptsFolder = "Scripts";
constexpr const char* KindFolders[3] = {"Models", "Textures", "Scripts"};
constexpr const char* KindLabels[3] = {"MDL", "TEX", "LUA"};

/* The flat colour of a kind's placeholder picture: blue-grey models, green textures, yellow scripts. */
ImVec4 kindColor(int kind_)
{
	if(kind_ == 0) return ImVec4(0.25f, 0.35f, 0.5f, 1.0f);
	if(kind_ == 1) return ImVec4(0.25f, 0.5f, 0.3f, 1.0f);
	return ImVec4(0.55f, 0.5f, 0.2f, 1.0f);
}

/* The two hardcoded folders of the external assets, next to the games: Global (every level) and Level (the open level's). */
const char* externalLabel(int game_)
{
	return game_ == 2 ? "Global" : "Level";
}

/* Where a game's scripts are in the build's Assets (game 2 = Global's External) and in the source tree, which the build copies over them (CopyScripts, CopyExternal); an edit has to be in both. */
std::filesystem::path buildScriptRoot(int game_)
{
	return game_ == 2 ? std::filesystem::path(Scripting::ExternalScriptsRoot) : std::filesystem::path(ScriptsPath) / ScriptFolders[game_];
}

std::filesystem::path sourceScriptRoot(int game_)
{
	std::filesystem::path root = SF_SOURCE_DIR;
	return game_ == 2 ? root / "External" / "Scripts" : root / SourceScriptsFolder / ScriptFolders[game_];
}

/* The game and the path inside its script root of a script file's path in the build's Assets (Asset::source); false if it is in none of the roots. */
bool locateScript(const std::string& rSource_, int& rGame_, std::filesystem::path& rRelative_)
{
	for(int game = 0; game < 3; game++)
	{
		std::filesystem::path relative = std::filesystem::path(rSource_).lexically_relative(buildScriptRoot(game));
		if(relative.empty() || *relative.begin() == "..") continue;
		rGame_ = game;
		rRelative_ = relative;
		return true;
	}
	return false;
}

/* The text in lower case (ASCII), for comparing names without regard to case. */
std::string lowerCase(std::string text_)
{
	for(char& rChar : text_) rChar = static_cast<char>(std::tolower(static_cast<unsigned char>(rChar)));
	return text_;
}

/* The name an asset is matched by between the catalog and an entity's disc mesh / texture (ASCII lower case, hypothesis that the names agree): SADX models "<rel>@0x<offset>" (spaces dropped), SADX textures their file name, Shadow ones the entry name after " > " without ".dff" / ".txd". */
std::string matchKey(int game_, int kind_, std::string name_)
{
	size_t arrow = name_.find(" > ");
	if(game_ == 1 && arrow != std::string::npos) name_ = name_.substr(arrow + 3);
	if(game_ == 0 && kind_ == 0) name_.erase(std::remove(name_.begin(), name_.end(), ' '), name_.end());
	if(game_ == 0 && kind_ == 1) name_ = std::filesystem::path(name_).filename().generic_string();
	for(char& rChar : name_) rChar = static_cast<char>(std::tolower(static_cast<unsigned char>(rChar)));
	if(game_ == 1 && name_.size() > 4 && (name_.ends_with(".dff") || name_.ends_with(".txd"))) name_.resize(name_.size() - 4);
	return name_;
}

/* The text broken into two lines of at most the width (the break is wherever the first line is full), with ".." at the end of the second if it was cut. A short text still gets two lines, so the tiles of a row stay aligned. */
std::string fitText(const std::string& text_, float width_)
{
	std::string first;
	size_t used = 0;
	while(used < text_.size() && ImGui::CalcTextSize((first + text_[used]).c_str()).x <= width_) first += text_[used++];
	if(used == 0 && !text_.empty()) first = text_.substr(0, ++used);
	std::string second = text_.substr(used);
	if(ImGui::CalcTextSize(second.c_str()).x > width_)
	{
		while(!second.empty() && ImGui::CalcTextSize((second + "..").c_str()).x > width_) second.pop_back();
		second += "..";
	}
	return first + "\n" + second;
}

/* The contents of a drag's floating preview: the tile's picture (when it has one; flipped like the tile's) over the name. */
void dragPreview(GLuint texture_, bool flipped_, const std::string& name_)
{
	if(texture_ != 0) ImGui::Image(static_cast<ImTextureID>(texture_), ImVec2(TileSize, TileSize), ImVec2(0.0f, flipped_ ? 1.0f : 0.0f), ImVec2(1.0f, flipped_ ? 0.0f : 1.0f));
	ImGui::TextUnformatted(name_.c_str());
}
}

void AssetPanel::load()
{
	this->loaded = true;
	this->searchStale = true;
	this->loadCatalog();
	this->loadScripts();
	this->loadExternal();
	for(int game = 0; game < 2; game++)
	{
		this->sortAndCount(game);
	}
}

void AssetPanel::sortAndCount(int game_)
{
	for(int kind = 0; kind < KindCount; kind++)
	{
		this->counts[game_][kind] = 0;
		for(auto& rList : this->assets[game_][kind])
		{
			std::sort(rList.begin(), rList.end(), [](const Asset& a_, const Asset& b_) { return a_.name < b_.name; });
			this->counts[game_][kind] += static_cast<uint32_t>(rList.size());
		}
	}
}

const Level* AssetPanel::openLevel()
{
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(Globals::Game::levels.alive[i]) return &Globals::Game::levels.objects[i];
	}
	return nullptr;
}

void AssetPanel::refreshLevel()
{
	const ::Level* pLevel = openLevel();
	const std::vector<PoolId>& rSpawned = Scripting::spawnedEntities();
	std::vector<PoolId> referenced;
	if(pLevel != nullptr) referenced = pLevel->referencedPrefabs();
	uint64_t seen = pLevel == nullptr ? 0 : (reinterpret_cast<uintptr_t>(pLevel) * 31 + pLevel->entityIds.size()) * 31 + rSpawned.size();
	for(PoolId id : referenced) seen = seen * 31 + id.id + 1;
	if(seen == this->levelSeen) return;
	this->levelSeen = seen;
	this->searchStale = true;

	/* Per game the keys of the meshes and textures the entities draw, and the script files of their behaviours: those of the level's own entities, and apart those of the entities a script spawned (Scripting::spawnedEntities, the players), which the tiles show in another colour. */
	struct Used
	{
		std::array<std::unordered_set<std::string>, 2> meshes;
		std::array<std::unordered_set<std::string>, 2> textures;
		std::unordered_set<std::string> scripts;
	};
	Used level;
	Used spawned;
	auto collect = [](Used& rUsed_, PoolId id_)
	{
		if(!entityExists(id_)) return;
		const Entity& rEntity = Globals::Game::entities[id_];
		if(rEntity.behaviorSetId != UninitializedPoolId)
		{
			for(PoolId behaviorId : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds) rUsed_.scripts.insert(lowerCase(Globals::Game::entityBehaviorTypes[behaviorId].sourceFile));
		}
		int game = rEntity.sourceEngine == SA_NINJA ? 0 : rEntity.sourceEngine == STH2005_ONE ? 1 : -1;
		if(game < 0 || rEntity.instanceId == UninitializedPoolId) return;
		try
		{
			rUsed_.meshes[game].insert(matchKey(game, Model, rEntity.discMesh()));
			std::string texture = rEntity.discTexture();
			if(!texture.empty()) rUsed_.textures[game].insert(matchKey(game, Texture, texture));
		}
		catch(const std::exception&)
		{
		}
	};
	if(pLevel != nullptr)
	{
		for(PoolId id : pLevel->entityIds) collect(level, id);
		for(PoolId id : rSpawned) collect(spawned, id);

		/* A prefab in a prefab field has no entity of its own to read its model from: one that exists (placed in the level, or live now) stands for it. */
		for(PoolId prefab : referenced)
		{
			for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
			{
				if(!Globals::Game::entities.alive[i] || Globals::Game::entities.objects[i].prefabId != prefab) continue;
				collect(level, Globals::Game::entities.objects[i].selfId);
				break;
			}
		}
	}
	for(int game = 0; game < 2; game++)
	{
		this->levelCounts[game] = {};
		for(int kind = 0; kind < KindCount; kind++)
		{
			for(auto& rList : this->assets[game][kind])
			{
				for(Asset& rAsset : rList)
				{
					auto has = [&](const Used& rUsed_)
					{
						if(kind == Script) return rUsed_.scripts.contains(lowerCase(rAsset.source));
						return (kind == Model ? rUsed_.meshes : rUsed_.textures)[game].contains(matchKey(game, kind, rAsset.source));
					};
					rAsset.spawned = has(spawned);
					rAsset.inLevel = rAsset.spawned || has(level);
					if(rAsset.inLevel) this->levelCounts[game][kind]++;
				}
			}
		}
	}
}

std::vector<int> AssetPanel::levelIndices(int game_, int kind_, int group_) const
{
	std::vector<int> indices;
	const std::vector<Asset>& rList = this->assets[game_][kind_][group_];
	for(int i = 0; i < static_cast<int>(rList.size()); i++)
	{
		if(rList[i].inLevel) indices.push_back(i);
	}
	return indices;
}

/* The Global folder's assets (the project's pools) and the Level folder's (the open level's pools), each by name. A texture tile draws the GL texture itself. */
void AssetPanel::loadExternal()
{
	this->externalSeen = Loading::externalRevision();
	this->searchStale = true;
	for(int game = Global; game < GameCount; game++)
	{
		for(int kind = 0; kind < Script; kind++) this->assets[game][kind][0].clear();
	}
	if(this->selectedGame >= Global) this->selectedGame = -1;
	for(int game = Global; game < GameCount; game++)
	{
		Graphics::ExternalPools& rPools = game == Global ? Graphics::externalProject : Graphics::externalLevel;
		const char* source = game == Global ? "Global" : "Level";
		for(uint32_t i = 0; i < rPools.models.allocatedSize(); i++)
		{
			if(rPools.models.alive[i]) this->assets[game][Model][0].push_back({rPools.models.names[i], source});
		}
		for(uint32_t i = 0; i < rPools.textures.allocatedSize(); i++)
		{
			if(rPools.textures.alive[i]) this->assets[game][Texture][0].push_back({rPools.textures.names[i], source, "", false, false, rPools.textures.getInternal(i).textureId});
		}
		this->sortAndCount(game);
	}
}

/* One row per tab-separated line: game, kind, group, name, source (see tools/genAssetCatalog.py); a line that does not fit is skipped. */
void AssetPanel::loadCatalog()
{
	std::ifstream stream(CatalogPath);
	if(!stream)
	{
		std::fprintf(stderr, "AssetPanel: %s not found (tools/genAssetCatalog.py makes Catalog/AssetCatalog.tsv).\n", CatalogPath);
		return;
	}
	std::string line;
	while(std::getline(stream, line))
	{
		std::vector<std::string> fields;
		std::stringstream parts(line);
		std::string field;
		while(std::getline(parts, field, '\t')) fields.push_back(field);
		if(fields.size() != 5) continue;
		int game = std::atoi(fields[0].c_str());
		int kind = std::atoi(fields[1].c_str());
		int group = std::atoi(fields[2].c_str());
		if(game < 0 || game > 1 || kind < 0 || kind > 1 || group < 0 || group > 4) continue;
		this->assets[game][kind][group].push_back({fields[3], fields[4]});
	}
}

/* Every .lua file below Scripts/<game folder>, named by its file name, with its folder inside that folder in Asset::folder ("replays"); and, under Global's External, every one below Assets/External/Scripts (the project's own, Scripting.h). */
void AssetPanel::loadScripts()
{
	namespace fs = std::filesystem;
	auto scan = [&](const fs::path& rRoot_, int game_)
	{
		std::error_code error;
		for(fs::recursive_directory_iterator it(rRoot_, error), end; !error && it != end; it.increment(error))
		{
			if(it->is_directory())
			{
				this->scriptDirs[game_].push_back(fs::relative(it->path(), rRoot_).generic_string());
				continue;
			}
			if(!it->is_regular_file() || it->path().extension() != ".lua") continue;
			fs::path relative = fs::relative(it->path(), rRoot_);
			this->assets[game_][Script][0].push_back({relative.filename().generic_string(), it->path().generic_string(), relative.parent_path().generic_string()});
		}
	};
	for(int game = 0; game < 2; game++) scan(fs::path(ScriptsPath) / ScriptFolders[game], game);
	scan(fs::path(Scripting::ExternalScriptsRoot), Global);
}

void AssetPanel::reloadScripts()
{
	for(int game = 0; game <= Global; game++)
	{
		this->assets[game][Script][0].clear();
		this->scriptDirs[game].clear();
	}
	this->loadScripts();
	for(int game = 0; game <= Global; game++) this->sortAndCount(game);
	this->searchStale = true;
	this->levelSeen = ~0ull;
	this->selectedGame = -1;
}

/* A name that becomes a file or folder name: no path separators, not "." / "..". */
static bool usableScriptName(const std::string& rName_)
{
	return !rName_.empty() && rName_.find_first_of("/\\") == std::string::npos && rName_ != "." && rName_ != "..";
}

/* Both copies are made, so the Assets one (what runs and what the panel lists) and the source one (what stays) agree; nothing is loaded by the game before the next start (Scripting::loadExternalScripts), and a new file is not read at all unless main.lua includes it or it is in Entity/ or BoundArea/. */
void AssetPanel::createScript(bool folder_)
{
	namespace fs = std::filesystem;
	std::string name = this->newName;
	if(!usableScriptName(name))
	{
		this->scriptMessage = "Not a usable name.";
		return;
	}
	if(!folder_ && fs::path(name).extension() != ".lua") name += ".lua";
	fs::path relative = fs::path(this->dialogFolder) / name;
	fs::path build = buildScriptRoot(this->dialogGame) / relative;
	fs::path source = sourceScriptRoot(this->dialogGame) / relative;
	std::error_code error;
	if(fs::exists(build, error) || fs::exists(source, error))
	{
		this->scriptMessage = name + " exists already.";
		return;
	}
	for(const fs::path& rPath : {build, source})
	{
		if(folder_) fs::create_directories(rPath, error);
		else
		{
			fs::create_directories(rPath.parent_path(), error);
			if(!error) std::ofstream(rPath).put('\n');
		}
		if(error)
		{
			this->scriptMessage = "Can't create " + rPath.generic_string() + ": " + error.message();
			return;
		}
	}
	this->scriptMessage = "Created " + relative.generic_string();
	this->reloadScripts();
}

/* Only ever asked for in Global's External (the games' own folders are not deleted from here); the folder and everything in it goes, from Assets and from the source tree. */
void AssetPanel::deleteScriptFolder()
{
	namespace fs = std::filesystem;
	if(this->dialogGame != Global || this->deleteSource.empty())
	{
		this->scriptMessage = "Only External folders can be deleted.";
		return;
	}
	std::error_code error;
	std::error_code sourceError;
	fs::remove_all(buildScriptRoot(Global) / this->deleteSource, error);
	fs::remove_all(sourceScriptRoot(Global) / this->deleteSource, sourceError);
	if(error || sourceError)
	{
		this->scriptMessage = "Delete failed: " + (error ? error : sourceError).message();
		return;
	}
	if(this->scriptFolder == this->deleteSource || this->scriptFolder.starts_with(this->deleteSource + "/")) this->scriptFolder.clear();
	this->scriptFile.clear();
	this->scriptMessage = "Deleted folder " + this->deleteSource;
	this->reloadScripts();
}

void AssetPanel::pasteScript()
{
	namespace fs = std::filesystem;
	fs::path from = this->copiedSource;
	std::error_code error;
	if(!fs::is_regular_file(from, error))
	{
		this->scriptMessage = "The copied file is gone.";
		return;
	}
	int game = this->nav.openGame;
	fs::path folder = fs::path(this->scriptFolder);
	std::string stem = from.stem().string();
	fs::path name = from.filename();
	for(int n = 1; fs::exists(buildScriptRoot(game) / folder / name, error) || fs::exists(sourceScriptRoot(game) / folder / name, error); n++)
	{
		name = stem + "_copy" + (n > 1 ? std::to_string(n) : "") + ".lua";
	}
	for(const fs::path& rRoot : {buildScriptRoot(game), sourceScriptRoot(game)})
	{
		fs::create_directories(rRoot / folder, error);
		if(!error) fs::copy_file(from, rRoot / folder / name, error);
		if(error)
		{
			this->scriptMessage = "Paste failed: " + error.message();
			return;
		}
	}
	this->scriptMessage = "Pasted " + (folder / name).generic_string();
	this->reloadScripts();
}

void AssetPanel::deleteScript()
{
	namespace fs = std::filesystem;
	int game = 0;
	fs::path relative;
	if(!locateScript(this->deleteSource, game, relative))
	{
		this->scriptMessage = "Not a script of the project.";
		return;
	}
	std::error_code error;
	fs::remove(buildScriptRoot(game) / relative, error);
	std::error_code sourceError;
	fs::remove(sourceScriptRoot(game) / relative, sourceError);
	if(error || sourceError)
	{
		this->scriptMessage = "Delete failed: " + (error ? error : sourceError).message();
		return;
	}
	if(this->scriptFile == this->deleteSource) this->scriptFile.clear();
	this->scriptMessage = "Deleted " + relative.generic_string();
	this->reloadScripts();
}

/* The source tree's copy is opened (the build's would be overwritten by the next build); the build's if the source one is missing. The command runs through the shell in the background with the path in quotes. */
void AssetPanel::openScript(const std::string& source_)
{
	namespace fs = std::filesystem;
	int game = 0;
	fs::path relative;
	fs::path path = source_;
	if(locateScript(source_, game, relative) && fs::exists(sourceScriptRoot(game) / relative)) path = sourceScriptRoot(game) / relative;
	std::string quoted;
#ifdef _WIN32
	quoted = "\"" + path.string() + "\"";
	std::string command = this->scriptEditorCommand[0] == 0 ? "start \"\"" : std::string(this->scriptEditorCommand);
	std::string line = command + " " + quoted;
#else
	quoted = "'";
	for(char c : path.string()) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
	quoted += "'";
	std::string command = this->scriptEditorCommand[0] == 0 ? "xdg-open" : std::string(this->scriptEditorCommand);
	std::string line = command + " " + quoted + " >/dev/null 2>&1 &";
#endif
	if(std::system(line.c_str()) != 0) this->scriptMessage = "Could not run: " + command;
}

/* Buttons are only asked for here; the popups are opened in drawScriptDialogs, which is outside the tile child (a popup must be opened and drawn at the same ID stack level). */
void AssetPanel::drawScriptToolbar()
{
	if(ImGui::SmallButton("New Lua file"))
	{
		this->dialogRequest = NewFile;
		this->newIsFolder = false;
		this->dialogGame = this->nav.openGame;
		this->dialogFolder = this->scriptFolder;
		this->newName[0] = 0;
	}
	ImGui::SameLine();
	if(ImGui::SmallButton("New folder"))
	{
		this->dialogRequest = NewFolder;
		this->newIsFolder = true;
		this->dialogGame = this->nav.openGame;
		this->dialogFolder = this->scriptFolder;
		this->newName[0] = 0;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(this->copiedSource.empty());
	if(ImGui::SmallButton("Paste") && !this->copiedSource.empty()) this->pasteScript();
	ImGui::EndDisabled();
	if(!this->copiedSource.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", this->copiedSource.c_str());
	ImGui::SameLine();
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputTextWithHint("##scriptEditor", "open with (system default)", this->scriptEditorCommand, sizeof(this->scriptEditorCommand));
	if(!this->scriptMessage.empty())
	{
		ImGui::SameLine();
		ImGui::TextUnformatted(this->scriptMessage.c_str());
	}
}

void AssetPanel::drawScriptDialogs()
{
	if(this->dialogRequest == NewFile || this->dialogRequest == NewFolder) ImGui::OpenPopup("New script item");
	if(this->dialogRequest == Delete) ImGui::OpenPopup("Delete script");
	if(this->dialogRequest == DeleteFolder) ImGui::OpenPopup("Delete script folder");
	if(this->dialogRequest == NewFile || this->dialogRequest == NewFolder) this->scriptMessage.clear();
	this->dialogRequest = None;
	if(ImGui::BeginPopupModal("New script item", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("New %s in %s", this->newIsFolder ? "folder" : "Lua file", this->dialogFolder.empty() ? "the root" : this->dialogFolder.c_str());
		bool entered = ImGui::InputText("##name", this->newName, sizeof(this->newName), ImGuiInputTextFlags_EnterReturnsTrue);
		if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
		if(ImGui::Button("Create") || entered)
		{
			this->createScript(this->newIsFolder);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	if(ImGui::BeginPopupModal("Delete script folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(this->deleteSource.c_str());
		ImGui::TextUnformatted("Deletes the folder and every file in it from Assets and from the source tree. It cannot be undone here.");
		if(ImGui::Button("Delete"))
		{
			this->deleteScriptFolder();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	if(ImGui::BeginPopupModal("Delete script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(std::filesystem::path(this->deleteSource).filename().generic_string().c_str());
		ImGui::TextUnformatted("Deletes the file from Assets and from the source tree. It cannot be undone here.");
		if(ImGui::Button("Delete"))
		{
			this->deleteScript();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

/* The tiles of one list as a grid of rows; only the rows on screen are drawn (the lists have thousands of tiles). Each tile: a flat picture of the kind's colour with its short label, the name under it, a tooltip with the source. */
void AssetPanel::drawTiles(int game_, int kind_, int group_, const std::vector<int>* pOnly_)
{
	const std::vector<Asset>& rList = this->assets[game_][kind_][group_];
	const ImGuiStyle& rStyle = ImGui::GetStyle();
	float cell = TileSize + 2.0f * rStyle.FramePadding.x;
	int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + rStyle.ItemSpacing.x) / (cell + rStyle.ItemSpacing.x)));
	int count = pOnly_ != nullptr ? static_cast<int>(pOnly_->size()) : static_cast<int>(rList.size());
	int rows = (count + columns - 1) / columns;
	float rowHeight = TileSize + 2.0f * rStyle.FramePadding.y + rStyle.ItemSpacing.y + 2.0f * ImGui::GetTextLineHeight() + rStyle.ItemSpacing.y;

	ImGuiListClipper clipper;
	clipper.Begin(rows, rowHeight);
	int firstRow = rows;
	int lastRow = 0;
	while(clipper.Step())
	{
		firstRow = std::min(firstRow, clipper.DisplayStart);
		lastRow = std::max(lastRow, clipper.DisplayEnd);
		for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
		{
			for(int column = 0; column < columns; column++)
			{
				int position = row * columns + column;
				if(position >= count) break;
				int i = pOnly_ != nullptr ? (*pOnly_)[position] : position;
				if(column != 0) ImGui::SameLine();

				ImGui::PushID(i);
				ImGui::BeginGroup();
				bool selected = this->selectedGame == game_ && this->selectedKind == kind_ && this->selectedGroup == group_ && this->selectedIndex == i;
				ImVec4 color = kindColor(kind_);
				if(rList[i].spawned) color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
				if(selected) color = ImVec4(0.2f, 0.4f, 0.8f, 1.0f);
				GLuint texture = 0;
				if(game_ >= Global) texture = kind_ == Texture ? rList[i].glTexture : 0;
				else if(kind_ != Script && ImGui::IsRectVisible(ImVec2(TileSize, TileSize))) texture = this->thumbnails.get(game_, rList[i].source, kind_ == Model);
				/* A tile of a script-spawned entity's asset has orange button, hover and press colors (blue by default; a thumbnail's frame shows through its background, so the button color matters at rest too), unless selected. */
				bool orange = rList[i].spawned && !selected;
				/* A script file is the label "LUA" on light green, with dark text so it reads on it (blue when selected, like the others). */
				bool lua = kind_ == Script && !selected;
				if(lua)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.85f, 0.55f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.92f, 0.65f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.97f, 0.75f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.2f, 0.1f, 1.0f));
				}
				if(orange)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.7f, 0.25f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.85f, 0.5f, 1.0f));
				}
				bool clicked;
				if(texture != 0)
				{
					ImVec4 background = selected ? ImVec4(0.2f, 0.4f, 0.8f, 0.7f) : rList[i].spawned ? ImVec4(1.0f, 0.5f, 0.0f, 0.7f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
					clicked = ImGui::ImageButton("##tile", static_cast<ImTextureID>(texture), ImVec2(TileSize, TileSize), ImVec2(0.0f, kind_ == Model ? 1.0f : 0.0f), ImVec2(1.0f, kind_ == Model ? 0.0f : 1.0f), background);
				}
				else
				{
					if(!lua) ImGui::PushStyleColor(ImGuiCol_Button, color);
					clicked = ImGui::Button(KindLabels[kind_], ImVec2(cell, TileSize + 2.0f * rStyle.FramePadding.y));
					if(!lua) ImGui::PopStyleColor();
				}
				if(orange)
				{
					ImGui::PopStyleColor(3);
				}
				if(lua) ImGui::PopStyleColor(4);
				if(clicked)
				{
					this->selectedGame = game_;
					this->selectedKind = kind_;
					this->selectedGroup = group_;
					this->selectedIndex = i;
					/* A script file is a folder in disguise: a click (outside the search) opens it to show its behaviours. */
					if(kind_ == Script && !this->searching)
					{
						this->scriptFile = rList[i].source;
						this->scriptFolder = rList[i].folder;
					}
				}
				/* A script tile's menu: open it with the chosen application, or ask to delete it (the dialog is drawn by drawScriptDialogs). */
				if(kind_ == Script && ImGui::BeginPopupContextItem("##scriptMenu"))
				{
					if(ImGui::MenuItem("Open in editor")) this->openScript(rList[i].source);
					if(ImGui::MenuItem("Copy")) this->copiedSource = rList[i].source;
					if(ImGui::MenuItem("Delete"))
					{
						this->dialogRequest = Delete;
						this->deleteSource = rList[i].source;
					}
					ImGui::EndPopup();
				}
				/* A script tile can be dropped on the inspector's Behaviour tab: the payload is the file's path, with the '\0'. */
				if(kind_ == Script && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("SCRIPT", rList[i].source.c_str(), rList[i].source.size() + 1);
					dragPreview(texture, false, rList[i].name);
					ImGui::EndDragDropSource();
				}
				/* A texture tile can be dropped on the inspector's Texture tab: "external" or "disc", a tab, then the name (an external texture's; a SADX .GVM's path; a Shadow .TXD's entry name, the part after " > " of the source), with the '\0'. */
				else if(kind_ == Texture && ImGui::BeginDragDropSource())
				{
					std::string payload;
					if(game_ >= Global) payload = "external\t" + rList[i].name;
					else
					{
						size_t arrow = rList[i].source.find(" > ");
						payload = "disc\t" + (game_ == 1 && arrow != std::string::npos ? rList[i].source.substr(arrow + 3) : rList[i].source);
					}
					ImGui::SetDragDropPayload("TEXTURE", payload.c_str(), payload.size() + 1);
					dragPreview(texture, false, rList[i].name);
					ImGui::EndDragDropSource();
				}
				/* A model tile can be dropped on the inspector's Mesh tab: "external" or "disc", a tab, then the name (an external model's; a SADX model's "<rel>@0x<offset in hex>", the name of its SAMesh, made from the source "<rel> @ 0x<offset>"; a Shadow .DFF's entry name, the part after " > "), with the '\0'. A land table has no mesh to give: it drags nothing. */
				else if(kind_ == Model && (game_ >= Global || rList[i].source.find("land table") == std::string::npos) && ImGui::BeginDragDropSource())
				{
					std::string payload;
					size_t at = rList[i].source.find(" @ 0x");
					size_t arrow = rList[i].source.find(" > ");
					if(game_ >= Global) payload = "external\t" + rList[i].name;
					else if(game_ == 0 && at != std::string::npos) payload = std::format("disc\t{}@0x{:X}", rList[i].source.substr(0, at), std::stoul(rList[i].source.substr(at + 5), nullptr, 16));
					else if(game_ == 1 && arrow != std::string::npos) payload = "disc\t" + rList[i].source.substr(arrow + 3);
					else payload = "disc\t" + rList[i].source;
					ImGui::SetDragDropPayload("MESH", payload.c_str(), payload.size() + 1);
					dragPreview(texture, kind_ == Model, rList[i].name);
					ImGui::EndDragDropSource();
				}
				else if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", rList[i].name.c_str(), rList[i].source.c_str());
				ImGui::TextUnformatted(fitText(rList[i].name, cell).c_str());
				ImGui::EndGroup();
				ImGui::PopID();
			}
		}
	}
	clipper.End();

	/*
	The pictures of the rows next to the drawn ones, as many rows again as are drawn (PrefetchRows) on each side, nearest first, after the drawn tiles took their share of the frame's quota: asking marks them used, so endFrame keeps
	them, and the ones not made yet are made over the next frames. Only when the folder is drawn at all (a clipper that drew nothing leaves lastRow below firstRow).
	*/
	if(game_ < Global && kind_ != Script && pOnly_ == nullptr && lastRow > firstRow)
	{
		int reach = (lastRow - firstRow) * PrefetchRows;
		for(int distance = 0; distance < reach; distance++)
		{
			for(int row : {firstRow - 1 - distance, lastRow + distance})
			{
				if(row < 0 || row >= rows) continue;
				for(int column = 0; column < columns; column++)
				{
					int i = row * columns + column;
					if(i >= static_cast<int>(rList.size())) break;
					this->thumbnails.get(game_, rList[i].source);
				}
			}
		}
	}
}

void AssetPanel::setModelRenderer(PrefabThumbnails* pRenderer_)
{
	this->thumbnails.setModelRenderer(pRenderer_);
}

void AssetPanel::beginFrame()
{
	this->thumbnails.beginFrame();
}

void AssetPanel::endFrame()
{
	this->thumbnails.endFrame();
}

void AssetPanel::destroy()
{
	if(this->folderTexture != 0) glDeleteTextures(1, &this->folderTexture);
	this->folderTexture = 0;
	this->thumbnails.destroy();
}

void AssetPanel::unload()
{
	if(!this->loaded) return;
	this->loaded = false;
	this->searchStale = true;
	this->assets = {};
	this->scriptDirs = {};
	this->counts = {};
	this->levelCounts = {};
	this->levelSeen = ~0ull;
	this->selectedGame = -1;
}

/* Reads Assets/Catalog/folder_icon.png (made from folder_icon.svg with rsvg-convert) into a texture once; without it folder tiles are flat buttons. */
void AssetPanel::loadFolderIcon()
{
	if(this->folderTexture != 0) return;
	int width = 0;
	int height = 0;
	int channels = 0;
	unsigned char* pPixels = stbi_load(FolderIconPath, &width, &height, &channels, 4);
	if(pPixels == nullptr)
	{
		std::fprintf(stderr, "AssetPanel: %s not found.\n", FolderIconPath);
		return;
	}
	glGenTextures(1, &this->folderTexture);
	glBindTexture(GL_TEXTURE_2D, this->folderTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pPixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(pPixels);
}

/* The behaviours (sf.behavior) and the bound area onTouch functions (sf.onTouch) whose sourceFile is the open script file, as tiles in the size of the asset tiles. Dragging a behaviour carries its name ("BEHAVIOR" payload, with the '\0'); the inspector looks it up in entityBehaviorTypes. An onTouch function is a "TCH" tile with the "TOUCH" payload (its name), looked up in boundAreaFunctions by the bound area rules. Read live every frame, so a script loaded after the tab was opened shows up. */
void AssetPanel::drawBehaviours()
{
	const auto& rTypes = Globals::Game::entityBehaviorTypes;
	const auto& rTouch = Globals::Game::boundAreaFunctions;
	std::vector<std::string> names;
	std::vector<std::string> touchNames;
	for(uint32_t i = 0; i < rTypes.objects.size(); i++)
	{
		if(rTypes.alive[i] && rTypes.objects[i].sourceFile == this->scriptFile) names.push_back(rTypes.names[i]);
	}
	for(uint32_t i = 0; i < rTouch.objects.size(); i++)
	{
		if(rTouch.alive[i] && rTouch.objects[i].sourceFile == this->scriptFile) touchNames.push_back(rTouch.names[i]);
	}
	if(names.empty() && touchNames.empty())
	{
		ImGui::TextUnformatted("No behaviour (sf.behavior) or onTouch function (sf.onTouch) registered by this file: it may be loaded by another script, or not be loaded at all.");
		return;
	}
	std::sort(names.begin(), names.end());
	std::sort(touchNames.begin(), touchNames.end());
	size_t behaviourCount = names.size();
	names.insert(names.end(), touchNames.begin(), touchNames.end());
	const ImGuiStyle& rStyle = ImGui::GetStyle();
	float cell = TileSize + 2.0f * rStyle.FramePadding.x;
	int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + rStyle.ItemSpacing.x) / (cell + rStyle.ItemSpacing.x)));
	for(int i = 0; i < static_cast<int>(names.size()); i++)
	{
		if(i % columns != 0) ImGui::SameLine();
		ImGui::PushID(i);
		ImGui::BeginGroup();
		bool isTouch = static_cast<size_t>(i) >= behaviourCount;
		ImGui::PushStyleColor(ImGuiCol_Button, isTouch ? kindColor(Script) : ImVec4(0.5f, 0.3f, 0.65f, 1.0f));
		ImGui::Button(isTouch ? "TCH" : "BHV", ImVec2(cell, TileSize + 2.0f * rStyle.FramePadding.y));
		ImGui::PopStyleColor();
		if(ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(isTouch ? "TOUCH" : "BEHAVIOR", names[i].c_str(), names[i].size() + 1);
			ImGui::TextUnformatted(names[i].c_str());
			ImGui::EndDragDropSource();
		}
		else if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", names[i].c_str());
		ImGui::TextUnformatted(fitText(names[i], cell).c_str());
		ImGui::EndGroup();
		ImGui::PopID();
	}
}

/* Folder tiles wrapped into rows of the width of the tab, the same size as the asset tiles. */
int AssetPanel::drawFolders(const std::vector<std::string>& labels_, int* pRightClicked_)
{
	if(pRightClicked_ != nullptr) *pRightClicked_ = -1;
	const ImGuiStyle& rStyle = ImGui::GetStyle();
	float cell = TileSize + 2.0f * rStyle.FramePadding.x;
	int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + rStyle.ItemSpacing.x) / (cell + rStyle.ItemSpacing.x)));
	int clicked = -1;
	for(int i = 0; i < static_cast<int>(labels_.size()); i++)
	{
		if(i % columns != 0) ImGui::SameLine();
		ImGui::PushID(i);
		ImGui::BeginGroup();
		bool pressed;
		if(this->folderTexture != 0)
		{
			pressed = ImGui::ImageButton("##folder", static_cast<ImTextureID>(this->folderTexture), ImVec2(TileSize, TileSize), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		}
		else
		{
			pressed = ImGui::Button("DIR", ImVec2(cell, TileSize + 2.0f * rStyle.FramePadding.y));
		}
		if(pressed) clicked = i;
		if(pRightClicked_ != nullptr && ImGui::IsItemClicked(ImGuiMouseButton_Right)) *pRightClicked_ = i;
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", labels_[i].c_str());
		ImGui::TextUnformatted(fitText(labels_[i], cell).c_str());
		ImGui::EndGroup();
		ImGui::PopID();
	}
	return clicked;
}

/* The path bar: one button per level, "Assets" first; a click goes back to that level (levels_ holds the depth to return to: 0 = Global / Level, 1 = the games and External, 2 = kinds, 3 = groups, 4+ = script folders, whose path is in the label order). */
void AssetPanel::drawPath(const std::vector<std::string>& names_, const std::vector<int>& levels_)
{
	for(size_t i = 0; i < names_.size(); i++)
	{
		if(i != 0)
		{
			ImGui::SameLine();
			ImGui::TextUnformatted(">");
			ImGui::SameLine();
		}
		ImGui::PushID(static_cast<int>(i));
		if(ImGui::SmallButton(names_[i].c_str()) && i + 1 < names_.size())
		{
			int level = levels_[i];
			this->scriptFile.clear();
			this->nav.goBack(level);
			if(level <= 2) this->openKind = -1;
			if(level <= 3) this->openGroup = -1;
			if(level <= 3) this->scriptFolder.clear();
			if(level > 3)
			{
				/* A script folder level: keep the first (level - 3) folders of the path. */
				std::vector<std::string> parts = splitFolderPath(this->scriptFolder);
				std::string kept;
				for(int n = 0; n < level - 3 && n < static_cast<int>(parts.size()); n++) kept += (kept.empty() ? "" : "/") + parts[n];
				this->scriptFolder = kept;
			}
		}
		ImGui::PopID();
	}
}

/* Every list is scanned once per search text (or after a list changed); the hits are indices into the lists, so drawTiles draws them like a filtered folder. The headings read "Game > Kind > Group (n)". */
void AssetPanel::drawSearch(const std::string& filter_)
{
	if(this->searchStale || this->searchKey != filter_)
	{
		this->searchStale = false;
		this->searchKey = filter_;
		this->searchHits.clear();
		std::string needle = lowerCase(filter_);
		for(int game = 0; game < GameCount; game++)
		{
			for(int kind = 0; kind < KindCount; kind++)
			{
				for(int group = 0; group < 5; group++)
				{
					const std::vector<Asset>& rList = this->assets[game][kind][group];
					SearchHit hit{game, kind, group, {}};
					for(int i = 0; i < static_cast<int>(rList.size()); i++)
					{
						if(lowerCase(rList[i].name).find(needle) != std::string::npos) hit.indices.push_back(i);
					}
					if(!hit.indices.empty()) this->searchHits.push_back(std::move(hit));
				}
			}
		}
	}

	const SourceEngineEnum games[2] = {SA_NINJA, STH2005_ONE};
	ImGui::BeginChild("AssetSearch", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	if(this->searchHits.empty()) ImGui::TextUnformatted("No asset matches.");
	for(int h = 0; h < static_cast<int>(this->searchHits.size()); h++)
	{
		const SearchHit& rHit = this->searchHits[h];
		std::string label = rHit.game >= Global ? externalLabel(rHit.game) : prefabGameFolder(games[rHit.game]);
		label += std::string(" > ") + KindFolders[rHit.kind];
		if(rHit.game < Global && rHit.kind != Script) label += std::string(" > ") + prefabGroupFolder(static_cast<PrefabGroup>(rHit.group));
		label += std::format(" ({})", rHit.indices.size());
		ImGui::PushID(h);
		ImGui::SeparatorText(label.c_str());
		this->searching = true;
		this->drawTiles(rHit.game, rHit.kind, rHit.group, &rHit.indices);
		this->searching = false;
		ImGui::PopID();
	}
	ImGui::EndChild();
}

void AssetPanel::draw(const std::string& filter_)
{
	if(!this->loaded) this->load();
	else if(this->externalSeen != Loading::externalRevision()) this->loadExternal();
	this->loadFolderIcon();
	if(this->nav.inLevel()) this->refreshLevel();
	this->drawScriptDialogs();
	if(this->selectedGame >= 0)
	{
		const Asset& rSelected = this->assets[this->selectedGame][this->selectedKind][this->selectedGroup][this->selectedIndex];
		ImGui::Text("Selected: %s (%s)", rSelected.name.c_str(), rSelected.source.c_str());
	}
	else
	{
		ImGui::TextUnformatted("Click an asset.");
	}
	if(!filter_.empty())
	{
		this->drawSearch(filter_);
		return;
	}

	/* The path bar's names and the level each returns to. */
	std::vector<std::string> names = {"Assets"};
	this->nav.addPathNames(names);
	std::vector<int> levels;
	for(size_t i = 0; i < names.size(); i++) levels.push_back(static_cast<int>(i));
	if(this->openKind >= 0)
	{
		names.push_back(KindFolders[this->openKind]);
		levels.push_back(3);
	}
	if(this->openGroup >= 0 && this->nav.openGame < Global)
	{
		names.push_back(prefabGroupFolder(static_cast<PrefabGroup>(this->openGroup)));
		levels.push_back(4);
	}
	if(this->openKind == Script)
	{
		int depth = 0;
		for(const std::string& rName : splitFolderPath(this->scriptFolder))
		{
			names.push_back(rName);
			levels.push_back(4 + depth);
			depth++;
		}
		if(!this->scriptFile.empty())
		{
			names.push_back(std::filesystem::path(this->scriptFile).filename().generic_string());
			levels.push_back(4 + depth);
		}
	}
	this->drawPath(names, levels);

	ImGui::BeginChild("AssetTiles", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	if(this->nav.openScope < 0)
	{
		this->nav.pickScope(this->drawFolders(ScopeNav::scopeLabels()));
	}
	else if(this->nav.openGame < 0)
	{
		this->nav.pickGame(this->drawFolders(ScopeNav::gameLabels()));
	}
	else if(this->openKind < 0)
	{
		std::vector<std::string> labels;
		bool inLevel = this->nav.inLevel();
		for(int kind = 0; kind < (this->nav.openGame == Level ? Script : KindCount); kind++) labels.push_back(std::format("{} ({})", KindFolders[kind], inLevel ? this->levelCounts[this->nav.openGame][kind] : this->counts[this->nav.openGame][kind]));
		int clicked = this->drawFolders(labels);
		if(clicked >= 0)
		{
			this->openKind = clicked;
			if(this->nav.openGame >= Global) this->openGroup = 0;
		}
	}
	else if(this->openKind == Script && !this->scriptFile.empty())
	{
		if(ImGui::SmallButton("Open in editor")) this->openScript(this->scriptFile);
		ImGui::SameLine();
		if(ImGui::SmallButton("Delete"))
		{
			this->dialogRequest = Delete;
			this->deleteSource = this->scriptFile;
		}
		this->drawBehaviours();
	}
	else if(this->openKind == Script)
	{
		const std::vector<Asset>& rList = this->assets[this->nav.openGame][Script][0];
		/* The folders directly inside the open folder (once each, in name order) and the files in it. */
		std::vector<std::string> folders;
		std::vector<int> files;
		std::string prefix = this->scriptFolder.empty() ? "" : this->scriptFolder + "/";
		bool inLevel = this->nav.inLevel();
		for(int i = 0; i < static_cast<int>(rList.size()); i++)
		{
			if(inLevel && !rList[i].inLevel) continue;
			const std::string& rFolder = rList[i].folder;
			if(rFolder == this->scriptFolder)
			{
				files.push_back(i);
				continue;
			}
			if(!rFolder.starts_with(prefix)) continue;
			std::string rest = rFolder.substr(prefix.size());
			std::string child = rest.substr(0, rest.find('/'));
			if(std::find(folders.begin(), folders.end(), child) == folders.end()) folders.push_back(child);
		}
		/* The folders with no file in them are not in any asset's folder: they come from the directories found. A level view lists only what the level uses, so it has none. */
		if(!inLevel)
		{
			for(const std::string& rDir : this->scriptDirs[this->nav.openGame])
			{
				if(!rDir.starts_with(prefix) || rDir.size() == prefix.size()) continue;
				std::string rest = rDir.substr(prefix.size());
				std::string child = rest.substr(0, rest.find('/'));
				if(std::find(folders.begin(), folders.end(), child) == folders.end()) folders.push_back(child);
			}
			this->drawScriptToolbar();
		}
		std::sort(folders.begin(), folders.end());
		/* Ctrl+C copies the selected script tile, Ctrl+V pastes into the open folder; not while a text field has the keyboard. */
		ImGuiIO& rIo = ImGui::GetIO();
		if(!inLevel && rIo.KeyCtrl && !rIo.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			if(ImGui::IsKeyPressed(ImGuiKey_C, false) && this->selectedGame >= 0 && this->selectedKind == Script && this->selectedGame <= Global)
			{
				this->copiedSource = this->assets[this->selectedGame][Script][0][this->selectedIndex].source;
			}
			if(ImGui::IsKeyPressed(ImGuiKey_V, false) && !this->copiedSource.empty()) this->pasteScript();
		}
		/* A right click on a folder of Global's External asks to delete it (the games' own folders are not deleted from here). */
		int rightClicked = -1;
		int clicked = this->drawFolders(folders, &rightClicked);
		if(rightClicked >= 0 && this->nav.openGame == Global)
		{
			this->dialogRequest = DeleteFolder;
			this->dialogGame = Global;
			this->deleteSource = prefix + folders[rightClicked];
		}
		if(clicked >= 0) this->scriptFolder = prefix + folders[clicked];
		else this->drawTiles(this->nav.openGame, Script, 0, &files);
	}
	else if(this->openGroup < 0)
	{
		std::vector<std::string> labels;
		std::vector<int> groups;
		for(int group = 0; group < 5; group++)
		{
			size_t size = this->nav.inLevel() ? this->levelIndices(this->nav.openGame, this->openKind, group).size() : this->assets[this->nav.openGame][this->openKind][group].size();
			if(size == 0) continue;
			labels.push_back(std::format("{} ({})", prefabGroupFolder(static_cast<PrefabGroup>(group)), size));
			groups.push_back(group);
		}
		int clicked = this->drawFolders(labels);
		if(clicked >= 0) this->openGroup = groups[clicked];
	}
	else if(this->nav.inLevel())
	{
		std::vector<int> indices = this->levelIndices(this->nav.openGame, this->openKind, this->openGroup);
		this->drawTiles(this->nav.openGame, this->openKind, this->openGroup, &indices);
	}
	else
	{
		this->drawTiles(this->nav.openGame, this->openKind, this->openGroup);
	}
	ImGui::EndChild();
}
}
