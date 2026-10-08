#pragma once

#ifdef EDITOR
#include "EditorCamera.h"
#include "Gizmo.h"
#include <GLFW/glfw3.h>
#include "DebugUI.h"
#include "CollisionView.h"
#include "Text.h"
#include "LevelLoader.h"
#include "EntityListPanel.h"
#include "InspectorPanel.h"
#include "PrefabPanel.h"
#include "CollisionShape.h"
#include "TextureChoice.h"
#include "MeshChoice.h"
#include "ExternalField.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

/*
Everything of the editor in one place: the debug UI and its panels, the collision view, the entity labels, the transform gizmo of the selected entity, the keys that drive them and a camera of its own. Only built with EDITOR (CMake); the gameplay
never reads it except through editorActive().
The editor is active (`active`; the toolbar's Play / Stop switch, setPlaying) from the start. While it is, the editor camera is what the screen shows (Renderer::runAll) and what the fly keys (W A S D, Space, Ctrl, Shift), the mouse buttons (right turn, left pan) and the wheel (zoom) move, with the cursor visible (EditorCamera.h); the gameplay's cameras
(Globals::Game::cameras, mainCamera) are not drawn on the screen, do not take the mouse and keep following their target, so nothing done in the editor moves them. The camera is not an entity and not in the camera pool: it has no
entry in the entity list and the scripts cannot reach it. Gameplay culling (GameplaySystem::tick) keeps using the gameplay main camera.
Order: init after the renderer, the scripts' main file and the main camera; processInputs and draw once per frame; destroy before the renderer.
*/
struct Editor
{
	/* True while in the editor (the start state), false while playing (the Play button, setPlaying). */
	bool active = true;

	/* While playing: the debug menu is open (Tab), with the cursor free for it. Always false in the editor. */
	bool debugMenuOpen = false;

	/* True while the cursor is free (visible, for the debug UI) and the mouse movement is not read: whenever the editor is active except during a button hold (EditorCamera::dragging). */
	bool cursorFree = false;

	/* False until init, so the window callbacks (which can run earlier) leave the camera alone. */
	bool initialized = false;

	EditorCamera camera;

	/* The distance to the model the camera looks at and the time until it is measured again (processInputs). */
	float speedProbeDistance = 0.0f;
	float speedProbeTimer = 0.0f;
	Graphics::DebugUI debugUI;
	Graphics::CollisionView collisionView;
	Graphics::Text text;
	Graphics::EntityListPanel entityList;
	Graphics::InspectorPanel inspector;
	Graphics::PrefabPanel prefabPanel;
	Graphics::Gizmo gizmo;

	/* The state of the keys the last time processInputs saw them, so that one press counts once. */
	bool tabPressed = false;
	bool escapePressed = false;
	bool cPressed = false;
	bool vPressed = false;
	bool zPressed = false;
	bool yPressed = false;
	bool sPressed = false;
	bool leftPressed = false;
	bool rightPressed = false;
	bool upPressed = false;
	bool downPressed = false;
	bool gizmoKeyPressed[3] = {false, false, false};

	/* Click to select (processInputs): a left press the UI and the gizmo did not take is pending until the button is released (a camera hold cancels it). clickX / clickY: where the press was. */
	bool clickLeftWas = false;
	bool clickPending = false;
	double clickX = 0.0;
	double clickY = 0.0;

	/* The first loaded level, UninitializedPoolId if none: the one the editor edits and saves (one level is loaded at a time). */
	PoolId currentLevel() const;

	/* The File menu's work (DebugUI): writes the current level's edits into the level file of the name (Loading::saveLevelEdits) and says in fileMessage how it went; or puts the level back as its source has it (the game's data plus the level's saved Source Game Level; Level::restoreSource, unsaved until Save). */
	void saveLevel(const std::string& fileName_);
	void restoreSourceValues();

	/* File > Save Source Game Level: makes the level as it is now the level's source (Loading::saveSourceLevel: Restore Source Game Level returns to it from now on), then loads the level again from its files so the editor holds exactly what a load gives (the selection is cleared, the players go to the start); says in fileMessage how it went. */
	void saveSourceLevel();

	/* Replaces the loaded level with Levels/<name>/<name>.json (the scripts' resetGame with a name: the level, the players at its start); what was edited and not saved in the old level is gone. */
	void loadLevel(const std::string& levelName_);
	std::string fileMessage;

	/* The File menu's New level (DebugUI): writes the level file (Loading::createLevelFile) and, if that worked, loads it as loadLevel does; says in fileMessage how it went. */
	void newLevel(const Loading::NewLevelFile& level_);

	/* The File menu's glTF level items (DebugUI; Loading::swapGLTFLevelModel / addGLTFLevelCollision / swapGLTFLevelCollision on the current level): each says in fileMessage how it went. Not run. */
	void swapLevelModel(const std::string& gltfPath_);
	void addLevelCollision(const std::string& gltfPath_);
	void swapLevelCollision(const std::string& name_, const std::string& gltfPath_);

	/*
	The Import menu (DebugUI; Loading::importExternalTexture / importExternalModel): a PNG or JPEG becomes an external texture, a glTF/.glb an external model, in the project's assets or, with inLevel_, in the open level's. An empty name_ takes the file's name.
	A model's textures are imported as "<model>_tex<i>"; one that exists already in the scope is kept as it is. Each says in fileMessage how it went. Not run.
	*/
	void importTexture(const std::string& path_, const std::string& name_, bool inLevel_);
	void importModel(const std::string& gltfPath_, const std::string& name_, bool inLevel_);

	/*
	What setPlaying keeps of one entity at Play for the way back. A property the editor can edit and Stop must give back is a field here (captured in one place, Editor.cpp keptEntityOf, and given back in one, giveKeptEntity), not a vector of its own.
	*/
	struct KeptEntity
	{
		glm::vec3 position;
		glm::mat4 rotation;
		glm::vec3 scale;
		std::vector<PoolId> behaviors;
		std::vector<Physics::CollisionShape> shapes;
		TextureChoice texture;
		MeshChoice mesh;
		/* The external field values (Entity::externals); an entity reference is kept as entityIndex (Level::editIndexOf). */
		std::map<std::string, std::map<std::string, ExternalValue>> externals;
		std::map<std::string, std::map<std::string, std::string>> externalsKept;
	};

	/* A level's own entity, by its index in Level::entityIds. */
	struct KeptOwn
	{
		uint32_t index;
		KeptEntity entity;
	};

	/* An entity added to the level, made again from its prefab. */
	struct KeptAdded
	{
		PoolId prefabId;
		KeptEntity entity;
	};

	/* The prefabs' own edits (behaviours, bound area shapes, model, texture). */
	struct KeptPrefab
	{
		PoolId prefabId;
		std::vector<PoolId> added;
		std::vector<PoolId> removed;
		std::vector<Physics::CollisionShape> addedShapes;
		std::vector<Physics::CollisionShape> removedShapes;
		TextureChoice texture;
		MeshChoice mesh;
	};
	std::vector<KeptOwn> keptOwn;
	std::vector<KeptAdded> keptAdded;
	std::vector<KeptPrefab> keptPrefabs;
	void clearKept();

	/* An entity's position, rotation and scale: what Ctrl+Z gives back. */
	struct Transform
	{
		glm::vec3 position;
		glm::mat4 rotation;
		glm::vec3 scale;
		bool operator==(const Transform&) const = default;
	};

	/* Ctrl+C: what was copied, a prefab entity (prefabId, entity) or a camera entity (isCamera, camera). */
	struct CopiedCamera
	{
		glm::vec3 front;
		float yaw;
		float pitch;
		float fov;
		float nearPlane;
		float farPlane;
		float speed;
		float speedUp;
		bool enabled;
		/* The main camera (a paste clears it: a copy never takes over), and the size of its render target (0 = the screen; a copy gets a render target of its own of that size). */
		bool isMain;
		uint32_t targetWidth;
		uint32_t targetHeight;
	};
	struct Clipboard
	{
		bool valid = false;
		bool isCamera = false;
		std::string name;
		PoolId prefabId = UninitializedPoolId;
		KeptEntity entity;
		CopiedCamera camera;
	};
	/* One step of Ctrl+Z / Ctrl+Y: either the entity's transform to give back (created false; before), or an entity the editor made (created true: undoing removes it, and keeps its state in snapshot so that redoing makes it again, with a new PoolId). */
	struct UndoAction
	{
		bool created;
		PoolId entity;
		Transform before;
		Clipboard snapshot;
	};
	std::vector<UndoAction> undoStack;
	std::vector<UndoAction> redoStack;

	/* The selected entity and its transform as last seen (trackTransform): a different value once nothing is being dragged is a finished change. */
	PoolId trackedEntity = UninitializedPoolId;
	Transform tracked;
	bool trackedBusy = false;

	Clipboard clipboard;

	/* The level's camera entities at Play (snapshotOf), made again at Stop: a camera is not in keptAdded (no prefab). */
	std::vector<Clipboard> keptCameras;

	/* A camera entity at a position that the level owns (it is saved with the level, Loading::saveLevelEdits "cameras"); the Add camera button and paste make cameras with it. */
	PoolId addLevelCamera(const char* name_, const glm::vec3& rPosition_);

	/* Puts an entity the editor made (Add camera, paste) on the undo stack and selects it. */
	void recordCreated(PoolId entityId_);

	/* Ctrl+Z undoes the latest step, Ctrl+Y does the latest undone one again; a new step empties the redo ones. Both stacks are dropped when the level is loaded again, restored or the game is played (the entities are made again). */
	void undo();
	void redo();
	void pushUndo(const UndoAction& rAction_);
	void applyAction(const UndoAction& rAction_, std::vector<UndoAction>& rOpposite_);
	Clipboard snapshotOf(PoolId entityId_);
	PoolId spawnFrom(const Clipboard& rCopy_, const glm::vec3& rPosition_);
	void clearUndo();

	/* Looks at the selected entity once a frame (processInputs, in the editor): when its transform differs from the last one seen and no gizmo drag or ImGui widget is being held, pushes the old transform (a whole drag is one step). */
	void trackTransform();

	/* Ctrl+C copies the selected entity; Ctrl+V makes a copy where the cursor points (the horizontal plane through the copied entity, else where it was), selected and undoable. A camera entity copies the camera's own settings (not its render target). Says in fileMessage when it can't. */
	void copySelected();
	void pasteAtCursor();

	/*
	editor_settings.json (next to Assets/, in the working directory): the panel sizes (the user drags their edges) and the editor camera's settings (EntityListPanel's "Editor camera" tab). init reads it (a missing or broken file
	leaves the defaults); destroy writes it only when its content differs from what init read, so a run with no change leaves the file alone.
	*/
	void loadSettings();
	void saveSettings();
	std::string savedSettings;

	void init(GLFWwindow* pWindow_);

	/* Puts the editor camera where the gameplay main camera is and looks where it looks (not its planes or fov). Done when the editor becomes active; main.cpp also does it once the level script has placed the main camera. */
	void matchGameplayCamera();

	/* Enters play (the gameplay camera is the view, the cursor is captured, the game ticks) or goes back to the editor (the editor camera starts at the gameplay camera). Going back resets the game (Scripting::resetGame: the level is loaded again, the players go to its start); the level's entities keep the positions and additions the editor had at Play, saved or not. */
	void setPlaying(bool playing_);

	/* Reads the editor's keys and mouse buttons: fly, the gizmo (1 / 2 / 3 pick its mode, its handle drags come before the camera's button hold), button holds, Tab (the debug menu, while playing), C, V and the arrows. With Ctrl held (either one): Z undoes, Y redoes, C copies, V pastes and S saves the level (Editor::saveLevel); C and V toggle nothing and the fly keys stop meanwhile. Not while an ImGui text field has the keyboard. */
	void processInputs();

	/* The editor camera's reaction to a mouse movement (Window callback, Utils.h mouseCallback): turns or pans it during a right button hold. */
	void onMouse(float xOffset_, float yOffset_);

	/* The editor camera's reaction to the wheel (Window callback, Utils.h scrollCallback): zoom, unless the debug UI is using the wheel. */
	void onScroll(float yOffset_);

	/* Draws the debug UI over the screen (after Renderer::runAll), if active. */
	void draw();

	void destroy();
};
inline Editor editor;

inline bool editorActive()
{
	return editor.active;
}
#else
/* The editor does not exist in this build: it is never active. */
inline bool editorActive()
{
	return false;
}
#endif
