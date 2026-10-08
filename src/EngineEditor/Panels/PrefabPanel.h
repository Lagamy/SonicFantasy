#pragma once
#include "PoolId.h"
#include "PrefabThumbnails.h"
#include "AssetPanel.h"
#include <array>
#include <string>
#include <vector>

struct EditorCamera;

namespace Graphics
{
/*
The bottom panel of the editor: a fixed window along the bottom edge between the entity list (left) and the inspector (right), with a tab bar. "Assets" is the AssetPanel (assets). "Prefabs" is a file browser like it: a path bar above and the current folder shown as
tiles (the folder icon of the Assets tab, the picture of a prefab from PrefabThumbnails: the model, or a cube that is dark blue for collision only, dark red for behaviour only, with the name under it). The top level has two hardcoded folders, "Global" and "Level", each with the same three folders: the two games (prefabGameFolder: inside one a
folder per group, prefabGroupFolder, with the number of prefabs, then the prefabs of Globals::Game::prefabs; under Global all of them, under Level only the ones the open level's entities were made from (Entity::prefabId)) and "External" (under Global the editor-wide External prefabs, under Level the open level's), where "New folder" and "New prefab" make folders and empty prefabs
(Loading/ExternalPrefabs.h, saved at once). An External prefab gets its model and texture by dragging them from the Assets tab onto the Mesh and Texture tabs of the inspector, in the box "Every entity of <prefab>" of an entity spawned from it. The lists are made again when
prefabRevision (Prefab.h) changes.
Clicking a tile selects the prefab (selected). "Spawn at camera" and releasing a dragged tile over the scene (anywhere no ImGui window is under the mouse) spawn it with spawnPrefab (API.h) SpawnDistance in front of the editor camera, with no SET record
(SpawnParams::pPlacement null: a script behaviour that needs one has to cope, a failure is reported on stderr and the panel shows a message), and select the new entity in the EntityListPanel. Drawn by DebugUI::draw, inside its ImGui frame; Editor::prefabPanel.
*/
struct PrefabPanel
{
	PoolId selected = UninitializedPoolId;

	/* The content of the "Assets" tab. */
	AssetPanel assets;

	/* The panel's height in pixels: the user drags its top edge, draw() reads the result back, and Editor saves it (editor_settings.json). */
	float height = 230.0f;

	/* How far in front of the editor camera a spawned entity is put (a guess: SADX objects are tens of units big). */
	static constexpr float SpawnDistance = 30.0f;

	/* Makes the thumbnail target; once the window and renderer exist. */
	void init();

	/* rSelectedEntity_ is the EntityListPanel's selection, which a spawn sets. */
	void draw(PoolId& rSelectedEntity_, const EditorCamera& rCamera_);

	void destroy();

private:
	/* The game indices of the top-level folders of the Prefabs tab (the first two are the games, as in Prefab::game order SADX, Shadow). */
	static constexpr int Global = ScopeNav::Global;

	PrefabThumbnails thumbnails;

	/* The prefabs of each game (0 = SADX, 1 = Shadow) and group, and of each External scope (0 = Global, 1 = Level), by name, and the prefabRevision they were made at. */
	std::array<std::array<std::vector<PoolId>, 5>, 2> tiles;
	std::array<std::vector<PoolId>, 2> externalTiles;
	uint32_t listedRevision = 0;

	/* The prefab of a tile being dragged, remembered across frames so that the release can be told from the frame it happens in. */
	PoolId dragged = UninitializedPoolId;

	std::string message;

	/* The search box above the tabs, one text for both tabs (the Assets tab searches every game, kind and group, the Prefabs tab every folder; a non-empty text shows only the names holding it, case does not matter). */
	char search[128] = "";

	/* What the Prefabs tab shows when searching: tiles filtered by the search text it was made for; made again when the text or tiles change. */
	std::array<std::array<std::vector<PoolId>, 5>, 2> shown;
	std::array<std::vector<PoolId>, 2> externalShown;
	std::string shownFor;
	bool shownStale = true;

	/* Where the Prefabs tab is: nav (scope, then game; see ScopeNav); then for a game group -1 = the groups; for External openFolder is the path inside ("" = the top, "a/b"). */
	ScopeNav nav;
	int openGroup = -1;
	std::string openFolder;

	/* The name popup of "New folder" (1) / "New prefab" (2); 0 = none. */
	int creating = 0;
	char newName[64] = "";

	/* The Output tab: the message count last drawn (to scroll down when a new one arrives while at the bottom). */
	uint64_t outputSeen = 0;

	void rebuildTiles();
	void filterTiles();
	void drawPath();
	void drawBrowser();
	void drawSearchResults();
	void drawGrid(const std::vector<PoolId>& rList_);
	void drawNewMenu();
	/* The "Output" tab: the messages of the scripts (Scripting/ScriptLog.h), errors red; follows the newest unless scrolled up. */
	void drawOutput();
	void drawTile(PoolId prefabId_, float cellWidth_);
	void spawn(PoolId prefabId_, PoolId& rSelectedEntity_, const EditorCamera& rCamera_);
};
}
