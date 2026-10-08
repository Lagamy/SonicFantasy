#include "AssetPanel.h"
#include "Prefab.h"
#include "ExternalAssets.h"
#include "GPU_Globals.h"
#include "Globals.h"
#include "Level.h"
#include "API.h"
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
	const Level* pLevel = openLevel();
	uint64_t seen = pLevel == nullptr ? 0 : reinterpret_cast<uintptr_t>(pLevel) * 31 + pLevel->entityIds.size();
	if(seen == this->levelSeen) return;
	this->levelSeen = seen;
	this->searchStale = true;

	/* Per game the keys of the meshes and textures the level's entities draw, and the script files of their behaviours. */
	std::array<std::unordered_set<std::string>, 2> meshes;
	std::array<std::unordered_set<std::string>, 2> textures;
	std::unordered_set<std::string> scripts;
	if(pLevel != nullptr)
	{
		for(PoolId id : pLevel->entityIds)
		{
			if(id.id >= Globals::Game::entities.objects.size() || !Globals::Game::entities.alive[id.id]) continue;
			const Entity& rEntity = Globals::Game::entities[id];
			if(rEntity.behaviorSetId != UninitializedPoolId)
			{
				for(PoolId behaviorId : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds) scripts.insert(lowerCase(Globals::Game::entityBehaviorTypes[behaviorId].sourceFile));
			}
			int game = rEntity.sourceEngine == SA_NINJA ? 0 : rEntity.sourceEngine == STH2005_ONE ? 1 : -1;
			if(game < 0 || rEntity.instanceId == UninitializedPoolId) continue;
			try
			{
				meshes[game].insert(matchKey(game, Model, rEntity.discMesh()));
				std::string texture = rEntity.discTexture();
				if(!texture.empty()) textures[game].insert(matchKey(game, Texture, texture));
			}
			catch(const std::exception&)
			{
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
					if(kind == Script) rAsset.inLevel = scripts.contains(lowerCase(rAsset.source));
					else rAsset.inLevel = (kind == Model ? meshes : textures)[game].contains(matchKey(game, kind, rAsset.source));
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
		for(auto& rList : this->assets[game]) rList[0].clear();
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
			if(rPools.textures.alive[i]) this->assets[game][Texture][0].push_back({rPools.textures.names[i], source, "", rPools.textures.getInternal(i).textureId});
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

/* Every .lua file below Scripts/<game folder>, named by its file name, with its folder inside that folder in Asset::folder ("replays"). */
void AssetPanel::loadScripts()
{
	namespace fs = std::filesystem;
	for(int game = 0; game < 2; game++)
	{
		fs::path root = fs::path(ScriptsPath) / ScriptFolders[game];
		std::error_code error;
		for(fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
		{
			if(!it->is_regular_file() || it->path().extension() != ".lua") continue;
			fs::path relative = fs::relative(it->path(), root);
			this->assets[game][Script][0].push_back({relative.filename().generic_string(), it->path().generic_string(), relative.parent_path().generic_string()});
		}
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
				if(selected) color = ImVec4(0.2f, 0.4f, 0.8f, 1.0f);
				GLuint texture = 0;
				if(game_ >= Global) texture = kind_ == Texture ? rList[i].glTexture : 0;
				else if(kind_ == Script) texture = this->folderTexture;
				else if(kind_ != Script && ImGui::IsRectVisible(ImVec2(TileSize, TileSize))) texture = this->thumbnails.get(game_, rList[i].source, kind_ == Model);
				bool clicked;
				if(texture != 0)
				{
					ImVec4 background = selected ? ImVec4(0.2f, 0.4f, 0.8f, 0.7f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
					clicked = ImGui::ImageButton("##tile", static_cast<ImTextureID>(texture), ImVec2(TileSize, TileSize), ImVec2(0.0f, kind_ == Model ? 1.0f : 0.0f), ImVec2(1.0f, kind_ == Model ? 0.0f : 1.0f), background);
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Button, color);
					clicked = ImGui::Button(KindLabels[kind_], ImVec2(cell, TileSize + 2.0f * rStyle.FramePadding.y));
					ImGui::PopStyleColor();
				}
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
				/* A script tile can be dropped on the inspector's Behaviour tab: the payload is the file's path, with the '\0'. */
				if(kind_ == Script && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("SCRIPT", rList[i].source.c_str(), rList[i].source.size() + 1);
					ImGui::TextUnformatted(rList[i].name.c_str());
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
					ImGui::TextUnformatted(rList[i].name.c_str());
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
					ImGui::TextUnformatted(rList[i].name.c_str());
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

/* The behaviours whose sourceFile is the open script file, as tiles in the size of the asset tiles. Dragging one carries its name ("BEHAVIOR" payload, with the '\0'); the inspector looks it up in entityBehaviorTypes. Read live every frame, so a script loaded after the tab was opened shows up. */
void AssetPanel::drawBehaviours()
{
	const auto& rTypes = Globals::Game::entityBehaviorTypes;
	std::vector<std::string> names;
	for(uint32_t i = 0; i < rTypes.objects.size(); i++)
	{
		if(rTypes.alive[i] && rTypes.objects[i].sourceFile == this->scriptFile) names.push_back(rTypes.names[i]);
	}
	if(names.empty())
	{
		ImGui::TextUnformatted("No behaviour registered by this file (sf.behavior): it may be loaded by another script, or not be loaded at all.");
		return;
	}
	std::sort(names.begin(), names.end());
	const ImGuiStyle& rStyle = ImGui::GetStyle();
	float cell = TileSize + 2.0f * rStyle.FramePadding.x;
	int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + rStyle.ItemSpacing.x) / (cell + rStyle.ItemSpacing.x)));
	for(int i = 0; i < static_cast<int>(names.size()); i++)
	{
		if(i % columns != 0) ImGui::SameLine();
		ImGui::PushID(i);
		ImGui::BeginGroup();
		ImGui::PushStyleColor(ImGuiCol_Button, kindColor(Script));
		ImGui::Button("BHV", ImVec2(cell, TileSize + 2.0f * rStyle.FramePadding.y));
		ImGui::PopStyleColor();
		if(ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("BEHAVIOR", names[i].c_str(), names[i].size() + 1);
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
int AssetPanel::drawFolders(const std::vector<std::string>& labels_)
{
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
			if(level <= 0) this->openScope = -1;
			if(level <= 1) this->openGame = -1;
			if(level <= 2) this->openKind = -1;
			if(level <= 3) this->openGroup = -1;
			if(level <= 3) this->scriptFolder.clear();
			if(level > 3)
			{
				/* A script folder level: keep the first (level - 3) folders of the path. */
				std::string kept;
				size_t from = 0;
				for(int n = 0; n < level - 3; n++)
				{
					size_t slash = this->scriptFolder.find('/', from);
					if(slash == std::string::npos) slash = this->scriptFolder.size();
					kept = this->scriptFolder.substr(0, slash);
					from = slash + 1;
				}
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
	if(this->openScope == 1 && this->openGame >= 0 && this->openGame < Global) this->refreshLevel();
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
	const SourceEngineEnum games[2] = {SA_NINJA, STH2005_ONE};
	std::vector<std::string> names = {"Assets"};
	std::vector<int> levels = {0};
	if(this->openScope >= 0)
	{
		names.push_back(externalLabel(Global + this->openScope));
		levels.push_back(1);
	}
	if(this->openGame >= 0)
	{
		names.push_back(this->openGame >= Global ? "External" : prefabGameFolder(games[this->openGame]));
		levels.push_back(2);
	}
	if(this->openKind >= 0)
	{
		names.push_back(KindFolders[this->openKind]);
		levels.push_back(3);
	}
	if(this->openGroup >= 0 && this->openGame < Global)
	{
		names.push_back(prefabGroupFolder(static_cast<PrefabGroup>(this->openGroup)));
		levels.push_back(4);
	}
	if(this->openKind == Script)
	{
		size_t from = 0;
		int depth = 0;
		while(from < this->scriptFolder.size())
		{
			size_t slash = this->scriptFolder.find('/', from);
			if(slash == std::string::npos) slash = this->scriptFolder.size();
			names.push_back(this->scriptFolder.substr(from, slash - from));
			levels.push_back(4 + depth);
			depth++;
			from = slash + 1;
		}
		if(!this->scriptFile.empty())
		{
			names.push_back(std::filesystem::path(this->scriptFile).filename().generic_string());
			levels.push_back(4 + depth);
		}
	}
	this->drawPath(names, levels);

	ImGui::BeginChild("AssetTiles", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	if(this->openScope < 0)
	{
		std::vector<std::string> labels = {externalLabel(Global), externalLabel(Level)};
		int clicked = this->drawFolders(labels);
		if(clicked >= 0) this->openScope = clicked;
	}
	else if(this->openGame < 0)
	{
		std::vector<std::string> labels = {prefabGameFolder(games[0]), prefabGameFolder(games[1]), "External"};
		int clicked = this->drawFolders(labels);
		if(clicked >= 0) this->openGame = clicked < 2 ? clicked : Global + this->openScope;
	}
	else if(this->openKind < 0)
	{
		std::vector<std::string> labels;
		bool inLevel = this->openScope == 1 && this->openGame < Global;
		for(int kind = 0; kind < (this->openGame >= Global ? Script : KindCount); kind++) labels.push_back(std::format("{} ({})", KindFolders[kind], inLevel ? this->levelCounts[this->openGame][kind] : this->counts[this->openGame][kind]));
		int clicked = this->drawFolders(labels);
		if(clicked >= 0)
		{
			this->openKind = clicked;
			if(this->openGame >= Global) this->openGroup = 0;
		}
	}
	else if(this->openKind == Script && !this->scriptFile.empty())
	{
		this->drawBehaviours();
	}
	else if(this->openKind == Script)
	{
		const std::vector<Asset>& rList = this->assets[this->openGame][Script][0];
		/* The folders directly inside the open folder (once each, in name order) and the files in it. */
		std::vector<std::string> folders;
		std::vector<int> files;
		std::string prefix = this->scriptFolder.empty() ? "" : this->scriptFolder + "/";
		bool inLevel = this->openScope == 1 && this->openGame < Global;
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
		std::sort(folders.begin(), folders.end());
		int clicked = this->drawFolders(folders);
		if(clicked >= 0) this->scriptFolder = prefix + folders[clicked];
		else this->drawTiles(this->openGame, Script, 0, &files);
	}
	else if(this->openGroup < 0)
	{
		std::vector<std::string> labels;
		std::vector<int> groups;
		for(int group = 0; group < 5; group++)
		{
			size_t size = this->openScope == 1 && this->openGame < Global ? this->levelIndices(this->openGame, this->openKind, group).size() : this->assets[this->openGame][this->openKind][group].size();
			if(size == 0) continue;
			labels.push_back(std::format("{} ({})", prefabGroupFolder(static_cast<PrefabGroup>(group)), size));
			groups.push_back(group);
		}
		int clicked = this->drawFolders(labels);
		if(clicked >= 0) this->openGroup = groups[clicked];
	}
	else if(this->openScope == 1 && this->openGame < Global)
	{
		std::vector<int> indices = this->levelIndices(this->openGame, this->openKind, this->openGroup);
		this->drawTiles(this->openGame, this->openKind, this->openGroup, &indices);
	}
	else
	{
		this->drawTiles(this->openGame, this->openKind, this->openGroup);
	}
	ImGui::EndChild();
}
}
