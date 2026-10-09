#pragma once
#include "Camera.h"
#include "LevelLoader.h"
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

struct Editor;

namespace Graphics
{
/*
Dear ImGui overlay, kept apart from the scene renderer: init after the renderer (it needs the GL context and the window's
callbacks, which it chains), draw once per frame after Renderer::run and before the buffer swap, destroy before the renderer.
The entity list and the inspector (src/EngineEditor/Panels) are along the left and right edges; the other panels (Game, Collision, Markers) are one window with a tab per panel. draw reads and changes the state of the Editor it is given (collision view, labels, panels); the entity labels are projected with the editor camera. The cursor is captured by the camera until it is freed (main.cpp), so the controls only work then; wantsMouse says whether ImGui
is using the mouse (the cursor is over its window), so a click elsewhere can give the mouse back to the camera.
*/
struct DebugUI
{
	void init(GLFWwindow* pWindow_);
	void draw(Editor& rEditor_);
	bool wantsMouse() const;

	/* Whether ImGui is using the keyboard (a text field has focus). */
	bool wantsKeyboard() const;
	void destroy();

	/* The text field of the File menu's Save as. */
	char saveAsName[64] = "";

	/* The fields of the File menu's New level; engine 0 = sadx, 1 = sth2005, 2 = gltf. */
	char newName[64] = "";
	int newEngine = 0;
	char newShader[32] = "SA";
	char newLandTableRel[128] = "";
	char newLandTableOffset[32] = "";
	char newTextures[128] = "";
	char newTxd[64] = "";
	char newSections[512] = "";
	char newPlacementFiles[256] = "";
	char newEntityTable[64] = "";
	char newTypeNames[64] = "";
	char newModel[256] = "";
	char newCollisions[512] = "";

	/* The path fields of the File menu's Swap level model and Swap collision > Add collision file popups. */
	char swapModelPath[256] = "";
	char addCollisionPath[256] = "";

	/* Swap collision: the collision file picked in the side menu and the path of the file that replaces it. */
	std::string swapCollisionName;
	char swapCollisionPath[256] = "";

	/* The Import menu's popups: the file path, the asset's name (empty = the file's name) and whether it goes into the open level (else the project). */
	char importTexturePath[256] = "";
	char importTextureName[64] = "";
	bool importTextureInLevel = false;
	char importModelPath[256] = "";
	char importModelName[64] = "";
	bool importModelInLevel = false;

	/* The level files found when Load level was chosen. */
	std::vector<std::string> levelNames;
};
}
