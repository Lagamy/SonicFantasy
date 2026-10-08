#pragma once
#include "AssetThumbnails.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct Level;

namespace Graphics
{
struct PrefabThumbnails;
/*
The "Assets" tab of the editor's bottom panel (PrefabPanel draws the window and the tab bar and calls draw inside the tab). A file browser: the current folder is shown as tiles, a folder tile (the icon Assets/Catalog/folder_icon.png, made from folder_icon.svg) opens on click, and a
path bar above has a button per level to go back. Levels: "Global" and "Level" (openScope); inside each the same three folders, the games (prefabGameFolder) and "External"; inside a game the folders "Models", "Textures" and "Scripts" (with their counts); models and textures have a folder per group (prefabGroupFolder, the same groups as the prefabs, empty ones left out), inside it one tile per asset by name;
scripts have the folders and files of their directory tree; a script file is itself a folder in disguise (folder icon, click opens it): inside are one tile per behaviour that file registered with sf.behavior (EntityBehavior::sourceFile == the file's path, so only once the script is loaded), which can be dragged onto the inspector's Behaviour tab.
Models and textures are the files of the two discs: Assets/Catalog/AssetCatalog.tsv, made by tools/genAssetCatalog.py from ResourceMap.txt (rows: game, kind, group, name, source; the groups are a hypothesis made from names, see the tool)
and read when the tab is shown. Scripts are the .lua files below Assets/Scripts/sadx and Assets/Scripts/shadow (the copy CMake makes of Scripts/), found when the tab is shown, with no groups, shown in the subfolders they are in; Scripts/main.lua and game.lua belong to no game and are not listed.
This is only the browser: a tile has a flat placeholder picture; for a texture (a SADX .GVM or .GVR, a Shadow .TXD) the picture is the first texture, for a model (not a land table) it is the model drawn like a prefab (PrefabThumbnails::renderModel), made from the disc only while the tile is on screen or near it and freed when it is not (AssetThumbnails), and clicking only selects (selected, shown in the tab). Big groups (thousands of tiles) are drawn clipped to the rows on screen.
Under Global the two games' folders list every disc asset; under Level only the ones the open level uses (refreshLevel; hypothesis, from its entities: Entity::discMesh / discTexture matched to the catalog source by name, and the files of their behaviours (EntityBehavior::sourceFile); the level's own land table and texture set are not counted, and with no level open the folders are empty). "External" differs: under Global it is game index 2 (Graphics::externalProject, the editor-wide External assets), under Level game index 3 (Graphics::externalLevel, the open level's, in its own folder), each with Models and Textures (no groups; a texture tile shows the texture itself, a model a placeholder); they are rebuilt whenever Loading::externalRevision changes, also while the tab is open. The other lists are loaded when the tab is opened and freed when it is left (unload), not at editor start. Editor::prefabPanel owns it (PrefabPanel::assets).
*/
struct AssetPanel
{
	/* The tab's content, inside the tab item. Loads the lists first if they are not loaded (load). A non-empty filter_ replaces the browser with the search: every asset of every game, kind and group whose name holds the text (case does not matter), in sections by where it lives (drawSearch). */
	void draw(const std::string& filter_);

	/* Start and end of every frame's pictures (AssetThumbnails), called by PrefabPanel every frame, tab shown or not: the end frees the pictures of the tiles that were not drawn. */
	void setModelRenderer(PrefabThumbnails* pRenderer_);
	void beginFrame();
	void endFrame();
	void destroy();

	/* The icon-and-label folder tiles of the browser, in rows (made public for the Prefabs tab, which looks the same); returns the index of the clicked one or -1. loadFolderIcon first (it reads the icon once). */
	int drawFolders(const std::vector<std::string>& labels_);
	void loadFolderIcon();

	/* The first loaded level (the one the editor edits), nullptr if none. Public for the Prefabs tab, whose Level folder lists the prefabs of its entities. */
	static const Level* openLevel();

	/* Frees the lists and the selection; PrefabPanel calls it every frame the tab is not the shown one, so nothing is held (or read) while the tab is closed. Does nothing if not loaded. */
	void unload();

private:
	/* The game indices of the External assets: Global (the project's) and Level (the open level's); GameCount counts them with the two games. */
	static constexpr int Global = 2;
	static constexpr int Level = 3;
	static constexpr int GameCount = 4;

	enum Kind
	{
		Model,
		Texture,
		Script,
		KindCount
	};

	struct Asset
	{
		std::string name;
		std::string source;
		/* Scripts only: the folder inside the game's script folder ("" or "replays"); the name is the file name alone. */
		std::string folder;
		/* Disc assets only: the open level uses it (refreshLevel); read only under the Level folder. */
		bool inLevel = false;
		/* External textures only: the texture's GL id (the pools keep it alive; the list is rebuilt when they change). */
		GLuint glTexture = 0;
	};

	/* Per game (0 = SADX, 1 = Shadow, 2 = Global, 3 = Level: external models and textures only, in group 0, no group folder), kind and group (PrefabGroup order; scripts use group 0 only), by name. */
	std::array<std::array<std::array<std::vector<Asset>, 5>, KindCount>, GameCount> assets;
	std::array<std::array<uint32_t, KindCount>, GameCount> counts{};
	/* The same counts of only the assets the open level uses (disc games only). */
	std::array<std::array<uint32_t, KindCount>, 2> levelCounts{};
	/* What refreshLevel last looked at (the level and its entity count); ~0 = not yet. */
	uint64_t levelSeen = ~0ull;

	/* Where the browser is: openScope -1 = Global / Level, then 0 = Global, 1 = Level; game -1 = the list of games and External (External is game Global + openScope); then kind -1 = the kinds; then group -1 = the groups (models and textures); scriptFolder is the open folder of the scripts. */
	int openScope = -1;
	int openGame = -1;
	int openKind = -1;
	int openGroup = -1;
	std::string scriptFolder;
	/* The open script file (its path, Asset::source) while the browser is inside it showing its behaviours; empty = none open. */
	std::string scriptFile;
	/* True while drawSearch draws the tiles: a script tile then only selects instead of opening, so the search stays on screen. */
	bool searching = false;

	/* The selected tile: game, kind, group and index in the list; game -1 = none. */
	int selectedGame = -1;
	int selectedKind = 0;
	int selectedGroup = 0;
	int selectedIndex = 0;

	/* The search: the text the hits were made for, and the hits (the assets whose name holds it, per list). Stale (made again at the next search) whenever a list changes. */
	struct SearchHit
	{
		int game;
		int kind;
		int group;
		std::vector<int> indices;
	};
	std::string searchKey;
	std::vector<SearchHit> searchHits;
	bool searchStale = true;

	bool loaded = false;
	uint32_t externalSeen = 0;
	GLuint folderTexture = 0;
	AssetThumbnails thumbnails;

	/* Reads the catalog and finds the scripts, when the tab is first shown after start or after unload. A missing catalog leaves the models and textures empty and is reported on stderr. */
	void load();
	void loadCatalog();
	void loadScripts();
	/* The Global and Level folders' lists from the external pools (Loading::externalRevision says when they changed: draw rebuilds them then, so an import shows at once with no closing and reopening). */
	void loadExternal();
	void sortAndCount(int game_);
	/* Marks the disc assets the open level uses (Asset::inLevel) and counts them (levelCounts); drawn again only when the level or its entity count changed. */
	void refreshLevel();
	/* The indices of the list that the open level uses, for the Level folder. */
	std::vector<int> levelIndices(int game_, int kind_, int group_) const;
	/* The tiles of a list (all of it, or only the indices of pOnly_), clipped to the rows on screen. */
	void drawTiles(int game_, int kind_, int group_, const std::vector<int>* pOnly_ = nullptr);
	/* The search results, the tiles of each hit list under a heading naming the list. */
	void drawSearch(const std::string& filter_);
	/* The tiles of the behaviours the open script file registered, by name; each is a drag source ("BEHAVIOR" payload: the name). */
	void drawBehaviours();
	void drawPath(const std::vector<std::string>& names_, const std::vector<int>& levels_);
};
}
