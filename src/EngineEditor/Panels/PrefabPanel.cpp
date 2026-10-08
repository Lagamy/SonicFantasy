#include "PrefabPanel.h"
#include "Globals.h"
#include "API.h"
#include "Prefab.h"
#include "Editor.h"
#include "Level.h"
#include "EditorCamera.h"
#include "EntityListPanel.h"
#include "InspectorPanel.h"
#include "GPU_API.h"
#include "ExternalPrefabs.h"
#include "ScriptLog.h"
#include "Scripting.h"
#include <unordered_set>
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <cstring>

namespace Graphics
{
namespace
{
constexpr float TileSize = 72.0f;

/* How far above and below the tile area (in heights of it) a tile keeps or gets its picture: 2 = about five times what is on screen stays loaded, so a small scroll does not unload and reload from the disc. */
constexpr float PrefetchMargin = 2.0f;

/* The text in lower case (ASCII), for comparing names without regard to case. */
std::string lowerCase(std::string text_)
{
	for(char& rChar : text_) rChar = static_cast<char>(std::tolower(static_cast<unsigned char>(rChar)));
	return text_;
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

void PrefabPanel::init()
{
	this->thumbnails.init();
	this->assets.setModelRenderer(&this->thumbnails);
}

void PrefabPanel::destroy()
{
	this->assets.destroy();
	this->thumbnails.destroy();
}

void PrefabPanel::rebuildTiles()
{
	for(auto& rGame : this->tiles)
	{
		for(auto& rGroup : rGame) rGroup.clear();
	}
	for(auto& rScope : this->externalTiles) rScope.clear();
	const auto& rPrefabs = Globals::Game::prefabs;
	for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
	{
		if(!rPrefabs.alive[i]) continue;
		const Prefab& rPrefab = rPrefabs.objects[i];
		PoolId id(i, rPrefabs.generation[i]);
		if(rPrefab.kind == PrefabKind::External) this->externalTiles[rPrefab.inLevel ? 1 : 0].push_back(id);
		else this->tiles[rPrefab.game == SA_NINJA ? 0 : 1][static_cast<int>(rPrefab.group)].push_back(id);
	}
	auto byName = [&](PoolId a_, PoolId b_) { return rPrefabs.objects[a_.id].name < rPrefabs.objects[b_.id].name; };
	for(auto& rGame : this->tiles)
	{
		for(auto& rGroup : rGame) std::sort(rGroup.begin(), rGroup.end(), byName);
	}
	for(auto& rScope : this->externalTiles) std::sort(rScope.begin(), rScope.end(), byName);
	this->listedRevision = prefabRevision();
	this->shownStale = true;
}

/* shown = tiles with only the prefabs whose name holds the search text. */
void PrefabPanel::filterTiles()
{
	this->shownStale = false;
	this->shownFor = this->search;
	std::string needle = lowerCase(this->shownFor);
	const auto& rPrefabs = Globals::Game::prefabs;
	auto matches = [&](PoolId id_) { return needle.empty() || lowerCase(rPrefabs.objects[id_.id].name).find(needle) != std::string::npos; };
	for(int game = 0; game < 2; game++)
	{
		for(int group = 0; group < 5; group++)
		{
			std::vector<PoolId>& rShown = this->shown[game][group];
			rShown.clear();
			for(PoolId id : this->tiles[game][group])
			{
				if(matches(id)) rShown.push_back(id);
			}
		}
		this->externalShown[game].clear();
		for(PoolId id : this->externalTiles[game])
		{
			if(matches(id)) this->externalShown[game].push_back(id);
		}
	}
}

/* One tile: the picture as a button (a grey one while its picture is not made), the name under it, a tooltip with what the prefab is made of, and the drag source. */
void PrefabPanel::drawTile(PoolId prefabId_, float cellWidth_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	ImGui::PushID(static_cast<int>(prefabId_.id));
	ImGui::BeginGroup();

	/* Inside the tile area's window: the picture is wanted on screen and within PrefetchMargin heights of it (the tiles off screen are submitted too, ImGui clips them). */
	bool visible = ImGui::IsRectVisible(ImVec2(TileSize, TileSize));
	float top = ImGui::GetCursorScreenPos().y;
	float areaTop = ImGui::GetWindowPos().y;
	float areaHeight = ImGui::GetWindowSize().y;
	bool near = visible || (top + TileSize >= areaTop - areaHeight * PrefetchMargin && top <= areaTop + areaHeight * (1.0f + PrefetchMargin));
	GLuint texture = near ? this->thumbnails.get(prefabId_, !visible) : 0;
	/* Under Level, a prefab a script spawned an entity from (the players) is orange, as in the Assets tab, unless selected; the Button colors show through the picture's background, so they are pushed too. */
	bool orange = false;
	if(this->nav.openScope == 1 && prefabId_ != this->selected)
	{
		for(PoolId id : Scripting::spawnedEntities())
		{
			if(id.id < Globals::Game::entities.objects.size() && Globals::Game::entities.alive[id.id] && Globals::Game::entities[id].prefabId == prefabId_) orange = true;
		}
	}
	ImVec4 background = prefabId_ == this->selected ? ImVec4(0.2f, 0.4f, 0.8f, 0.7f) : orange ? ImVec4(1.0f, 0.5f, 0.0f, 0.7f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	if(orange)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.7f, 0.25f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.85f, 0.5f, 1.0f));
	}
	bool clicked;
	if(texture != 0)
	{
		/* The texture is upside down (GL's origin is the bottom left), hence the flipped UVs. */
		clicked = ImGui::ImageButton("##tile", static_cast<ImTextureID>(texture), ImVec2(TileSize, TileSize), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), background);
	}
	else
	{
		clicked = ImGui::Button("...", ImVec2(TileSize + 2.0f * ImGui::GetStyle().FramePadding.x, TileSize + 2.0f * ImGui::GetStyle().FramePadding.y));
	}
	if(orange) ImGui::PopStyleColor(3);
	if(clicked) this->selected = prefabId_;

	if(ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("PREFAB", &prefabId_, sizeof(PoolId));
		if(texture != 0) ImGui::Image(static_cast<ImTextureID>(texture), ImVec2(TileSize, TileSize), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
		ImGui::TextUnformatted(rPrefab.name.c_str());
		ImGui::EndDragDropSource();
	}
	else if(ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s\nmodel: %s, collision: %s, behaviour: %s", Globals::Game::prefabs.names[prefabId_.id].c_str(), rPrefab.hasModel ? "yes" : "no", rPrefab.hasCollision ? "yes" : "no", rPrefab.hasBehaviour ? "yes" : "no");
	}

	ImGui::TextUnformatted(fitText(rPrefab.name, cellWidth_).c_str());
	ImGui::EndGroup();
	ImGui::PopID();
}

/* Makes the entity at SpawnDistance in front of the camera and selects it in the entity list; a prefab that can't be made leaves a message (the reason is on stderr). */
void PrefabPanel::spawn(PoolId prefabId_, PoolId& rSelectedEntity_, const EditorCamera& rCamera_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	SpawnParams params;
	params.position = rCamera_.position + rCamera_.front * SpawnDistance;
	params.what = "(editor)";
	PoolId id = spawnPrefab(prefabId_, getShaderId(rPrefab.game == SA_NINJA ? "SA" : "STH2005"), params);
	if(id == UninitializedPoolId)
	{
		this->message = std::format("Could not spawn {}, see the log.", rPrefab.name);
		return;
	}
	this->message.clear();
	rSelectedEntity_ = id;

	/* The level owns it from now on, so it is saved with the level's edits and removed with the level. */
	PoolId levelId = editor.currentLevel();
	if(levelId != UninitializedPoolId) getLevel(levelId).entityIds.push_back(id);
	editor.recordCreated(id);
}

/* The tiles of a list as a wrapped grid. */
void PrefabPanel::drawGrid(const std::vector<PoolId>& rList_)
{
	float cell = TileSize + 2.0f * ImGui::GetStyle().FramePadding.x;
	int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) / (cell + ImGui::GetStyle().ItemSpacing.x)));
	for(int i = 0; i < static_cast<int>(rList_.size()); i++)
	{
		if(i % columns != 0) ImGui::SameLine();
		this->drawTile(rList_[i], cell);
	}
}

/* The path bar: "Prefabs" > Global / Level > the open game > its group, or External > the folders of openFolder; a click goes back to that level. */
void PrefabPanel::drawPath()
{
	std::vector<std::string> names = {"Prefabs"};
	this->nav.addPathNames(names);
	if(this->nav.openGame >= 0 && this->nav.openGame < Global && this->openGroup >= 0) names.push_back(prefabGroupFolder(static_cast<PrefabGroup>(this->openGroup)));
	size_t folderStart = names.size();
	if(this->nav.openGame >= Global)
	{
		for(const std::string& rName : splitFolderPath(this->openFolder)) names.push_back(rName);
	}
	for(size_t i = 0; i < names.size(); i++)
	{
		if(i != 0)
		{
			ImGui::SameLine();
			ImGui::TextUnformatted(">");
			ImGui::SameLine();
		}
		ImGui::PushID(static_cast<int>(i));
		if(ImGui::SmallButton(names[i].c_str()) && i + 1 < names.size())
		{
			if(i <= 1)
			{
				this->nav.goBack(static_cast<int>(i));
				this->openGroup = -1;
				this->openFolder.clear();
			}
			else if(i < folderStart)
			{
				this->openGroup = -1;
				this->openFolder.clear();
			}
			else
			{
				/* Keep the first (i - folderStart + 1) folders of the path. */
				std::string kept;
				for(size_t n = folderStart; n <= i; n++) kept += (kept.empty() ? "" : "/") + names[n];
				this->openFolder = kept;
			}
		}
		ImGui::PopID();
	}
}

/* "New folder" and "New prefab" for Global / Level: the button opens a popup asking for the name, Enter or Create makes it in the open folder. A failure is shown in the panel's message. */
void PrefabPanel::drawNewMenu()
{
	Loading::ExternalScope scope = this->nav.openGame == Global ? Loading::ExternalScope::Project : Loading::ExternalScope::Level;
	if(ImGui::Button("New folder"))
	{
		this->creating = 1;
		this->newName[0] = '\0';
		ImGui::OpenPopup("NewExternalName");
	}
	ImGui::SameLine();
	if(ImGui::Button("New prefab"))
	{
		this->creating = 2;
		this->newName[0] = '\0';
		ImGui::OpenPopup("NewExternalName");
	}
	if(ImGui::BeginPopup("NewExternalName"))
	{
		ImGui::TextUnformatted(this->creating == 1 ? "Folder name" : "Prefab name");
		if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		bool enter = ImGui::InputText("##newName", this->newName, sizeof(this->newName), ImGuiInputTextFlags_EnterReturnsTrue);
		if(ImGui::Button("Create") || enter)
		{
			try
			{
				if(this->creating == 1) Loading::createExternalPrefabFolder(scope, this->openFolder, this->newName);
				else this->selected = Loading::createExternalPrefab(scope, this->openFolder, this->newName);
				this->message.clear();
			}
			catch(const std::exception& rError)
			{
				this->message = std::string("Not created: ") + rError.what();
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

/* The Prefabs tab's content for the open place. */
void PrefabPanel::drawBrowser()
{
	if(this->nav.openScope < 0)
	{
		this->nav.pickScope(this->assets.drawFolders(ScopeNav::scopeLabels()));
		return;
	}
	if(this->nav.openGame < 0)
	{
		this->nav.pickGame(this->assets.drawFolders(ScopeNav::gameLabels()));
		return;
	}
	if(this->nav.openGame < Global)
	{
		/* Under Level only the prefabs of the open level's entities and those in its prefab fields (read every frame: entities are added and removed). */
		std::array<std::vector<PoolId>, 5> levelTiles;
		if(this->nav.openScope == 1)
		{
			std::unordered_set<uint32_t> used;
			const Level* pLevel = AssetPanel::openLevel();
			if(pLevel != nullptr)
			{
				for(PoolId id : pLevel->entityIds)
				{
					if(id.id < Globals::Game::entities.objects.size() && Globals::Game::entities.alive[id.id]) used.insert(Globals::Game::entities[id].prefabId.id);
				}
				/* Entities scripts spawned (the players) are not in entityIds; their prefabs belong to the level too. */
				for(PoolId id : Scripting::spawnedEntities())
				{
					if(id.id < Globals::Game::entities.objects.size() && Globals::Game::entities.alive[id.id]) used.insert(Globals::Game::entities[id].prefabId.id);
				}
				/* A prefab dropped on a prefab field of an entity's behaviour belongs to the level too, before anything is spawned from it. */
				for(PoolId id : pLevel->referencedPrefabs()) used.insert(id.id);
			}
			for(int group = 0; group < 5; group++)
			{
				for(PoolId id : this->tiles[this->nav.openGame][group])
				{
					if(used.contains(id.id)) levelTiles[group].push_back(id);
				}
			}
		}
		const auto& rShown = this->nav.openScope == 1 ? levelTiles : this->tiles[this->nav.openGame];
		if(this->openGroup < 0)
		{
			std::vector<std::string> labels;
			std::vector<int> groups;
			for(int group = 0; group < 5; group++)
			{
				size_t size = rShown[group].size();
				if(size == 0) continue;
				labels.push_back(std::format("{} ({})", prefabGroupFolder(static_cast<PrefabGroup>(group)), size));
				groups.push_back(group);
			}
			int clicked = this->assets.drawFolders(labels);
			if(clicked >= 0) this->openGroup = groups[clicked];
		}
		else
		{
			this->drawGrid(rShown[this->openGroup]);
		}
		return;
	}

	this->drawNewMenu();
	int scopeIndex = this->nav.openGame == Global ? 0 : 1;
	Loading::ExternalScope scope = scopeIndex == 0 ? Loading::ExternalScope::Project : Loading::ExternalScope::Level;
	const auto& rPrefabs = Globals::Game::prefabs;

	/* The folders directly inside the open one (the first segment past its path), in name order, then the prefabs in it. */
	std::vector<std::string> labels;
	std::vector<std::string> paths;
	std::string prefix = this->openFolder.empty() ? "" : this->openFolder + "/";
	for(const std::string& rFolder : Loading::externalPrefabFolders(scope))
	{
		if(!rFolder.starts_with(prefix) || rFolder.find('/', prefix.size()) != std::string::npos || rFolder.size() == prefix.size()) continue;
		labels.push_back(rFolder.substr(prefix.size()));
		paths.push_back(rFolder);
	}
	int clicked = this->assets.drawFolders(labels);
	if(clicked >= 0)
	{
		this->openFolder = paths[clicked];
		return;
	}
	std::vector<PoolId> inFolder;
	for(PoolId id : this->externalTiles[scopeIndex])
	{
		if(rPrefabs.objects[id.id].folder == this->openFolder) inFolder.push_back(id);
	}
	this->drawGrid(inFolder);
	if(labels.empty() && inFolder.empty()) ImGui::TextDisabled("Empty. New folder / New prefab; give a prefab a model by dragging one from the Assets tab onto the inspector's Mesh tab of a spawned entity.");
}

/* The search: the matching prefabs in sections named by where they live ("Game > Group", "Global > folder"). */
void PrefabPanel::drawSearchResults()
{
	const SourceEngineEnum games[2] = {SA_NINJA, STH2005_ONE};
	const auto& rPrefabs = Globals::Game::prefabs;
	bool any = false;
	for(int game = 0; game < 2; game++)
	{
		ImGui::PushID(game);
		for(int group = 0; group < 5; group++)
		{
			const std::vector<PoolId>& rList = this->shown[game][group];
			if(rList.empty()) continue;
			any = true;
			ImGui::PushID(group);
			ImGui::SeparatorText(std::format("{} > {} ({})", prefabGameFolder(games[game]), prefabGroupFolder(static_cast<PrefabGroup>(group)), rList.size()).c_str());
			this->drawGrid(rList);
			ImGui::PopID();
		}
		ImGui::PopID();
	}
	for(int scope = 0; scope < 2; scope++)
	{
		std::vector<std::string> folders;
		for(PoolId id : this->externalShown[scope])
		{
			if(std::find(folders.begin(), folders.end(), rPrefabs.objects[id.id].folder) == folders.end()) folders.push_back(rPrefabs.objects[id.id].folder);
		}
		std::sort(folders.begin(), folders.end());
		for(const std::string& rFolder : folders)
		{
			std::vector<PoolId> list;
			for(PoolId id : this->externalShown[scope])
			{
				if(rPrefabs.objects[id.id].folder == rFolder) list.push_back(id);
			}
			any = true;
			ImGui::PushID(scope);
			ImGui::PushID(rFolder.c_str());
			ImGui::SeparatorText(std::format("{}{}{} ({})", scope == 0 ? "Global" : "Level", rFolder.empty() ? "" : " > ", rFolder, list.size()).c_str());
			this->drawGrid(list);
			ImGui::PopID();
			ImGui::PopID();
		}
	}
	if(!any) ImGui::TextUnformatted("No prefab matches.");
}

/* Clear empties the shared log. Errors are red and may hold several lines (the traceback). A new message scrolls the list down only if it was at the bottom before, so reading old lines is not interrupted. */
void PrefabPanel::drawOutput()
{
	if(ImGui::Button("Clear")) Scripting::logLines.clear();
	ImGui::SameLine();
	ImGui::TextDisabled("%zu messages from scripts (print and errors)", Scripting::logLines.size());
	ImGui::BeginChild("OutputLines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
	bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
	for(const Scripting::LogLine& rLine : Scripting::logLines)
	{
		if(rLine.error) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		ImGui::TextUnformatted(rLine.text.c_str());
		if(rLine.error) ImGui::PopStyleColor();
	}
	if(this->outputSeen != Scripting::logTotal && atBottom) ImGui::SetScrollHereY(1.0f);
	this->outputSeen = Scripting::logTotal;
	ImGui::EndChild();
}

void PrefabPanel::draw(PoolId& rSelectedEntity_, const EditorCamera& rCamera_)
{
	this->thumbnails.beginFrame();
	this->assets.beginFrame();
	if(this->listedRevision != prefabRevision())
	{
		this->rebuildTiles();
		this->thumbnails.forgetExternal();
	}
	auto& rPrefabs = Globals::Game::prefabs;
	if(this->selected != UninitializedPoolId && (this->selected.id >= rPrefabs.objects.size() || !rPrefabs.alive[this->selected.id] || rPrefabs.generation[this->selected.id] != this->selected.generation)) this->selected = UninitializedPoolId;
	/* A folder that is gone (the level it was in was replaced) leaves the browser at the top of its scope. */
	if(this->nav.openGame >= Global && !this->openFolder.empty())
	{
		const std::vector<std::string>& rFolders = Loading::externalPrefabFolders(this->nav.openGame == Global ? Loading::ExternalScope::Project : Loading::ExternalScope::Level);
		if(std::find(rFolders.begin(), rFolders.end(), this->openFolder) == rFolders.end()) this->openFolder.clear();
	}

	ImVec2 display = ImGui::GetIO().DisplaySize;
	float left = editor.entityList.width;
	float width = display.x - left - editor.inspector.width;
	ImGui::SetNextWindowPos(ImVec2(left, display.y - this->height));
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 100.0f), ImVec2(width, display.y * 0.7f));
	ImGui::SetNextWindowSize(ImVec2(width, this->height));
	ImGui::Begin("Assets and prefabs", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
	this->height = ImGui::GetWindowSize().y;

	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##AssetSearch", "search assets and prefabs by name", this->search, sizeof(this->search));
	if(this->shownStale || this->shownFor != this->search) this->filterTiles();
	bool filtering = this->search[0] != '\0';

	if(ImGui::BeginTabBar("BottomTabs"))
	{
		if(ImGui::BeginTabItem("Assets"))
		{
			this->assets.draw(this->search);
			ImGui::EndTabItem();
		}
		else
		{
			this->assets.unload();
		}
		if(ImGui::BeginTabItem("Prefabs"))
		{
			this->assets.loadFolderIcon();
			ImGui::BeginDisabled(this->selected == UninitializedPoolId);
			if(ImGui::Button("Spawn at camera")) this->spawn(this->selected, rSelectedEntity_, rCamera_);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if(this->selected != UninitializedPoolId) ImGui::Text("Selected: %s", Globals::Game::prefabs.names[this->selected.id].c_str());
			else ImGui::TextUnformatted("Click a prefab, then Spawn at camera, or drag it into the scene.");
			if(!this->message.empty())
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", this->message.c_str());
			}
			if(!filtering) this->drawPath();

			ImGui::BeginChild("PrefabTiles", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
			if(filtering) this->drawSearchResults();
			else this->drawBrowser();
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Output"))
		{
			this->drawOutput();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
	this->thumbnails.endFrame();
	this->assets.endFrame();

	/*
	A dragged tile released where no ImGui window is under the mouse is dropped into the scene. The prefab is remembered from the frames of the drag, and the release is read before the payload is looked at again, because ImGui may have dropped
	the payload by the frame the release is seen in. Unverified: not run.
	*/
	if(this->dragged != UninitializedPoolId && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		if(!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) this->spawn(this->dragged, rSelectedEntity_, rCamera_);
		this->dragged = UninitializedPoolId;
	}
	const ImGuiPayload* pPayload = ImGui::GetDragDropPayload();
	if(pPayload != nullptr && pPayload->IsDataType("PREFAB")) std::memcpy(&this->dragged, pPayload->Data, sizeof(PoolId));
	else if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) this->dragged = UninitializedPoolId;
}
}
