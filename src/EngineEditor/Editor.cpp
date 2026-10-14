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
#include "imgui.h"
#include <filesystem>
#include <format>
#include <cmath>
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
	if(json.contains("scriptEditor") && json["scriptEditor"].is_string()) std::snprintf(this->prefabPanel.assets.scriptEditorCommand, sizeof(this->prefabPanel.assets.scriptEditorCommand), "%s", json["scriptEditor"].get<std::string>().c_str());
	if(!json.contains("camera") || !json["camera"].is_object()) return;
	const Json& rCamera = json["camera"];
	EditorCamera& rEditorCamera = this->camera;
	rEditorCamera.lookSensitivity = std::clamp(rCamera.value("lookSensitivity", rEditorCamera.lookSensitivity), 0.005f, 0.5f);
	rEditorCamera.panPerSpeed = std::clamp(rCamera.value("panPerSpeed", rEditorCamera.panPerSpeed), 0.0005f, 0.05f);
	rEditorCamera.zoomPerSpeed = std::clamp(rCamera.value("zoomPerSpeed", rEditorCamera.zoomPerSpeed), 0.01f, 2.0f);
	rEditorCamera.referenceDistance = std::clamp(rCamera.value("referenceDistance", rEditorCamera.referenceDistance), 0.1f, 10000.0f);
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
	json["scriptEditor"] = std::string(this->prefabPanel.assets.scriptEditorCommand);
	const EditorCamera& rCamera = this->camera;
	json["camera"] = {
		{"lookSensitivity", rCamera.lookSensitivity},
		{"panPerSpeed", rCamera.panPerSpeed},
		{"zoomPerSpeed", rCamera.zoomPerSpeed},
		{"referenceDistance", rCamera.referenceDistance},
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
	return activeLevelId();
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

void Editor::saveAllLevels()
{
	int saved = 0;
	std::string failed;
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(!Globals::Game::levels.alive[i]) continue;
		const std::string& rName = Globals::Game::levels.names[i];
		try
		{
			if(Loading::saveLevelEdits(PoolId(i, Globals::Game::levels.generation[i]), rName)) saved++;
		}
		catch(const std::exception& rError)
		{
			failed += " " + rName + " (" + rError.what() + ")";
		}
	}
	std::string mapNote;
	if(this->mapUnsaved)
	{
		try
		{
			Globals::Game::map.write();
			this->mapUnsaved = false;
			mapNote = " Wrote Map.json.";
		}
		catch(const std::exception& rError)
		{
			mapNote = std::string(" Map.json not written: ") + rError.what();
		}
	}
	if(saved == 0 && failed.empty() && mapNote.empty()) this->fileMessage = "Nothing saved: no level loaded.";
	else this->fileMessage = std::format("Saved {} changed level(s).", saved) + (failed.empty() ? std::string() : " Save failed:" + failed) + mapNote;
}

void Editor::clearKept()
{
	this->keptLevels.clear();
}

namespace
{
/* The editable state of one entity, as setPlaying keeps it (Editor::KeptEntity). The entities its external fields refer to are kept as their index in the level (Level::editIndexOf): the level is made again at Stop. */
Editor::KeptEntity keptEntityOf(const Entity& rEntity_, const Level& rLevel_)
{
	Editor::KeptEntity kept;
	kept.position = rEntity_.position;
	kept.rotation = rEntity_.rotation;
	kept.scale = rEntity_.scale;
	if(rEntity_.behaviorSetId != UninitializedPoolId) kept.behaviors = Globals::Game::entityBehaviors[rEntity_.behaviorSetId].behaviorIds;
	kept.shapes = entityShapes(rEntity_.selfId);
	kept.texture = rEntity_.textureChoice;
	kept.mesh = rEntity_.meshChoice;
	kept.externals = rEntity_.externals;
	kept.externalsKept = rEntity_.externalsKept;
	for(auto& [behaviorName, rValues] : kept.externals)
	{
		for(auto& [fieldName, rValue] : rValues)
		{
			if(rValue.entity == UninitializedPoolId) continue;
			rValue.entityIndex = rLevel_.editIndexOf(rValue.entity);
			rValue.entity = UninitializedPoolId;
		}
	}
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
	rEntity.externals = rKept_.externals;
	rEntity.externalsKept = rKept_.externalsKept;
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

namespace
{
constexpr size_t MaxUndoActions = 200;

Editor::Transform transformOf(const Entity& rEntity_)
{
	return {rEntity_.position, rEntity_.rotation, rEntity_.scale};
}
}

void Editor::clearUndo()
{
	this->undoStack.clear();
	this->redoStack.clear();
	this->tracked.clear();
	this->trackedBusy = false;
	this->trackedPlaces.clear();
	this->trackedPlacesBusy = false;
}

void Editor::renameUndoEntity(PoolId old_, PoolId new_)
{
	for(std::vector<UndoAction>* pStack : {&this->undoStack, &this->redoStack})
	{
		for(UndoAction& rAction : *pStack)
		{
			if(rAction.entity == old_) rAction.entity = new_;
			for(EntityTransform& rMember : rAction.group)
			{
				if(rMember.entity == old_) rMember.entity = new_;
			}
		}
	}
}

/* A new step ends the redo ones. */
void Editor::pushUndo(const UndoAction& rAction_)
{
	this->undoStack.push_back(rAction_);
	if(this->undoStack.size() > MaxUndoActions) this->undoStack.erase(this->undoStack.begin());
	this->redoStack.clear();
}

void Editor::recordCreated(PoolId entityId_)
{
	this->pushUndo({true, entityId_, Transform(), Clipboard()});
	this->entityList.selectOnly(entityId_);
	this->entityList.selectedFolder = -1;
}

/*
A step is the selected entities' transforms before a change that the user made: the gizmo drag or an inspector widget being held (ImGui::IsAnyItemActive) is "busy", and the first idle frame after a busy one compares the transforms with the ones seen before it, so a whole drag is one step. Any other change
(the collision system pushing an entity out of an overlap in the editor) is taken as the new baseline without a step, otherwise undoing would be followed by a push-out that is recorded again and never gets past. One changed entity is a plain step; several (a group drag) are one group step.
*/
void Editor::trackTransform()
{
	/* In the Map view an entity moves with its level, not by the user: no entity step, the levels' places are tracked instead, and the entities' baseline is taken again when the Level view is back. */
	if(this->mapView)
	{
		this->tracked.clear();
		this->trackedBusy = false;
		this->trackPlaces();
		return;
	}
	this->trackedPlaces.clear();
	this->trackedPlacesBusy = false;

	std::vector<EntityTransform> current;
	for(PoolId id : this->entityList.selection())
	{
		if(entityExists(id)) current.push_back({id, transformOf(getEntity(id))});
	}
	bool same = !current.empty() && current.size() == this->tracked.size();
	for(size_t i = 0; same && i < current.size(); i++) same = current[i].entity == this->tracked[i].entity;
	if(!same)
	{
		this->tracked = current;
		this->trackedBusy = false;
		return;
	}
	if(this->gizmo.wantsMouse() || ImGui::IsAnyItemActive())
	{
		this->trackedBusy = true;
		return;
	}
	if(this->trackedBusy)
	{
		std::vector<EntityTransform> changed;
		for(size_t i = 0; i < current.size(); i++)
		{
			if(!(current[i].transform == this->tracked[i].transform)) changed.push_back(this->tracked[i]);
		}
		if(!changed.empty())
		{
			UndoAction action{false, changed[0].entity, changed[0].transform, Clipboard()};
			if(changed.size() > 1) action.group = changed;
			this->pushUndo(action);
		}
	}
	this->tracked = current;
	this->trackedBusy = false;
}

/* The Map view's counterpart of trackTransform: the places (position, rotation) of the selected levels; a step holds the places of the levels that changed. */
void Editor::trackPlaces()
{
	std::vector<MapLevel> current;
	for(const std::string& rName : this->mapSelection())
	{
		MapLevel place;
		if(this->levelPlace(rName, place)) current.push_back(place);
	}
	bool same = !current.empty() && current.size() == this->trackedPlaces.size();
	for(size_t i = 0; same && i < current.size(); i++) same = current[i].name == this->trackedPlaces[i].name;
	if(!same)
	{
		this->trackedPlaces = current;
		this->trackedPlacesBusy = false;
		return;
	}
	if(this->gizmo.wantsMouse() || ImGui::IsAnyItemActive())
	{
		this->trackedPlacesBusy = true;
		return;
	}
	if(this->trackedPlacesBusy)
	{
		UndoAction action{false, UninitializedPoolId, Transform(), Clipboard()};
		for(size_t i = 0; i < current.size(); i++)
		{
			if(current[i].position != this->trackedPlaces[i].position || current[i].rotation != this->trackedPlaces[i].rotation) action.places.push_back(this->trackedPlaces[i]);
		}
		if(!action.places.empty()) this->pushUndo(action);
	}
	this->trackedPlaces = current;
	this->trackedPlacesBusy = false;
}

/*
Does one step and puts its opposite on rOpposite_ (undo: the redo stack, redo: the undo stack). A transform step gives the entity the stored transform and stores the one it had; a group step does so for each entity of the group that still exists; a map step gives each level its stored place (by name)
and stores the one it had. A created step removes the entity from its level and the pool (Level::restoreSource does the same for added entities), keeping its state; the opposite
(a created step with a state) makes it again, and the entity has a new PoolId then, so the steps of both stacks that name the old one are changed to the new. The caller skips a step whose entity is gone.
*/
void Editor::applyAction(const UndoAction& rAction_, std::vector<UndoAction>& rOpposite_)
{
	PoolId levelId = this->currentLevel();
	if(!rAction_.places.empty())
	{
		UndoAction opposite{false, UninitializedPoolId, Transform(), Clipboard()};
		for(const MapLevel& rPlace : rAction_.places)
		{
			MapLevel now;
			if(!this->levelPlace(rPlace.name, now)) continue;
			opposite.places.push_back(now);
			this->setLevelPlace(rPlace);
		}
		this->trackedPlaces.clear();
		this->trackedPlacesBusy = false;
		if(!opposite.places.empty()) rOpposite_.push_back(opposite);
		return;
	}
	if(!rAction_.group.empty())
	{
		UndoAction opposite{false, UninitializedPoolId, Transform(), Clipboard()};
		this->entityList.selectOnly(UninitializedPoolId);
		for(const EntityTransform& rMember : rAction_.group)
		{
			if(!entityExists(rMember.entity)) continue;
			Entity& rEntity = getEntity(rMember.entity);
			opposite.group.push_back({rMember.entity, transformOf(rEntity)});
			rEntity.setPosition(rMember.transform.position);
			rEntity.setRotation(rMember.transform.rotation);
			rEntity.setScale(rMember.transform.scale);
			this->entityList.toggleSelected(rMember.entity);
		}
		if(opposite.group.empty()) return;
		opposite.entity = opposite.group[0].entity;
		opposite.before = opposite.group[0].transform;
		rOpposite_.push_back(opposite);
		Globals::Game::spatialIndex.dirty = true;
		this->entityList.selectedFolder = -1;
		this->tracked.clear();
		this->trackedBusy = false;
		return;
	}
	if(rAction_.created && rAction_.snapshot.valid)
	{
		PoolId id = this->spawnFrom(rAction_.snapshot, rAction_.snapshot.entity.position);
		if(id == UninitializedPoolId) return;
		for(std::vector<UndoAction>* pStack : {&this->undoStack, &this->redoStack})
		{
			for(UndoAction& rOther : *pStack)
			{
				if(rOther.entity == rAction_.entity) rOther.entity = id;
				for(EntityTransform& rMember : rOther.group)
				{
					if(rMember.entity == rAction_.entity) rMember.entity = id;
				}
			}
		}
		rOpposite_.push_back({true, id, Transform(), Clipboard()});
		this->entityList.selectOnly(id);
		this->entityList.selectedFolder = -1;
		return;
	}
	if(rAction_.created)
	{
		Clipboard snapshot = this->snapshotOf(rAction_.entity);
		if(levelId != UninitializedPoolId) std::erase(getLevel(levelId).entityIds, rAction_.entity);
		if(this->entityList.selected == rAction_.entity) this->entityList.selectOnly(UninitializedPoolId);
		Globals::Game::entities.remove(rAction_.entity);
		Globals::Game::spatialIndex.dirty = true;
		rOpposite_.push_back({true, rAction_.entity, Transform(), snapshot});
		return;
	}
	Entity& rEntity = getEntity(rAction_.entity);
	rOpposite_.push_back({false, rAction_.entity, transformOf(rEntity), Clipboard()});
	rEntity.setPosition(rAction_.before.position);
	rEntity.setRotation(rAction_.before.rotation);
	rEntity.setScale(rAction_.before.scale);
	Globals::Game::spatialIndex.dirty = true;
	this->entityList.selectOnly(rAction_.entity);
	this->entityList.selectedFolder = -1;
	this->tracked.clear();
	this->trackedBusy = false;
}

namespace
{
/* Whether a step can still be done: a map step always (levels not loaded are skipped when it is applied), a group step while one of its entities exists, a created step on the redo stack while it has a state to make the entity from, any other while its entity exists. */
bool stepPossible(const Editor::UndoAction& rAction_, bool redo_)
{
	if(!rAction_.places.empty()) return true;
	if(!rAction_.group.empty())
	{
		for(const Editor::EntityTransform& rMember : rAction_.group)
		{
			if(entityExists(rMember.entity)) return true;
		}
		return false;
	}
	if(redo_ && rAction_.created) return rAction_.snapshot.valid;
	return entityExists(rAction_.entity);
}

const char* stepKind(const Editor::UndoAction& rAction_)
{
	if(!rAction_.places.empty()) return "a level place change";
	if(!rAction_.group.empty()) return "a group transform change";
	return rAction_.created ? "the creation of an entity" : "a transform change";
}
}

void Editor::undo()
{
	while(!this->undoStack.empty())
	{
		UndoAction action = this->undoStack.back();
		this->undoStack.pop_back();
		if(!stepPossible(action, false)) continue;
		this->applyAction(action, this->redoStack);
		this->fileMessage = std::string("Undid ") + stepKind(action) + ".";
		return;
	}
	this->fileMessage = "Nothing to undo.";
}

/* A created step on the redo stack names an entity that is gone on purpose (it is made again), so only the other kinds are checked for existence. */
void Editor::redo()
{
	while(!this->redoStack.empty())
	{
		UndoAction action = this->redoStack.back();
		this->redoStack.pop_back();
		if(!stepPossible(action, true)) continue;
		this->applyAction(action, this->undoStack);
		this->fileMessage = std::string("Redid ") + stepKind(action) + ".";
		return;
	}
	this->fileMessage = "Nothing to redo.";
}

/* A prefab entity keeps its prefab and Editor::KeptEntity (the state Stop keeps); a camera entity keeps the camera's own settings. A player or an entity of neither kind (no prefab, no camera) gives an invalid copy: a second player would not be a copy of the first; reason in fileMessage. */
Editor::Clipboard Editor::snapshotOf(PoolId entityId_)
{
	Clipboard copy;
	PoolId levelId = this->currentLevel();
	if(entityId_ == UninitializedPoolId || !entityExists(entityId_))
	{
		this->fileMessage = "Nothing copied: no entity selected.";
		return copy;
	}
	const Entity& rEntity = getEntity(entityId_);
	copy.name = Globals::Game::entities.names[entityId_.id];
	if(rEntity.cameraId != UninitializedPoolId)
	{
		const Camera& rCamera = Globals::Game::cameras[rEntity.cameraId];
		copy.isCamera = true;
		copy.entity.position = rCamera.position;
		const Graphics::RenderTarget* pTarget = rCamera.renderTarget();
		copy.camera = {rCamera.front, rCamera.yaw, rCamera.pitch, rCamera.fov, rCamera.nearPlane, rCamera.farPlane, rCamera.speed, rCamera.speedUp, rCamera.enabled, Globals::Game::mainCamera == rEntity.cameraId, pTarget != nullptr ? pTarget->width : 0u, pTarget != nullptr ? pTarget->height : 0u};
	}
	else if(rEntity.prefabId != UninitializedPoolId && levelId != UninitializedPoolId)
	{
		PrefabKind kind = Globals::Game::prefabs[rEntity.prefabId].kind;
		if(kind == PrefabKind::SonicPlayer || kind == PrefabKind::ShadowPlayer)
		{
			this->fileMessage = "Nothing copied: a player can't be copied.";
			return copy;
		}
		copy.prefabId = rEntity.prefabId;
		copy.entity = keptEntityOf(rEntity, getLevel(levelId));
	}
	else
	{
		this->fileMessage = "Nothing copied: the entity has no prefab and no camera.";
		return copy;
	}
	copy.valid = true;
	return copy;
}

void Editor::copySelected()
{
	Clipboard copy = this->snapshotOf(this->entityList.selected);
	if(!copy.valid) return;
	copy.camera.isMain = false;
	this->clipboard = std::move(copy);
	this->fileMessage = "Copied " + this->clipboard.name;
}

PoolId Editor::addLevelCamera(const char* name_, const glm::vec3& rPosition_, PoolId levelId_)
{
	PoolId id = addCamera(name_, rPosition_);
	PoolId levelId = levelId_ != UninitializedPoolId ? levelId_ : this->currentLevel();
	if(levelId != UninitializedPoolId)
	{
		getEntity(id).levelId = levelId;
		getLevel(levelId).entityIds.push_back(id);
	}
	return id;
}

/* Makes an entity from a copy at a position: a camera entity with the copy's settings, or an entity of its prefab that the level owns from then on (like an entity of the prefab panel), given the copy's transform, behaviours, shapes, model, texture and field values. UninitializedPoolId if it can't (the reason is on stderr or in fileMessage). */
PoolId Editor::spawnFrom(const Clipboard& rCopy_, const glm::vec3& rPosition_, PoolId levelId_)
{
	if(rCopy_.isCamera)
	{
		PoolId id = this->addLevelCamera(rCopy_.name.c_str(), rPosition_, levelId_);
		Camera& rCamera = Globals::Game::cameras[getEntity(id).cameraId];
		const CopiedCamera& rSettings = rCopy_.camera;
		rCamera.front = rSettings.front;
		rCamera.yaw = rSettings.yaw;
		rCamera.pitch = rSettings.pitch;
		rCamera.fov = rSettings.fov;
		rCamera.nearPlane = rSettings.nearPlane;
		rCamera.farPlane = rSettings.farPlane;
		rCamera.speed = rSettings.speed;
		rCamera.speedUp = rSettings.speedUp;
		rCamera.enabled = rSettings.enabled;
		if(rSettings.targetWidth > 0) rCamera.setRenderTarget(addRenderTarget(rSettings.targetWidth, rSettings.targetHeight));
		rCamera.refreshProjection();
		rCamera.set(rPosition_);
		if(rSettings.isMain) Globals::Game::mainCamera = getEntity(id).cameraId;
		return id;
	}
	PoolId levelId = levelId_ != UninitializedPoolId ? levelId_ : this->currentLevel();
	if(levelId == UninitializedPoolId) return UninitializedPoolId;
	const Prefab& rPrefab = Globals::Game::prefabs[rCopy_.prefabId];
	SpawnParams params;
	params.position = rPosition_;
	params.name = rCopy_.name;
	params.what = "(editor)";
	params.levelId = levelId;
	PoolId id = spawnPrefab(rCopy_.prefabId, Graphics::getShaderId(rPrefab.game == SA_NINJA ? "SA" : "STH2005"), params);
	if(id == UninitializedPoolId)
	{
		this->fileMessage = "Could not make " + rCopy_.name + ", see the log.";
		return id;
	}
	Level& rLevel = getLevel(levelId);
	rLevel.entityIds.push_back(id);
	KeptEntity kept = rCopy_.entity;
	kept.position = rPosition_;
	giveKeptEntity(id, kept);
	rLevel.resolveExternalEntities();
	return id;
}

/* The copy goes where the cursor's ray crosses the horizontal plane through the copied entity (there is no ray against the level's collision yet); with the cursor on the UI, or a ray that never reaches the plane, it goes where the copied entity was. */
void Editor::pasteAtCursor()
{
	if(!this->clipboard.valid)
	{
		this->fileMessage = "Nothing to paste: copy an entity first (Ctrl+C).";
		return;
	}
	GLFWwindow* pWindow = Globals::App::window;
	glm::vec3 position = this->clipboard.entity.position;
	double cursorX = 0.0;
	double cursorY = 0.0;
	glfwGetCursorPos(pWindow, &cursorX, &cursorY);
	glm::vec3 point;
	if(!this->debugUI.wantsMouse() && Graphics::pickPlanePoint(pWindow, this->camera, cursorX, cursorY, position.y, point)) position = point;
	PoolId id = this->spawnFrom(this->clipboard, position);
	if(id == UninitializedPoolId) return;
	this->recordCreated(id);
	this->fileMessage = "Pasted " + this->clipboard.name;
}

void Editor::loadLevel(const std::string& levelName_)
{
	this->entityList.selectOnly(UninitializedPoolId);
	this->clearKept();
	this->clearUndo();
	Scripting::resetGame(levelName_.c_str());
	Globals::Game::spatialIndex.rebuild();
	this->matchGameplayCamera();
	this->fileMessage = "Loaded " + levelName_ + " (a failure is on stderr).";
}

bool Editor::levelPlace(const std::string& name_, MapLevel& rPlace_) const
{
	const std::string& rName = name_.empty() ? Globals::Game::map.active : name_;
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(!Globals::Game::levels.alive[i] || Globals::Game::levels.objects[i].name != rName) continue;
		rPlace_.name = rName;
		rPlace_.position = Globals::Game::levels.objects[i].mapPosition;
		rPlace_.rotation = Globals::Game::levels.objects[i].mapRotation;
		return true;
	}
	return false;
}

std::vector<std::string> Editor::mapSelection() const
{
	std::vector<std::string> names;
	names.push_back(this->mapSelected.empty() ? Globals::Game::map.active : this->mapSelected);
	for(const std::string& rName : this->mapExtraSelected)
	{
		if(std::find(names.begin(), names.end(), rName) == names.end()) names.push_back(rName);
	}
	std::erase_if(names, [this](const std::string& rName_) {
		MapLevel place;
		return !this->levelPlace(rName_, place);
	});
	return names;
}

bool Editor::mapIsSelected(const std::string& name_) const
{
	std::vector<std::string> names = this->mapSelection();
	return std::find(names.begin(), names.end(), name_) != names.end();
}

void Editor::selectMapLevel(const std::string& name_, bool additive_)
{
	if(!additive_)
	{
		this->mapSelected = name_;
		this->mapExtraSelected.clear();
		return;
	}
	std::vector<std::string> names = this->mapSelection();
	auto found = std::find(names.begin(), names.end(), name_);
	if(found != names.end()) names.erase(found);
	else names.insert(names.begin(), name_);
	this->mapSelected = names.empty() ? std::string() : names.front();
	this->mapExtraSelected.assign(names.empty() ? names.end() : names.begin() + 1, names.end());
}

void Editor::setLevelPlace(const MapLevel& rPlace_)
{
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(!Globals::Game::levels.alive[i] || Globals::Game::levels.objects[i].name != rPlace_.name) continue;
		Globals::Game::levels.objects[i].setMapTransform(rPlace_.position, rPlace_.rotation, false);
		Globals::Game::spatialIndex.dirty = true;
		Globals::Game::map.set(rPlace_);
		this->mapDirty = true;
		return;
	}
}

void Editor::chooseActive(const std::string& name_)
{
	this->loadLevel(name_);
	this->mapSelected = name_;
	this->mapExtraSelected.clear();
	this->mapView = false;
	this->entityList.focusLevelTab = true;
}

void Editor::addLevelToMap(const std::string& name_)
{
	MapLevel place;
	place.name = name_;
	if(!this->levelPlace(name_, place))
	{
		Globals::Game::map.set(place);
		try
		{
			PoolId id = Loading::loadLevel(name_);
			Globals::Game::map.loaded.push_back(id);
			Globals::Game::spatialIndex.rebuild();
		}
		catch(const std::exception& rError)
		{
			this->fileMessage = "Add level to map: " + name_ + " not loaded: " + rError.what();
			return;
		}
	}
	else Globals::Game::map.set(place);
	this->mapSelected = name_;
	this->mapExtraSelected.clear();
	this->mapDirty = true;
	this->fileMessage = "Added " + name_ + " to the map.";
}

void Editor::removeLevelFromMap(const std::string& name_)
{
	MapLevel place;
	if(this->levelPlace(name_, place))
	{
		if(name_ == Globals::Game::map.active)
		{
			place.position = glm::vec3(0.0f);
			place.rotation = glm::vec3(0.0f);
			this->setLevelPlace(place);
		}
		else
		{
			for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
			{
				if(!Globals::Game::levels.alive[i] || Globals::Game::levels.objects[i].name != name_) continue;
				PoolId id(i, Globals::Game::levels.generation[i]);
				std::erase(Globals::Game::map.loaded, id);
				Globals::Game::levels.remove(id);
				break;
			}
			Globals::Game::spatialIndex.rebuild();
		}
	}
	Globals::Game::map.remove(name_);
	this->entityList.selectOnly(UninitializedPoolId);
	this->mapSelected.clear();
	this->mapExtraSelected.clear();

	/* A map step of Ctrl+Z would give the removed entry back (Map::set), so the map steps go. */
	for(std::vector<UndoAction>* pStack : {&this->undoStack, &this->redoStack})
	{
		std::erase_if(*pStack, [](const UndoAction& rAction_) { return !rAction_.places.empty(); });
	}
	this->trackedPlaces.clear();
	this->mapDirty = true;
	this->fileMessage = "Removed " + name_ + " from the map.";
}

bool Editor::hasKeptLevel(const std::string& rName_) const
{
	for(const KeptLevel& rKept : this->keptLevels)
	{
		if(rKept.name == rName_) return true;
	}
	return false;
}

void Editor::restoreKeptLevel(PoolId levelId_)
{
	const std::string& rName = getLevel(levelId_).name;
	for(const KeptLevel& rKept : this->keptLevels)
	{
		if(rKept.name != rName) continue;
		getLevel(levelId_).restoreSource();
		this->giveKeptLevel(levelId_, rKept);
		return;
	}
}

void Editor::restoreKeptEntities(PoolId levelId_, const std::vector<uint32_t>& rRemadeIndices_)
{
	Level& rLevel = getLevel(levelId_);
	for(const KeptLevel& rKept : this->keptLevels)
	{
		if(rKept.name != rLevel.name) continue;
		for(const KeptOwn& rOwn : rKept.own)
		{
			if(std::find(rRemadeIndices_.begin(), rRemadeIndices_.end(), rOwn.index) == rRemadeIndices_.end()) continue;
			if(rOwn.index >= rLevel.entityIds.size() || !entityExists(rLevel.entityIds[rOwn.index])) continue;
			giveKeptEntity(rLevel.entityIds[rOwn.index], rOwn.entity);
			this->renameUndoEntity(rOwn.oldId, rLevel.entityIds[rOwn.index]);
		}
		for(const KeptAdded& rAdded : rKept.added)
		{
			if(rAdded.index < rLevel.builtCount) continue;
			SpawnParams params;
			params.position = rAdded.entity.position;
			params.what = "(editor)";
			params.levelId = levelId_;
			PoolId id = spawnPrefab(rAdded.prefabId, Graphics::getShaderId(Globals::Game::prefabs[rAdded.prefabId].game == SA_NINJA ? "SA" : "STH2005"), params);
			if(id == UninitializedPoolId) continue;
			rLevel.entityIds.push_back(id);
			giveKeptEntity(id, rAdded.entity);
			this->renameUndoEntity(rAdded.oldId, id);

			/* A script may hold the handle the entity had at Play (a global): it follows the entity to this one. */
			if(Scripting::ready()) Scripting::entityRemade(rAdded.oldId, id);
		}
		for(size_t i = 0; i < rKept.cameras.size(); i++)
		{
			if(rKept.cameraIndices[i] < rLevel.builtCount || !rKept.cameras[i].valid) continue;
			this->spawnFrom(rKept.cameras[i], rKept.cameras[i].entity.position, levelId_);
		}
		return;
	}
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
		const Graphics::ExternalPools& rPools = inLevel_ ? Graphics::externalLevel() : Graphics::externalProject;
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

void Editor::saveSourceLevel()
{
	PoolId levelId = this->currentLevel();
	if(levelId == UninitializedPoolId)
	{
		this->fileMessage = "Nothing saved: no level loaded.";
		return;
	}
	std::string levelName = Globals::Game::levels.names[levelId.id];
	try
	{
		Loading::saveSourceLevel(levelId);
	}
	catch(const std::exception& rError)
	{
		this->fileMessage = std::string("Save Source Game Level failed: ") + rError.what();
		return;
	}
	this->loadLevel(levelName);
	this->fileMessage = "Saved " + levelName + " as its Source Game Level and loaded it again (a load failure is on stderr).";
}

void Editor::restoreSourceValues()
{
	PoolId levelId = this->currentLevel();
	if(levelId == UninitializedPoolId) return;
	this->entityList.selectOnly(UninitializedPoolId);
	this->clearUndo();
	getLevel(levelId).restoreSource();
	Globals::Game::spatialIndex.rebuild();
	/* The prefabs' level edits are gone (restoreSource); the Source Game Level's own prefab edits are the level's source set (Prefab::inLevels), which stays. */
	this->fileMessage = "Restored the Source Game Level, prefab behaviour, bound area, model and texture edits included (not saved until File > Save).";
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

void Editor::giveKeptLevel(PoolId levelId_, const KeptLevel& rKept_)
{
	Level& rLevel = getLevel(levelId_);

	/* The prefabs' edits first (new entities get them at spawn), then each entity's exact list. */
	for(const KeptPrefab& rPrefab : rKept_.prefabs)
	{
		for(PoolId behaviorId : rPrefab.added) addPrefabBehavior(rPrefab.prefabId, behaviorId, PrefabScope::Level, levelId_);
		for(PoolId behaviorId : rPrefab.removed) removePrefabBehavior(rPrefab.prefabId, behaviorId, PrefabScope::Level, levelId_);
		for(const Physics::CollisionShape& rShape : rPrefab.addedShapes) addPrefabShape(rPrefab.prefabId, rShape, PrefabScope::Level, levelId_);
		for(const Physics::CollisionShape& rShape : rPrefab.removedShapes) removePrefabShape(rPrefab.prefabId, rShape, PrefabScope::Level, levelId_);
		if(rPrefab.mesh.kind != MeshChoiceKind::Default) setPrefabMesh(rPrefab.prefabId, rPrefab.mesh, PrefabScope::Level, levelId_);
		if(rPrefab.texture.kind != TextureChoiceKind::Default) setPrefabTexture(rPrefab.prefabId, rPrefab.texture, PrefabScope::Level, levelId_);
	}

	for(const KeptOwn& rOwn : rKept_.own)
	{
		if(rOwn.index >= rLevel.sourceEntityCount || !entityExists(rLevel.entityIds[rOwn.index])) continue;
		giveKeptEntity(rLevel.entityIds[rOwn.index], rOwn.entity);
		this->renameUndoEntity(rOwn.oldId, rLevel.entityIds[rOwn.index]);
	}
	for(const KeptAdded& rAdded : rKept_.added)
	{
		SpawnParams params;
		params.position = rAdded.entity.position;
		params.what = "(editor)";
		params.levelId = levelId_;
		PoolId id = spawnPrefab(rAdded.prefabId, Graphics::getShaderId(Globals::Game::prefabs[rAdded.prefabId].game == SA_NINJA ? "SA" : "STH2005"), params);
		if(id == UninitializedPoolId) continue;
		rLevel.entityIds.push_back(id);
		giveKeptEntity(id, rAdded.entity);
		this->renameUndoEntity(rAdded.oldId, id);
		if(Scripting::ready()) Scripting::entityRemade(rAdded.oldId, id);
	}
	for(const Clipboard& rCamera : rKept_.cameras)
	{
		if(rCamera.valid) this->spawnFrom(rCamera, rCamera.entity.position, levelId_);
	}
	rLevel.resolveExternalEntities();
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
		for(uint32_t levelSlot = 0; levelSlot < Globals::Game::levels.objects.size(); levelSlot++)
		{
			if(!Globals::Game::levels.alive[levelSlot]) continue;
			PoolId levelId(levelSlot, Globals::Game::levels.generation[levelSlot]);
			const Level& rLevel = getLevel(levelId);
			KeptLevel kept;
			kept.name = Globals::Game::levels.names[levelSlot];
			for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
			{
				if(!Globals::Game::prefabs.alive[i]) continue;
				const PrefabEdits& rEdits = prefabLevelEdits(Globals::Game::prefabs[PoolId(i, Globals::Game::prefabs.generation[i])], levelId);
				if(rEdits.addedBehaviors.empty() && rEdits.removedBehaviors.empty() && rEdits.addedShapes.empty() && rEdits.removedShapes.empty() && rEdits.texture.kind == TextureChoiceKind::Default && rEdits.mesh.kind == MeshChoiceKind::Default) continue;
				kept.prefabs.push_back({PoolId(i, Globals::Game::prefabs.generation[i]), rEdits.addedBehaviors, rEdits.removedBehaviors, rEdits.addedShapes, rEdits.removedShapes, rEdits.texture, rEdits.mesh});
			}
			for(uint32_t i = 0; i < rLevel.entityIds.size(); i++)
			{
				if(!entityExists(rLevel.entityIds[i])) continue;
				const Entity& rEntity = getEntity(rLevel.entityIds[i]);
				if(i < rLevel.sourceEntityCount) kept.own.push_back({i, keptEntityOf(rEntity, rLevel), rLevel.entityIds[i]});
				else if(rEntity.prefabId != UninitializedPoolId) kept.added.push_back({rEntity.prefabId, keptEntityOf(rEntity, rLevel), rLevel.entityIds[i], i});
				else if(rEntity.cameraId != UninitializedPoolId)
				{
					kept.cameras.push_back(this->snapshotOf(rLevel.entityIds[i]));
					kept.cameraIndices.push_back(i);
				}
			}

			/* The state the editor leaves is what a reset in place keeps: the entities it edited are not "changed" against the file's build, only against what play does to them (Loading::resetLevel). */
			Level& rMutableLevel = getLevel(levelId);
			for(uint32_t i = 0; i < rMutableLevel.builtCount && i < rMutableLevel.entityIds.size(); i++)
			{
				if(entityExists(rMutableLevel.entityIds[i]) && rMutableLevel.built.contains(Level::builtKey(rMutableLevel.entityIds[i]))) rMutableLevel.captureBaseline(rMutableLevel.entityIds[i]);
			}
			this->keptLevels.push_back(std::move(kept));
		}
	}
	this->tracked.clear();
	this->trackedBusy = false;
	this->active = !playing_;
	this->debugMenuOpen = false;
	this->camera.dragging = false;
	this->cursorFree = this->active;
	glfwSetInputMode(pWindow, GLFW_CURSOR, this->active ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
	Globals::Mouse::firstMouse = true;
	if(this->active)
	{
		/*
		The game goes back to its start: the scripts' level is made again from its data (the scripts' resetGame, which loads the map's levels beside it), and so is every other level that was alive at Play and is not one of those (loaded by name, whether play left it or not), so what the play moved, what the editor added or moved before it is put back (kept at Play).
		Entities of the old levels are gone: nothing stays selected, and each level load makes its markers again.
		*/
		this->entityList.selectOnly(UninitializedPoolId);
		Scripting::resetGame();
		for(const KeptLevel& rKept : this->keptLevels)
		{
			/* The reset loaded the scripts' level and the map's others; a level of that name that is alive is used, one that is not (loaded by a script or the editor, outside the map) is made again and left to the map's next load to remove. */
			PoolId levelId = UninitializedPoolId;
			for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
			{
				if(Globals::Game::levels.alive[i] && Globals::Game::levels.names[i] == rKept.name) levelId = PoolId(i, Globals::Game::levels.generation[i]);
			}
			if(levelId == UninitializedPoolId)
			{
				try
				{
					levelId = Loading::loadLevel(rKept.name);
					Globals::Game::map.loaded.push_back(levelId);
				}
				catch(const std::exception& rError)
				{
					fprintf(stderr, "Stop: level %s not made again: %s\n", rKept.name.c_str(), rError.what());
					continue;
				}
			}

			/* The reloaded level has the file's edits; the editor's own state replaces them. */
			getLevel(levelId).restoreSource();
			this->giveKeptLevel(levelId, rKept);
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
		/*
		The camera's speed follows the distance to the selected entity (EditorCamera::updateSpeedScale): from the camera to the surface of its bounds sphere, so it does not depend on where the camera looks.
		Without a selection it follows the model the camera looks at: the nearest triangle on the view ray (Entity::rayHitsModel), found ten times a second; looking at nothing within the render distance gives the fastest.
		A distance to the nearest entity sphere is not used: the camera is inside the sphere of a level model, so it would always read 0.
		*/
		PoolId selected = this->entityList.selected;
		if(selected != UninitializedPoolId && entityExists(selected))
		{
			const Entity& rSelected = getEntity(selected);
			glm::vec3 center = rSelected.renderPosition();
			if(rSelected.bounds.valid) center += glm::mat3(rSelected.rotation) * rSelected.bounds.center;
			this->speedProbeDistance = std::max(glm::distance(center, rCamera.position) - (rSelected.bounds.valid ? rSelected.bounds.radius : 0.0f), 0.0f);
		}
		else
		{
			this->speedProbeTimer -= Globals::App::deltaTime;
			if(this->speedProbeTimer <= 0.0f)
			{
				this->speedProbeTimer = 0.1f;
				this->speedProbeDistance = rCamera.farPlane;
				for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
				{
					if(!Globals::Game::entities.alive[i]) continue;
					float distance = 0.0f;
					bool hasTriangles = false;
					if(Globals::Game::entities.objects[i].rayHitsModel(rCamera.position, rCamera.front, distance, hasTriangles)) this->speedProbeDistance = std::min(this->speedProbeDistance, distance);
				}
			}
		}
		rCamera.updateSpeedScale(this->speedProbeDistance, Globals::App::deltaTime);
		bool keyboardFree = !this->debugUI.wantsKeyboard();
		bool ctrlDown = glfwGetKey(pWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(pWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
		bool shortcutKeyDown = glfwGetKey(pWindow, GLFW_KEY_Z) == GLFW_PRESS || glfwGetKey(pWindow, GLFW_KEY_Y) == GLFW_PRESS || glfwGetKey(pWindow, GLFW_KEY_C) == GLFW_PRESS || glfwGetKey(pWindow, GLFW_KEY_V) == GLFW_PRESS || glfwGetKey(pWindow, GLFW_KEY_S) == GLFW_PRESS;
		if(keyboardFree && !(ctrlDown && shortcutKeyDown)) rCamera.processKeys(pWindow, Globals::App::deltaTime);

		/*
		Ctrl+Z undo, Ctrl+Y redo, Ctrl+C copy, Ctrl+V paste at the cursor, Ctrl+S save the level (the File menu's Save): each once per press. They need the keyboard free (no text field) and the editor camera not in a button hold. Ctrl alone still flies down; with one of these keys it flies nothing (S would fly back).
		*/
		bool zDown = glfwGetKey(pWindow, GLFW_KEY_Z) == GLFW_PRESS;
		bool yDown = glfwGetKey(pWindow, GLFW_KEY_Y) == GLFW_PRESS;
		bool sDown = glfwGetKey(pWindow, GLFW_KEY_S) == GLFW_PRESS;
		bool cKey = glfwGetKey(pWindow, GLFW_KEY_C) == GLFW_PRESS;
		bool vKey = glfwGetKey(pWindow, GLFW_KEY_V) == GLFW_PRESS;
		if(keyboardFree && ctrlDown && !rCamera.dragging)
		{
			if(zDown && !this->zPressed) this->undo();
			if(yDown && !this->yPressed) this->redo();
			if(cKey && !this->cPressed) this->copySelected();
			if(vKey && !this->vPressed) this->pasteAtCursor();
			if(sDown && !this->sPressed)
			{
				this->saveAllLevels();
			}
		}
		this->zPressed = zDown;
		this->yPressed = yDown;
		this->sPressed = sDown;

		/*
		The gizmo takes a left press on one of its handles and keeps the mouse until the handle is released. 1 / 2 / 3 pick Move / Rotate / Scale (W is a fly key, so W E R is not used); not during a drag or a camera hold, nor while a text field has the keyboard.
		*/
		if(this->mapView)
		{
			std::vector<MapLevel> places;
			std::vector<glm::vec3> positions;
			std::vector<glm::vec3> rotations;
			for(const std::string& rName : this->mapSelection())
			{
				MapLevel place;
				if(!this->levelPlace(rName, place)) continue;
				places.push_back(place);
				positions.push_back(place.position);
				rotations.push_back(place.rotation);
			}
			if(this->gizmo.updateLevels(pWindow, rCamera, positions, rotations, this->debugUI.wantsMouse(), rCamera.dragging))
			{
				for(size_t i = 0; i < places.size(); i++)
				{
					places[i].position = positions[i];
					places[i].rotation = rotations[i];
					this->setLevelPlace(places[i]);
				}
			}
		}
		else this->gizmo.update(pWindow, rCamera, this->entityList.selection(), this->debugUI.wantsMouse(), rCamera.dragging);
		for(int i = 0; i < 3; i++)
		{
			bool keyDown = glfwGetKey(pWindow, GLFW_KEY_1 + i) == GLFW_PRESS;
			if(keyDown && !this->gizmoKeyPressed[i] && !this->gizmo.wantsMouse() && !rCamera.dragging && !this->debugUI.wantsKeyboard()) this->gizmo.mode = static_cast<Graphics::Gizmo::Mode>(i);
			this->gizmoKeyPressed[i] = keyDown;
		}

		/*
		Click to select: a left press on the scene (not on the UI, a gizmo handle or during a camera hold) followed by a release selects the entity under the press (Graphics::pickEntity); a miss keeps the selection. The ray uses the press position.
		With Shift held the entity (in the Map view, the level) is added to the selection or, if it is selected, removed from it. Shift is used, not Ctrl: Ctrl flies the camera down.
		*/
		bool clickDown = glfwGetMouseButton(pWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		if(clickDown && !this->clickLeftWas && !rCamera.dragging && !this->debugUI.wantsMouse() && !this->gizmo.wantsMouse())
		{
			this->clickPending = true;
			glfwGetCursorPos(pWindow, &this->clickX, &this->clickY);
		}
		if(rCamera.dragging) this->clickPending = false;
		if(!clickDown && this->clickLeftWas && this->clickPending)
		{
			this->clickPending = false;
			bool additive = glfwGetKey(pWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(pWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
			PoolId picked = Graphics::pickEntity(pWindow, rCamera, this->clickX, this->clickY);
			if(this->mapView)
			{
				/* In the Map view a click selects the level of the entity under it (an entity with no level counts as the active one's), else the level whose bounds the click enters (a level with no entities). */
				PoolId levelId = UninitializedPoolId;
				if(picked != UninitializedPoolId)
				{
					levelId = getEntity(picked).levelId;
					if(levelId == UninitializedPoolId || !Globals::Game::levels.alive[levelId.id]) levelId = activeLevelId();
				}
				else levelId = Graphics::pickLevel(pWindow, rCamera, this->clickX, this->clickY);
				if(levelId != UninitializedPoolId) this->selectMapLevel(Globals::Game::levels.objects[levelId.id].name, additive);
			}
			else if(picked != UninitializedPoolId)
			{
				if(additive) this->entityList.toggleSelected(picked);
				else this->entityList.selectOnly(picked);
				this->entityList.selectedFolder = -1;
			}
		}
		this->clickLeftWas = clickDown;
		this->trackTransform();

		/* A map change is finished when the handle or the inspector widget is let go: the collision is put right then, but Map.json is only written by Save (saveAllLevels); mapUnsaved says it is behind. */
		if(this->mapDirty && !this->gizmo.wantsMouse() && !ImGui::IsAnyItemActive())
		{
			this->mapDirty = false;
			this->mapUnsaved = true;
			for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
			{
				if(Globals::Game::levels.alive[i]) Globals::Game::levels.objects[i].commitMapCollision();
			}
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

	/* C toggles the collision lines view (Graphics::CollisionView); with Ctrl it is Ctrl+C (above), and so for V. */
	bool ctrlHeld = glfwGetKey(pWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(pWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
	bool cDown = glfwGetKey(pWindow, GLFW_KEY_C) == GLFW_PRESS;
	if(cDown && !this->cPressed && !ctrlHeld) this->collisionView.enabled = !this->collisionView.enabled;
	this->cPressed = cDown;

	/* V switches the lines view between coloring by triangle attribute and by slope. */
	bool vDown = glfwGetKey(pWindow, GLFW_KEY_V) == GLFW_PRESS;
	if(vDown && !this->vPressed && !ctrlHeld) this->collisionView.colorByAttribute = !this->collisionView.colorByAttribute;
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
