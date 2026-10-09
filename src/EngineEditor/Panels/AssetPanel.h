#pragma once
#include "AssetThumbnails.h"
#include "ScopeNav.h"
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
Under Global the two games' folders list every disc asset; under Level only the ones the open level uses (refreshLevel; hypothesis, from its entities and those a script spawned (Scripting::spawnedEntities, the players; orange tiles; and a prefab held in a prefab field of a behaviour (Level::referencedPrefabs), through one of its entities if any exists): Entity::discMesh / discTexture matched to the catalog source by name, and the files of their behaviours (EntityBehavior::sourceFile); the level's own land table and texture set are not counted, and with no level open the folders are empty). "External" differs: under Global it is game index 2 (Graphics::externalProject, the editor-wide External assets), under Level game index 3 (Graphics::externalLevel, the open level's, in its own folder), each with Models and Textures (Global's also Scripts: Assets/External/Scripts, loaded at start by Scripting::loadExternalScripts; no groups; a texture tile shows the texture itself, a model a placeholder); they are rebuilt whenever Loading::externalRevision changes, also while the tab is open. The other lists are loaded when the tab is opened and freed when it is left (unload), not at editor start. Editor::prefabPanel owns it (PrefabPanel::assets).
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

	/* The icon-and-label folder tiles of the browser, in rows (made public for the Prefabs tab, which looks the same); returns the index of the clicked one or -1 (pRightClicked_, if given, gets the index of the right-clicked one or -1). loadFolderIcon first (it reads the icon once). */
	int drawFolders(const std::vector<std::string>& labels_, int* pRightClicked_ = nullptr);
	void loadFolderIcon();

	/* The first loaded level (the one the editor edits), nullptr if none. Public for the Prefabs tab, whose Level folder lists the prefabs of its entities. */
	static const Level* openLevel();

	/* Frees the lists and the selection; PrefabPanel calls it every frame the tab is not the shown one, so nothing is held (or read) while the tab is closed. Does nothing if not loaded. */
	void unload();

	/* The command that opens a script file ("code", "kate"; run as <command> '<file>'); empty = the system's default application. Saved in the editor settings by Editor (scriptEditor). */
	char scriptEditorCommand[256] = {};

private:
	/* The game indices of the External assets: Global (the project's) and Level (the open level's); GameCount counts them with the two games. */
	static constexpr int Global = ScopeNav::Global;
	static constexpr int Level = Global + 1;
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
		/* Disc assets only: a script-spawned entity uses it (refreshLevel); its tile is orange. */
		bool spawned = false;
		/* External textures only: the texture's GL id (the pools keep it alive; the list is rebuilt when they change). */
		GLuint glTexture = 0;
	};

	/* Per game (0 = SADX, 1 = Shadow, 2 = Global, 3 = Level: external models and textures only; Global also scripts; in group 0, no group folder), kind and group (PrefabGroup order; scripts use group 0 only), by name. */
	std::array<std::array<std::array<std::vector<Asset>, 5>, KindCount>, GameCount> assets;
	std::array<std::array<uint32_t, KindCount>, GameCount> counts{};
	/* The same counts of only the assets the open level uses (disc games only). */
	std::array<std::array<uint32_t, KindCount>, 2> levelCounts{};
	/* What refreshLevel last looked at (the level and its entity count); ~0 = not yet. */
	uint64_t levelSeen = ~0ull;

	/* Where the browser is: nav (scope, then game; see ScopeNav); then kind -1 = the kinds; then group -1 = the groups (models and textures); scriptFolder is the open folder of the scripts. */
	ScopeNav nav;
	int openKind = -1;
	int openGroup = -1;
	std::string scriptFolder;
	/* The open script file (its path, Asset::source) while the browser is inside it showing its behaviours; empty = none open. */
	std::string scriptFile;
	/* Every folder below each script root (relative to it, "replays/old"), per game 0..Global: a folder with no file in it has no asset to show it by. */
	std::array<std::vector<std::string>, 3> scriptDirs;
	/* The script dialog asked for by a toolbar button or a tile's menu (NewFile, NewFolder, Delete; None = no), for drawScriptDialogs to open outside the tile child's ID stack; what it works on (the game and folder it was asked in, or the file to delete); the name field; the last result. */
	enum Dialog
	{
		None,
		NewFile,
		NewFolder,
		Delete,
		DeleteFolder
	};
	Dialog dialogRequest = None;
	bool newIsFolder = false;
	int dialogGame = 0;
	std::string dialogFolder;
	/* Delete: the script file's path (Asset::source); DeleteFolder: the folder's path inside Global's External scripts. */
	std::string deleteSource;
	/* The copied script file (its Asset::source path); empty = none. */
	std::string copiedSource;
	char newName[128] = {};
	std::string scriptMessage;
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
	/* Finds the scripts again after a file or folder was added or deleted. */
	void reloadScripts();
	/* The toolbar above a script folder's tiles (new Lua file, new folder, the opening command) and the dialogs it and a tile's menu ask for. */
	void drawScriptToolbar();
	void drawScriptDialogs();
	/* Creates the file or folder newName in dialogFolder of dialogGame's scripts, in the build's Assets and in the source tree; deletes the file deleteSource from both; opens a script file (the source tree's copy) with scriptEditorCommand. Each puts what went wrong in scriptMessage. */
	void createScript(bool folder_);
	void deleteScript();
	void deleteScriptFolder();
	/* Copies copiedSource into the open folder under a free name (name_copy.lua, name_copy2.lua ... if the name is taken), in both trees. */
	void pasteScript();
	void openScript(const std::string& source_);
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
