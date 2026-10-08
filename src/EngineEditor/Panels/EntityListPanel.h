#pragma once
#include "PoolId.h"
#include "Constants.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Graphics
{
/*
The entity list of the editor: a panel along the left edge of the window with every live entity of Globals::Game::entities as a tree of folders (a name filter above, a button that adds a camera, one that makes a folder), clipped so the thousands
of a level's objects cost nothing. Clicking an entity selects it: selected is what the InspectorPanel shows. Drawn by DebugUI::draw, inside its ImGui frame; Editor::entityList.
The built-in folders group the entities by the prefab they were made from (Entity::prefabId): the game's folder, then the prefab's group (Prefab.h), then the prefab; entities without a prefab are in "Ungrouped". They are rebuilt every frame and
cannot be changed. The user's folders (New folder; inside the selected one, if a user folder is selected) hold entities the user put there, by the entity's right click menu "Move to folder" or by dragging the entity onto a folder (onto a built-in folder: back to its own); an entity in a
user folder is shown there instead of in its built-in one. Right click on a user folder deletes it (its folders move up, its entities go back to their built-in folders). While the filter has text every folder is open and those with no match are hidden.
The folders are the panel's own state: they are not saved and the scripts cannot see them.
The window is titled "Level". A second tab, "Collision", is the collision view's panel (CollisionView::drawPanel, the same as the Debug window's); a third, "Editor camera", holds the settings of Editor::camera (EditorCamera.h); Editor saves them with the panel sizes.
*/
struct EntityListPanel
{
	PoolId selected = UninitializedPoolId;

	/* The user folder New folder makes its folder in (-1 = at the top); set by clicking a user folder, cleared by clicking anything else (the editor clears it when a click in the scene picks an entity). */
	int selectedFolder = -1;
	char filter[64] = "";

	/* The panel's width in pixels: the user drags its right edge, draw() reads the result back, and Editor saves it (editor_settings.json). The other panels are laid out around it. */
	float width = 300.0f;

	void draw();

private:
	/* The tabs of the panel: the entity tree, (Collision is drawn by CollisionView) and the settings of the editor camera (look, pan, zoom and fly speeds). */
	void drawEntities();
	void drawCameraSettings();

	/* parent is an index into userFolders, -1 for the top; a folder is made after its parent, so a parent has the smaller index. A deleted folder keeps its slot (alive false) so that the indices stay valid. */
	struct UserFolder
	{
		std::string name;
		int parent;
		bool alive;
	};

	std::vector<UserFolder> userFolders;

	/* The user folder (index into userFolders) of each entity that is in one, by the entity's PoolId packed in 64 bits (generation << 32 | id). */
	std::unordered_map<uint64_t, int> entityFolder;

	/* The keys of the folders that are open (a folder's key is its path, e.g. "a/SADX/Enemies" for a built-in one, "u3" for the user folder 3). */
	std::unordered_set<std::string> openFolders;

	char newFolderName[48] = "Folder";
};
}
