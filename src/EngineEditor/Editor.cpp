#include "Editor.h"
#include "Globals.h"
#include "API.h"
#include "Scripting.h"
#include "LevelLoader.h"
#include "Level.h"
#include "Prefab.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "ExternalAssets.h"
#include "GLTFFile.h"
#include "TextureFile.h"
#include <filesystem>
#include <format>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

namespace
{
using Json = nlohmann::json;
constexpr const char* SettingsPath = "editor_settings.json";
}

/* Keeps a value read from the file inside a range, so a hand-edited file cannot make the panels or the camera unusable. */
void Editor::loadSettings()
{
	std::ifstream stream(SettingsPath);
	if(!stream) return;
	Json json = Json::parse(stream, nullptr, false);
	if(!json.is_object()) return;
	this->savedSettings = json.dump(4);

	this->entityList.width = std::clamp(json.value("entityListWidth", this->entityList.width), 150.0f, 2000.0f);
	this->inspector.width = std::clamp(json.value("inspectorWidth", this->inspector.width), 200.0f, 2000.0f);
	this->prefabPanel.height = std::clamp(json.value("prefabPanelHeight", this->prefabPanel.height), 100.0f, 2000.0f);
	if(!json.contains("camera") || !json["camera"].is_object()) return;
	const Json& rCamera = json["camera"];
	EditorCamera& rEditorCamera = this->camera;
	rEditorCamera.lookSensitivity = std::clamp(rCamera.value("lookSensitivity", rEditorCamera.lookSensitivity), 0.005f, 0.5f);
	rEditorCamera.panPerSpeed = std::clamp(rCamera.value("panPerSpeed", rEditorCamera.panPerSpeed), 0.0005f, 0.05f);
	rEditorCamera.zoomPerSpeed = std::clamp(rCamera.value("zoomPerSpeed", rEditorCamera.zoomPerSpeed), 0.01f, 2.0f);
	rEditorCamera.speedPerNotch = std::clamp(rCamera.value("speedPerNotch", rEditorCamera.speedPerNotch), 1.01f, 2.0f);
	rEditorCamera.speed = std::clamp(rCamera.value("speed", rEditorCamera.speed), rEditorCamera.minSpeed, rEditorCamera.maxSpeed);
	rEditorCamera.speedUp = std::clamp(rCamera.value("speedUp", rEditorCamera.speedUp), 1.0f, 100.0f);
	rEditorCamera.fov = std::clamp(rCamera.value("fov", rEditorCamera.fov), 10.0f, 150.0f);
	rEditorCamera.nearPlane = std::clamp(rCamera.value("nearPlane", rEditorCamera.nearPlane), 0.01f, 100.0f);
	rEditorCamera.farPlane = std::clamp(rCamera.value("farPlane", rEditorCamera.farPlane), 100.0f, 500000.0f);
	rEditorCamera.refreshProjection();
}

void Editor::saveSettings()
{
	Json json;
	json["entityListWidth"] = this->entityList.width;
	json["inspectorWidth"] = this->inspector.width;
	json["prefabPanelHeight"] = this->prefabPanel.height;
	const EditorCamera& rCamera = this->camera;
	json["camera"] = {
		{"lookSensitivity", rCamera.lookSensitivity},
		{"panPerSpeed", rCamera.panPerSpeed},
		{"zoomPerSpeed", rCamera.zoomPerSpeed},
		{"speedPerNotch", rCamera.speedPerNotch},
		{"speed", rCamera.speed},
		{"speedUp", rCamera.speedUp},
		{"fov", rCamera.fov},
		{"nearPlane", rCamera.nearPlane},
		{"farPlane", rCamera.farPlane}};

	std::string text = json.dump(4);
	if(text == this->savedSettings) return;
	std::ofstream stream(SettingsPath);
	if(stream) stream << text << '\n';
}

void Editor::init(GLFWwindow* pWindow_)
{
	this->camera = EditorCamera(glm::vec3(0.0f));

	/* The editor starts active: the cursor is visible for the UI (the window is created with it captured). */
	this->cursorFree = true;
	glfwSetInputMode(pWindow_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	this->collisionView.init();
	this->debugUI.init(pWindow_);
	this->prefabPanel.init();
	this->loadSettings();
	this->initialized = true;
}

PoolId Editor::currentLevel() const
{
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(Globals::Game::levels.alive[i]) return PoolId(i, Globals::Game::levels.generation[i]);
	}
	return UninitializedPoolId;
}

void Editor::saveLevel(const std::string& fileName_)
{
	PoolId levelId = this->currentLevel();
	if(levelId == UninitializedPoolId || fileName_.empty())
	{
		this->fileMessage = "Nothing saved: no level loaded or no file name.";
		return;
	}
	try
	{
		Loading::saveLevelEdits(levelId, fileName_);
		this->fileMessage = "Saved Levels/" + fileName_ + "/" + fileName_ + ".json";
	}
	catch(const std::exception& rError)
	{
		this->fileMessage = std::string("Save failed: ") + rError.what();
	}
}

void Editor::clearKept()
{
	this->keptOwn.clear();
	this->keptAdded.clear();
	this->keptPrefabs.clear();
}

namespace
{
/* The editable state of one entity, as setPlaying keeps it (Editor::KeptEntity). */
Editor::KeptEntity keptEntityOf(const Entity& rEntity_)
{
	Editor::KeptEntity kept;
	kept.position = rEntity_.position;
	kept.rotation = rEntity_.rotation;
	kept.scale = rEntity_.scale;
	if(rEntity_.behaviorSetId != UninitializedPoolId) kept.behaviors = Globals::Game::entityBehaviors[rEntity_.behaviorSetId].behaviorIds;
	kept.shapes = entityShapes(rEntity_.selfId);
	kept.texture = rEntity_.textureChoice;
	kept.mesh = rEntity_.meshChoice;
	return kept;
}

/* Puts a kept state on an entity. A model or texture that can't be given again (its external file was removed meanwhile) is left out; the reason is on stderr. */
void giveKeptEntity(PoolId entityId_, const Editor::KeptEntity& rKept_)
{
	Entity& rEntity = getEntity(entityId_);
	rEntity.setPosition(rKept_.position);
	rEntity.setRotation(rKept_.rotation);
	rEntity.setScale(rKept_.scale);
	setEntityBehaviors(entityId_, rKept_.behaviors);
	setEntityShapes(entityId_, rKept_.shapes);
	if(!(rKept_.mesh == entityMesh(entityId_)))
	{
		try
		{
			setEntityMesh(entityId_, rKept_.mesh);
		}
		catch(const std::runtime_error& rError)
		{
			std::fprintf(stderr, "Model: not kept over Stop: %s\n", rError.what());
		}
	}
	if(!(rKept_.texture == entityTexture(entityId_)))
	{
		try
		{
			setEntityTexture(entityId_, rKept_.texture);
		}
		catch(const std::runtime_error& rError)
		{
			std::fprintf(stderr, "Texture: not kept over Stop: %s\n", rError.what());
		}
	}
}
}

void Editor::loadLevel(const std::string& levelName_)
{
	this->entityList.selected = UninitializedPoolId;
	this->clearKept();
	Scripting::resetGame(levelName_.c_str());
	Globals::Game::spatialIndex.rebuild();
	this->matchGameplayCamera();
	this->fileMessage = "Loaded " + levelName_ + " (a failure is on stderr).";
}

void Editor::newLevel(const Loading::NewLevelFile& level_)
{
	try
	{
		Loading::createLevelFile(level_);
	}
	catch(const std::exception& rError)
	{
		this->fileMessage = std::string("New level failed: ") + rError.what();
		return;
	}
	this->loadLevel(level_.name);
	this->fileMessage = "Made Levels/" + level_.name + "/" + level_.name + ".json and loaded it (a load failure is on stderr).";
}

void Editor::swapLevelModel(const std::string& gltfPath_)
{
	PoolId levelId = this->currentLevel();
	if(levelId == UninitializedPoolId || !Loading::isGLTFLevel(levelId)) return;
	try
	{
		Loading::swapGLTFLevelModel(levelId, gltfPath_);
		this->fileMessage = "Swapped the level model for " + gltfPath_;
	}
	catch(const std::exception& rError)
	{
		this->fileMessage = std::string("Swap level model failed: ") + rError.what();
	}
}

void Editor::addLevelCollision(const std::string& gltfPath_)
{
	PoolId levelId = this->currentLevel();
	if(levelId == UninitializedPoolId || !Loading::isGLTFLevel(levelId)) return;
	try
	{
		this->fileMessage = "Added the collision " + Loading::addGLTFLevelCollision(levelId, gltfPath_);
	}
	catch(const std::exception& rError)
	{
		this->fileMessage = std::string("Add collision failed: ") + rError.what();
	}
}

void Editor::importTexture(const std::string& path_, const std::string& name_, bool inLevel_)
{
	try
	{
		std::string name = name_.empty() ? std::filesystem::path(path_).stem().string() : name_;
		Loading::importExternalTexture(name, External::readTexture(path_), inLevel_ ? Loading::ExternalScope::Level : Loading::ExternalScope::Project);
		this->fileMessage = "Imported the texture " + name;
	}
	catch(const std::exception& rError)
	{
		this->fileMessage = std::string("Import texture failed: ") + rError.what();
	}
}

void Editor::importModel(const std::string& gltfPath_, const std::string& name_, bool inLevel_)
{
	try
	{
		std::string name = name_.empty() ? std::filesystem::path(gltfPath_).stem().string() : name_;
		Loading::ExternalScope scope = inLevel_ ? Loading::ExternalScope::Level : Loading::ExternalScope::Project;
		std::vector<Graphics::Texture> textures;
		std::vector<std::string> textureNames;
		External::ModelData model = External::readGLTFModel(gltfPath_, name, textures, textureNames);

		/* The glTF's image names become "<model>_tex<i>" file names, as a glTF level's textures do, so two imports never share a name. */
		const Graphics::ExternalPools& rPools = inLevel_ ? Graphics::externalLevel : Graphics::externalProject;
		std::map<std::string, std::string> fileNameOf;
		for(size_t i = 0; i < textures.size(); i++)
		{
			std::string textureName = std::format("{}_tex{}", name, i);
			if(!rPools.textures.nameToId.contains(textureName)) Loading::importExternalTexture(textureName, std::move(textures[i]), scope);
			fileNameOf[textureNames[i]] = textureName;
		}
		for(External::ModelSubmesh& rSubmesh : model.submeshes)
		{
			if(!rSubmesh.texture.empty()) rSubmesh.texture = fileNameOf.at(rSubmesh.texture);
		}
		Loading::importExternalModel(std::move(model), scope);
		this->fileMessage = "Imported the model " + name;
	}
	catch(const std::exception& rError)
	{
		this->fileMessage = std::string("Import model failed: ") + rError.what();
	}
}

void Editor::swapLevelCollision(const std::string& name_, const std::string& gltfPath_)
{
	PoolId levelId = this->currentLevel();
	if(levelId == UninitializedPoolId || !Loading::isGLTFLevel(levelId)) return;
	try
	{
		Loading::swapGLTFLevelCollision(levelId, name_, gltfPath_);
		this->fileMessage = "The collision " + name_ + " now holds " + gltfPath_;
	}
	catch(const std::exception& rError)
	{
		this->fileMessage = std::string("Swap collision failed: ") + rError.what();
	}
}

void Editor::restoreSourceValues()
{
	PoolId levelId = this->currentLevel();
	if(levelId == UninitializedPoolId) return;
	this->entityList.selected = UninitializedPoolId;
	getLevel(levelId).restoreSource();
	Globals::Game::spatialIndex.rebuild();
	this->fileMessage = "Restored the game's values, prefab behaviour, bound area, model and texture edits included (not saved until File > Save).";
}

void Editor::matchGameplayCamera()
{
	if(Globals::Game::mainCamera == UninitializedPoolId) return;
	const Camera& rGameplay = getMainCamera();
	this->camera.position = rGameplay.position;
	this->camera.yaw = rGameplay.yaw;
	this->camera.pitch = rGameplay.pitch;
	this->camera.front = rGameplay.front;
	this->camera.updateView();
	this->camera.updateUBO();
}

void Editor::setPlaying(bool playing_)
{
	GLFWwindow* pWindow = Globals::App::window;

	/*
	The level's state as the editor leaves it, kept for the way back: Stop loads the level again from its file, which would drop what is edited and not saved. Each of the level's own entities (by index) and each entity added to it (with its prefab): position, rotation, scale, behaviours, bound area, model, texture (Editor::KeptEntity), and the prefabs' edits.
	*/
	if(playing_ && this->active)
	{
		this->clearKept();
		for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
		{
			const Prefab& rPrefab = Globals::Game::prefabs.objects[i];
			if(!Globals::Game::prefabs.alive[i] || (rPrefab.addedBehaviors.empty() && rPrefab.removedBehaviors.empty() && rPrefab.addedShapes.empty() && rPrefab.removedShapes.empty() && rPrefab.texture.kind == TextureChoiceKind::Default && rPrefab.mesh.kind == MeshChoiceKind::Default)) continue;
			this->keptPrefabs.push_back({PoolId(i, Globals::Game::prefabs.generation[i]), rPrefab.addedBehaviors, rPrefab.removedBehaviors, rPrefab.addedShapes, rPrefab.removedShapes, rPrefab.texture, rPrefab.mesh});
		}
		PoolId levelId = this->currentLevel();
		if(levelId != UninitializedPoolId)
		{
			const Level& rLevel = getLevel(levelId);
			for(uint32_t i = 0; i < rLevel.entityIds.size(); i++)
			{
				if(!entityExists(rLevel.entityIds[i])) continue;
				const Entity& rEntity = getEntity(rLevel.entityIds[i]);
				if(i < rLevel.sourceEntityCount) this->keptOwn.push_back({i, keptEntityOf(rEntity)});
				else if(rEntity.prefabId != UninitializedPoolId) this->keptAdded.push_back({rEntity.prefabId, keptEntityOf(rEntity)});
			}
		}
	}
	this->active = !playing_;
	this->debugMenuOpen = false;
	this->camera.dragging = false;
	this->cursorFree = this->active;
	glfwSetInputMode(pWindow, GLFW_CURSOR, this->active ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
	Globals::Mouse::firstMouse = true;
	if(this->active)
	{
		/* The game goes back to its start: the level's objects are made again from its data (the scripts' resetGame), so what the play moved, what the editor added or moved before it is put back (kept at Play). Entities of the old level are gone: nothing stays selected, and the level load makes the markers again. */
		this->entityList.selected = UninitializedPoolId;
		Scripting::resetGame();

		/* The reloaded level has the file's edits; the editor's own state replaces them (its added entities are made again from their prefabs). */
		PoolId levelId = this->currentLevel();
		if(levelId != UninitializedPoolId)
		{
			Level& rLevel = getLevel(levelId);
			rLevel.restoreSource();

			/* The prefabs' edits first (new entities get them at spawn), then each entity's exact list. */
			for(const KeptPrefab& rPrefab : this->keptPrefabs)
			{
				for(PoolId behaviorId : rPrefab.added) addPrefabBehavior(rPrefab.prefabId, behaviorId);
				for(PoolId behaviorId : rPrefab.removed) removePrefabBehavior(rPrefab.prefabId, behaviorId);
				for(const Physics::CollisionShape& rShape : rPrefab.addedShapes) addPrefabShape(rPrefab.prefabId, rShape);
				for(const Physics::CollisionShape& rShape : rPrefab.removedShapes) removePrefabShape(rPrefab.prefabId, rShape);
				if(rPrefab.mesh.kind != MeshChoiceKind::Default) setPrefabMesh(rPrefab.prefabId, rPrefab.mesh);
				if(rPrefab.texture.kind != TextureChoiceKind::Default) setPrefabTexture(rPrefab.prefabId, rPrefab.texture);
			}

			for(const KeptOwn& rOwn : this->keptOwn)
			{
				if(rOwn.index >= rLevel.sourceEntityCount || !entityExists(rLevel.entityIds[rOwn.index])) continue;
				giveKeptEntity(rLevel.entityIds[rOwn.index], rOwn.entity);
			}
			for(const KeptAdded& rAdded : this->keptAdded)
			{
				SpawnParams params;
				params.position = rAdded.entity.position;
				params.what = "(editor)";
				PoolId id = spawnPrefab(rAdded.prefabId, Graphics::getShaderId(Globals::Game::prefabs[rAdded.prefabId].game == SA_NINJA ? "SA" : "STH2005"), params);
				if(id == UninitializedPoolId) continue;
				rLevel.entityIds.push_back(id);
				giveKeptEntity(id, rAdded.entity);
			}
		}
		Globals::Game::spatialIndex.rebuild();
		this->matchGameplayCamera();
	}
}

void Editor::processInputs()
{
	GLFWwindow* pWindow = Globals::App::window;
	EditorCamera& rCamera = this->camera;

	/* The camera only reacts while the editor is active: outside it the keys control the player, and the gameplay camera follows it (main.cpp updateFollowCamera). The keys are left to the UI while it has a text field. */
	if(this->active)
	{
		if(!this->debugUI.wantsKeyboard()) rCamera.processKeys(pWindow, Globals::App::deltaTime);

		/*
		The gizmo takes a left press on one of its handles before the camera can start a pan with it, and keeps the mouse until the handle is released. 1 / 2 / 3 pick Move / Rotate / Scale (W is a fly key, so W E R is not used); not during a drag or a camera hold, nor while a text field has the keyboard.
		*/
		this->gizmo.update(pWindow, rCamera, this->entityList.selected, this->debugUI.wantsMouse(), rCamera.dragging);
		for(int i = 0; i < 3; i++)
		{
			bool keyDown = glfwGetKey(pWindow, GLFW_KEY_1 + i) == GLFW_PRESS;
			if(keyDown && !this->gizmoKeyPressed[i] && !this->gizmo.wantsMouse() && !rCamera.dragging && !this->debugUI.wantsKeyboard()) this->gizmo.mode = static_cast<Graphics::Gizmo::Mode>(i);
			this->gizmoKeyPressed[i] = keyDown;
		}
		if(rCamera.updateDrag(pWindow, this->debugUI.wantsMouse() || this->gizmo.wantsMouse())) this->cursorFree = !rCamera.dragging;
	}

	/*
	Tab opens and closes the debug menu while playing (nothing in the editor): the cursor is free for it and the mouse look stops while it is open. The next mouse position becomes the baseline when the look starts again, so it does not jump.
	*/
	bool tabDown = glfwGetKey(pWindow, GLFW_KEY_TAB) == GLFW_PRESS;
	if(tabDown && !this->tabPressed && !this->active)
	{
		this->debugMenuOpen = !this->debugMenuOpen;
		this->cursorFree = this->debugMenuOpen;
		glfwSetInputMode(pWindow, GLFW_CURSOR, this->debugMenuOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
		Globals::Mouse::firstMouse = true;
	}
	this->tabPressed = tabDown;

	/* Escape stops the play and goes back to the editor (nothing in the editor itself). */
	bool escapeDown = glfwGetKey(pWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS;
	if(escapeDown && !this->escapePressed && !this->active) this->setPlaying(false);
	this->escapePressed = escapeDown;

	/* C toggles the collision lines view (Graphics::CollisionView). */
	bool cDown = glfwGetKey(pWindow, GLFW_KEY_C) == GLFW_PRESS;
	if(cDown && !this->cPressed) this->collisionView.enabled = !this->collisionView.enabled;
	this->cPressed = cDown;

	/* V switches the lines view between coloring by triangle attribute and by slope. */
	bool vDown = glfwGetKey(pWindow, GLFW_KEY_V) == GLFW_PRESS;
	if(vDown && !this->vPressed) this->collisionView.colorByAttribute = !this->collisionView.colorByAttribute;
	this->vPressed = vDown;

	/* Left / Right move the highlighted attribute of the lines view along the legend, through "none": the entities' legend while the debug UI shows its Entities sub tab, otherwise the level's. */
	bool leftDown = glfwGetKey(pWindow, GLFW_KEY_LEFT) == GLFW_PRESS;
	bool rightDown = glfwGetKey(pWindow, GLFW_KEY_RIGHT) == GLFW_PRESS;
	if(leftDown && !this->leftPressed) this->collisionView.entityTabActive ? this->collisionView.selectEntityAttribute(-1) : this->collisionView.selectAttribute(-1);
	if(rightDown && !this->rightPressed) this->collisionView.entityTabActive ? this->collisionView.selectEntityAttribute(1) : this->collisionView.selectAttribute(1);
	this->leftPressed = leftDown;
	this->rightPressed = rightDown;

	/* Up / Down switch the labels over the entities (Graphics::Text) between entity, mesh and collision names. */
	bool upDown = glfwGetKey(pWindow, GLFW_KEY_UP) == GLFW_PRESS;
	bool downDown = glfwGetKey(pWindow, GLFW_KEY_DOWN) == GLFW_PRESS;
	if(upDown && !this->upPressed) this->text.selectMode(-1);
	if(downDown && !this->downPressed) this->text.selectMode(1);
	this->upPressed = upDown;
	this->downPressed = downDown;
}

void Editor::onMouse(float xOffset_, float yOffset_)
{
	this->camera.onMouseMove(Globals::App::window, xOffset_, yOffset_);
}

void Editor::onScroll(float yOffset_)
{
	if(!this->initialized || !this->active || this->debugUI.wantsMouse()) return;
	this->camera.onScroll(Globals::App::window, yOffset_);
}

void Editor::draw()
{
	if(this->active || this->debugMenuOpen) this->debugUI.draw(*this);
}

void Editor::destroy()
{
	this->saveSettings();
	this->prefabPanel.destroy();
	this->debugUI.destroy();
	this->collisionView.destroy();
	this->camera.destroy();
	this->initialized = false;
}
