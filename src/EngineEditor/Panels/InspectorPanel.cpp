#include "InspectorPanel.h"
#include "Globals.h"
#include "API.h"
#include "Editor.h"
#include "Level.h"
#include "Prefab.h"
#include "GPU_Globals.h"
#include "imgui.h"
#include <algorithm>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

namespace Graphics
{
void InspectorPanel::draw(PoolId selected_)
{
	ImVec2 display = ImGui::GetIO().DisplaySize;
	float top = ImGui::GetFrameHeight();
	ImGui::SetNextWindowPos(ImVec2(display.x - this->width, top));
	ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, display.y - top), ImVec2(display.x * 0.5f, display.y - top));
	ImGui::SetNextWindowSize(ImVec2(this->width, display.y - top));
	ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
	this->width = ImGui::GetWindowSize().x;

	if(selected_ == UninitializedPoolId || !entityExists(selected_))
	{
		ImGui::TextDisabled("Select an entity in the list.");
		ImGui::End();
		return;
	}

	Entity& rEntity = getEntity(selected_);
	ImGui::Text("%s", Globals::Game::entities.names[selected_.id].c_str());
	ImGui::Separator();
	if(ImGui::BeginTabBar("InspectorTabs"))
	{
		if(ImGui::BeginTabItem("Transform"))
		{
			this->drawTransform(selected_);
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("BoundArea"))
		{
			this->drawBoundArea(selected_);
			ImGui::EndTabItem();
		}
		if(rEntity.physicsId != UninitializedPoolId && ImGui::BeginTabItem("Physics"))
		{
			this->drawPhysics(selected_);
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Behaviour"))
		{
			this->drawBehaviour(selected_);
			ImGui::EndTabItem();
		}
		if(rEntity.instanceId != UninitializedPoolId && ImGui::BeginTabItem("Mesh"))
		{
			this->drawMesh(selected_);
			ImGui::EndTabItem();
		}
		if(rEntity.instanceId != UninitializedPoolId && ImGui::BeginTabItem("Texture"))
		{
			this->drawTexture(selected_);
			ImGui::EndTabItem();
		}
		if(rEntity.cameraId != UninitializedPoolId && ImGui::BeginTabItem("Cameras"))
		{
			this->drawCamera(selected_);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

/* The position is set with Entity::setPosition (a teleport, which also refreshes what depends on it). */
void InspectorPanel::drawTransform(PoolId entityId_)
{
	Entity& rEntity = getEntity(entityId_);
	glm::vec3 position = rEntity.position;
	if(ImGui::DragFloat3("Position", &position.x, 0.5f)) rEntity.setPosition(position);
	glm::vec3 rendered = rEntity.renderPosition();
	ImGui::Text("Drawn at: %.2f, %.2f, %.2f", rendered.x, rendered.y, rendered.z);
	ImGui::Text("Active: %s%s", rEntity.active ? "yes" : "no", rEntity.behaviorNotCulled ? " (never put to sleep)" : "");
	ImGui::Text("Model: %s", rEntity.instanceId != UninitializedPoolId ? "yes" : "none");
	ImGui::Text("Bounds: radius %.1f%s", rEntity.bounds.radius, rEntity.bounds.valid ? "" : " (none)");

	/* An entity of the level's own (made from the game's data) can go back to where the data has it; one that was added has no such place. */
	PoolId levelId = editor.currentLevel();
	if(levelId != UninitializedPoolId)
	{
		Level& rLevel = getLevel(levelId);
		int32_t sourceIndex = rLevel.sourceIndexOf(entityId_);
		if(sourceIndex >= 0)
		{
			ImGui::Separator();
			ImGui::Text("Source: %.2f, %.2f, %.2f", rLevel.sourcePositions[sourceIndex].x, rLevel.sourcePositions[sourceIndex].y, rLevel.sourcePositions[sourceIndex].z);
			ImGui::BeginDisabled(rEntity.position == rLevel.sourcePositions[sourceIndex]);
			if(ImGui::Button("Return to Source Game state")) rLevel.revertSourceEntity(static_cast<uint32_t>(sourceIndex));
			ImGui::EndDisabled();
		}
	}
	if(rEntity.cameraId != UninitializedPoolId) ImGui::TextWrapped("This entity carries a camera: its position is not kept in step with the camera's (Cameras tab).");
}

/* One line for a prefab-wide shape in the list: form, size and centre. */
static std::string shapeSummary(const Physics::CollisionShape& rShape_)
{
	return std::format("form {}  a {:.1f} b {:.1f} c {:.1f}  at {:.1f}, {:.1f}, {:.1f}", rShape_.form, rShape_.a, rShape_.b, rShape_.c, rShape_.center.x, rShape_.center.y, rShape_.center.z);
}

/* A new shape for the Add buttons: a sphere (form 0) of radius 5 at the entity's origin, no collision behaviour (counts as a Body); the rest is edited in the shape's fields. */
static Physics::CollisionShape defaultShape()
{
	Physics::CollisionShape shape{};
	shape.a = 5.0f;
	shape.behavior = UninitializedPoolId;
	return shape;
}

/*
The bound area in two boxes. "This entity": the shapes of its collision, each editable in place (the raw fields of Physics::CollisionShape, the collision behaviour by combo; what the values mean is only what CollisionShape.h says), with Remove (this entity),
Remove from all and Add to all (a copy, for every entity of the prefab) and an Add shape button. For an entity made from a prefab, "Every entity of <prefab>": the shapes the editor added to or took from all of them, which new entities get too, with Undo.
An entity without a collision gets a hand-made one by its first shape. Shapes from the game's tables are read as the game has them; the shape's tags and interactions are those of its behaviour.
*/
void InspectorPanel::drawBoundArea(PoolId entityId_)
{
	Entity& rEntity = getEntity(entityId_);
	PoolId prefabId = rEntity.prefabId;
	std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);

	if(rEntity.collisionId != UninitializedPoolId)
	{
		const Physics::Collision& rCollision = Globals::Physics::collisions[rEntity.collisionId];
		ImGui::Text("Source: %s", rCollision.name.empty() ? "(made by hand)" : rCollision.name.c_str());
		ImGui::Text("Shapes: %u", static_cast<uint32_t>(rCollision.shapes.size()));
		ImGui::Text("Bounds: %.1f, %.1f, %.1f to %.1f, %.1f, %.1f", rCollision.boundsMin.x, rCollision.boundsMin.y, rCollision.boundsMin.z, rCollision.boundsMax.x, rCollision.boundsMax.y, rCollision.boundsMax.z);
	}
	ImGui::TextUnformatted("This entity:");
	ImGui::BeginChild("EntityShapes", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
	if(shapes.empty()) ImGui::TextDisabled("none");
	int removeIndex = -1;
	int removeAllIndex = -1;
	int addAllIndex = -1;
	bool changed = false;
	for(uint32_t i = 0; i < shapes.size(); i++)
	{
		Physics::CollisionShape& rShape = shapes[i];
		ImGui::PushID(static_cast<int>(i));
		if(ImGui::TreeNode("Shape", "Shape %u: form %u, attr 0x%08X", i, rShape.form, rShape.attr))
		{
			changed |= ImGui::InputScalar("form", ImGuiDataType_U8, &rShape.form);
			changed |= ImGui::DragFloat3("center", &rShape.center.x, 0.1f);
			changed |= ImGui::DragFloat("a", &rShape.a, 0.1f);
			changed |= ImGui::DragFloat("b", &rShape.b, 0.1f);
			changed |= ImGui::DragFloat("c", &rShape.c, 0.1f);
			changed |= ImGui::DragFloat("d", &rShape.d, 0.1f);
			changed |= ImGui::DragInt3("angle", &rShape.angle.x, 16.0f);
			changed |= ImGui::InputScalar("kind", ImGuiDataType_U8, &rShape.kind);
			changed |= ImGui::InputScalar("push", ImGuiDataType_U8, &rShape.push);
			changed |= ImGui::InputScalar("damage", ImGuiDataType_U8, &rShape.damage);
			changed |= ImGui::InputScalar("attr", ImGuiDataType_U32, &rShape.attr, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
			const auto& rBehaviors = Globals::Game::behaviors;
			const char* current = rShape.behavior == UninitializedPoolId ? "(none: counts as a Body)" : rBehaviors.names[rShape.behavior.id].c_str();
			if(ImGui::BeginCombo("behaviour", current))
			{
				if(ImGui::Selectable("(none: counts as a Body)", rShape.behavior == UninitializedPoolId))
				{
					rShape.behavior = UninitializedPoolId;
					changed = true;
				}
				for(uint32_t b = 0; b < rBehaviors.objects.size(); b++)
				{
					if(!rBehaviors.alive[b]) continue;
					PoolId behaviorId(b, rBehaviors.generation[b]);
					if(ImGui::Selectable(rBehaviors.names[b].c_str(), rShape.behavior == behaviorId))
					{
						rShape.behavior = behaviorId;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			if(rShape.behavior != UninitializedPoolId)
			{
				const BABehavior& rBehavior = rBehaviors[rShape.behavior];
				for(PoolId tag : rBehavior.tags) ImGui::BulletText("tag %s", Globals::Game::baTags.names[tag.id].c_str());
				ImGui::Text("Collides with %u tags, interacts with %u", static_cast<uint32_t>(rBehavior.availableCollisions.size()), static_cast<uint32_t>(rBehavior.availableInteractions.size()));
			}
			if(ImGui::SmallButton("Remove")) removeIndex = static_cast<int>(i);
			if(prefabId != UninitializedPoolId)
			{
				ImGui::SameLine();
				if(ImGui::SmallButton("Remove from all")) removeAllIndex = static_cast<int>(i);
				ImGui::SameLine();
				if(ImGui::SmallButton("Add to all")) addAllIndex = static_cast<int>(i);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if(ImGui::SmallButton("Add shape"))
	{
		shapes.push_back(defaultShape());
		changed = true;
	}
	ImGui::EndChild();

	/* The buttons act after the loop: they change the list the rows walk. An edit made this frame goes in first, so the prefab-wide ones see the shape as shown; Add to all moves this entity's shape to the end first, so the copy every entity gets is not a second one here. */
	if(removeIndex >= 0) shapes.erase(shapes.begin() + removeIndex);
	if(addAllIndex >= 0)
	{
		Physics::CollisionShape copy = shapes[addAllIndex];
		shapes.erase(shapes.begin() + addAllIndex);
		setEntityShapes(entityId_, shapes);
		addPrefabShape(prefabId, copy);
	}
	else if(removeAllIndex >= 0)
	{
		if(changed) setEntityShapes(entityId_, shapes);
		removePrefabShape(prefabId, shapes[removeAllIndex]);
	}
	else if(changed || removeIndex >= 0)
	{
		setEntityShapes(entityId_, shapes);
	}

	if(prefabId != UninitializedPoolId)
	{
		ImGui::Separator();
		ImGui::Text("Every entity of %s:", Globals::Game::prefabs.names[prefabId.id].c_str());
		ImGui::BeginChild("PrefabShapes", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		std::vector<Physics::CollisionShape> added = Globals::Game::prefabs[prefabId].addedShapes;
		std::vector<Physics::CollisionShape> removed = Globals::Game::prefabs[prefabId].removedShapes;
		if(added.empty() && removed.empty()) ImGui::TextDisabled("nothing added or removed");
		ImGui::PushID("added");
		for(uint32_t i = 0; i < added.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::Text("+ %s", shapeSummary(added[i]).c_str());
			ImGui::SameLine();
			if(ImGui::SmallButton("Undo")) removePrefabShape(prefabId, added[i]);
			ImGui::PopID();
		}
		ImGui::PopID();
		ImGui::PushID("removed");
		for(uint32_t i = 0; i < removed.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::Text("- %s", shapeSummary(removed[i]).c_str());
			ImGui::SameLine();
			if(ImGui::SmallButton("Undo")) addPrefabShape(prefabId, removed[i]);
			ImGui::PopID();
		}
		ImGui::PopID();
		if(ImGui::SmallButton("Add shape to all")) addPrefabShape(prefabId, defaultShape());
		ImGui::EndChild();
	}
}

void InspectorPanel::drawPhysics(PoolId entityId_)
{
	Physics::PhysicsBody& rBody = Globals::Physics::bodies[getEntity(entityId_).physicsId];
	ImGui::Text("Gravity: %.1f, %.1f, %.1f", Globals::Physics::gravity.x, Globals::Physics::gravity.y, Globals::Physics::gravity.z);
	ImGui::DragFloat("Weight", &rBody.weight, 0.01f);
	ImGui::DragFloat3("Velocity", &rBody.velocity.x, 0.5f);
	const Entity& rEntity = getEntity(entityId_);
	ImGui::Text("Staging: %.2f, %.2f, %.2f", rEntity.stagingPosition.x, rEntity.stagingPosition.y, rEntity.stagingPosition.z);
	ImGui::Text("Grounded: %s, normal %.2f, %.2f, %.2f", rBody.grounded ? "yes" : "no", rBody.groundNormal.x, rBody.groundNormal.y, rBody.groundNormal.z);
	ImGui::Text("Touching wall: %s, normal %.2f, %.2f, %.2f", rBody.touchingWall ? "yes" : "no", rBody.wallNormal.x, rBody.wallNormal.y, rBody.wallNormal.z);
	ImGui::Text("Snap: distance %.2f, by ray %s, radius %.2f", rBody.snapDistance, rBody.snapByRay ? "yes" : "no", rBody.snapRadius);
	ImGui::Text("Moves by mean velocity: %s", rBody.moveByMeanVelocity ? "yes" : "no");
}

/* A combo of every registered behaviour (Globals::Game::entityBehaviorTypes); the one clicked this frame, UninitializedPoolId otherwise. */
PoolId InspectorPanel::pickBehaviour(const char* id_)
{
	PoolId picked = UninitializedPoolId;
	auto& rTypes = Globals::Game::entityBehaviorTypes;
	ImGui::SetNextItemWidth(-1.0f);
	if(ImGui::BeginCombo(id_, "Add behaviour..."))
	{
		for(uint32_t i = 0; i < rTypes.objects.size(); i++)
		{
			if(rTypes.alive[i] && ImGui::Selectable(rTypes.names[i].c_str())) picked = PoolId(i, rTypes.generation[i]);
		}
		ImGui::EndCombo();
	}
	return picked;
}

/* Gives the behaviour to the one entity, or to every entity of its prefab. */
void InspectorPanel::giveBehaviour(PoolId entityId_, bool forPrefab_, PoolId behaviorId_)
{
	if(forPrefab_) addPrefabBehavior(getEntity(entityId_).prefabId, behaviorId_);
	else giveEntityBehavior(entityId_, behaviorId_);
}

/*
Makes the box drawn just before a place a script from the Assets tab can be dropped on ("SCRIPT" payload: the file's path, or "BEHAVIOR": one behaviour's name, given at once). The behaviours that file registered (EntityBehavior::sourceFile) are given: the only one at once, several through the
"Pick behaviour" popup (drawBehaviour opens it), none with a message.
*/
void InspectorPanel::acceptScript(PoolId entityId_, bool forPrefab_)
{
	if(!ImGui::BeginDragDropTarget()) return;
	/* One behaviour dragged out of an opened script file ("BEHAVIOR" payload: its name) is given at once. */
	if(const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload("BEHAVIOR"))
	{
		auto& rTypes = Globals::Game::entityBehaviorTypes;
		auto found = rTypes.nameToId.find(static_cast<const char*>(pPayload->Data));
		if(found == rTypes.nameToId.end())
		{
			this->message = std::format("Behaviour {} no longer exists.", static_cast<const char*>(pPayload->Data));
		}
		else
		{
			this->message.clear();
			this->giveBehaviour(entityId_, forPrefab_, found->second);
		}
	}
	else if(const ImGuiPayload* pScript = ImGui::AcceptDragDropPayload("SCRIPT"))
	{
		std::string path(static_cast<const char*>(pScript->Data));
		this->candidates.clear();
		auto& rTypes = Globals::Game::entityBehaviorTypes;
		for(uint32_t i = 0; i < rTypes.objects.size(); i++)
		{
			if(rTypes.alive[i] && rTypes.objects[i].sourceFile == path) this->candidates.push_back(PoolId(i, rTypes.generation[i]));
		}
		if(this->candidates.empty())
		{
			this->message = std::format("{} has registered no behaviour (sf.behavior): it may be loaded by another script, or not be loaded at all.", path);
		}
		else if(this->candidates.size() == 1)
		{
			this->message.clear();
			this->giveBehaviour(entityId_, forPrefab_, this->candidates[0]);
		}
		else
		{
			this->message.clear();
			this->candidatesForPrefab = forPrefab_;
			this->openPick = true;
		}
	}
	ImGui::EndDragDropTarget();
}

/*
The behaviours in the order they run each gameplay tick, in two boxes: "This entity" (Remove takes one off this entity; Remove from all takes it off every entity of the prefab) and, for an entity made from a prefab, "Every entity of <prefab>"
(what the editor gave to or took from all of them, which new entities get too). Each box has an Add combo and takes a script dropped on it.
*/
void InspectorPanel::drawBehaviour(PoolId entityId_)
{
	PoolId prefabId = getEntity(entityId_).prefabId;
	const auto& rTypes = Globals::Game::entityBehaviorTypes;

	ImGui::TextDisabled("Drag a script from the Assets tab onto a box to add its behaviours.");
	if(!this->message.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", this->message.c_str());

	ImGui::TextUnformatted("This entity, in running order:");
	ImGui::BeginChild("EntityBehaviours", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
	std::vector<PoolId> ids;
	if(getEntity(entityId_).behaviorSetId != UninitializedPoolId) ids = Globals::Game::entityBehaviors[getEntity(entityId_).behaviorSetId].behaviorIds;
	if(ids.empty()) ImGui::TextDisabled("none");
	for(uint32_t i = 0; i < ids.size(); i++)
	{
		ImGui::PushID(static_cast<int>(i));
		ImGui::Text("%s%s", rTypes.names[ids[i].id].c_str(), rTypes[ids[i]].scriptFunction != 0 ? " (script)" : "");
		ImGui::SameLine();
		if(ImGui::SmallButton("Remove")) removeEntityBehavior(entityId_, ids[i]);
		if(prefabId != UninitializedPoolId)
		{
			ImGui::SameLine();
			if(ImGui::SmallButton("Remove from all")) removePrefabBehavior(prefabId, ids[i]);
		}
		ImGui::PopID();
	}
	PoolId picked = this->pickBehaviour("##addEntity");
	if(picked != UninitializedPoolId) this->giveBehaviour(entityId_, false, picked);
	ImGui::EndChild();
	this->acceptScript(entityId_, false);

	if(prefabId != UninitializedPoolId)
	{
		ImGui::Separator();
		ImGui::Text("Every entity of %s:", Globals::Game::prefabs.names[prefabId.id].c_str());
		ImGui::BeginChild("PrefabBehaviours", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		std::vector<PoolId> added = Globals::Game::prefabs[prefabId].addedBehaviors;
		std::vector<PoolId> removed = Globals::Game::prefabs[prefabId].removedBehaviors;
		if(added.empty() && removed.empty()) ImGui::TextDisabled("nothing added or removed");
		ImGui::PushID("added");
		for(uint32_t i = 0; i < added.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::Text("+ %s", rTypes.names[added[i].id].c_str());
			ImGui::SameLine();
			if(ImGui::SmallButton("Undo")) removePrefabBehavior(prefabId, added[i]);
			ImGui::PopID();
		}
		ImGui::PopID();
		ImGui::PushID("removed");
		for(uint32_t i = 0; i < removed.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::Text("- %s", rTypes.names[removed[i].id].c_str());
			ImGui::SameLine();
			if(ImGui::SmallButton("Undo")) addPrefabBehavior(prefabId, removed[i]);
			ImGui::PopID();
		}
		ImGui::PopID();
		PoolId pickedAll = this->pickBehaviour("##addPrefab");
		if(pickedAll != UninitializedPoolId) this->giveBehaviour(entityId_, true, pickedAll);
		ImGui::EndChild();
		this->acceptScript(entityId_, true);
	}

	if(this->openPick)
	{
		ImGui::OpenPopup("Pick behaviour");
		this->openPick = false;
	}
	if(ImGui::BeginPopup("Pick behaviour"))
	{
		ImGui::TextUnformatted("The script registers several behaviours:");
		for(PoolId candidate : this->candidates)
		{
			if(ImGui::Selectable(rTypes.names[candidate.id].c_str()))
			{
				this->giveBehaviour(entityId_, this->candidatesForPrefab, candidate);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}
}

/* Gives the model to one entity or to every entity of its prefab; a failure (the model does not fit the entity's textures, a missing entry) is shown on the tab. */
void InspectorPanel::giveMesh(PoolId entityId_, bool forPrefab_, const MeshChoice& rChoice_)
{
	this->message.clear();
	try
	{
		if(!forPrefab_) setEntityMesh(entityId_, rChoice_);
		else if(uint32_t failed = setPrefabMesh(getEntity(entityId_).prefabId, rChoice_); failed != 0) this->message = std::format("{} entities of the prefab could not use it (the first reason is on stderr).", failed);
	}
	catch(const std::runtime_error& rError)
	{
		this->message = std::format("Model not used: {}.", rError.what());
	}
}

/* The same drop as acceptTexture for a model tile ("MESH" payload: "disc" or "external", a tab, the name: a SADX model as "<rel>@0x<offset>", a Shadow .DFF entry, an External model). */
void InspectorPanel::acceptMesh(PoolId entityId_, bool forPrefab_)
{
	if(!ImGui::BeginDragDropTarget()) return;
	if(const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload("MESH"))
	{
		std::string text(static_cast<const char*>(pPayload->Data));
		size_t tab = text.find('\t');
		MeshChoice choice;
		choice.kind = text.substr(0, tab) == "external" ? MeshChoiceKind::External : MeshChoiceKind::Disc;
		choice.name = text.substr(tab + 1);
		this->giveMesh(entityId_, forPrefab_, choice);
	}
	ImGui::EndDragDropTarget();
}

/* A combo of the external models (the opened level's, then the project's whose name the level does not use); returns the name picked this frame, "" if none. */
std::string InspectorPanel::pickExternalModel(const char* id_)
{
	std::string picked;
	if(ImGui::BeginCombo(id_, "External model..."))
	{
		for(int level = 0; level < 2; level++)
		{
			ExternalPools& rPools = level == 0 ? externalLevel : externalProject;
			for(size_t i = 0; i < rPools.models.allocatedSize(); i++)
			{
				if(!rPools.models.alive[i]) continue;
				const std::string& rName = rPools.models.names[i];
				if(level == 1 && externalLevel.models.nameToId.contains(rName)) continue;
				if(ImGui::Selectable(std::format("{}{}", rName, level == 0 ? " (level)" : "").c_str())) picked = rName;
			}
		}
		ImGui::EndCombo();
	}
	return picked;
}

/*
The model the entity is drawn with, in two boxes like the Texture tab: "This entity" and, for an entity made from a prefab, "Every entity of <prefab>". Each box shows the choice, a Default button, a combo of the external models, for a Shadow entity a combo
of the .DFF entries of its archive, and takes a model tile dragged from the Assets tab (a SADX model, a Shadow .DFF of the entity's archive, an External model). The collision, physics and behaviours are not touched; the textures stay (a model that uses
texture ids or names the entity's textures lack is refused or left untextured, see MeshChoice.h).
*/
void InspectorPanel::drawMesh(PoolId entityId_)
{
	Entity& rEntity = getEntity(entityId_);
	PoolId prefabId = rEntity.prefabId;
	auto describe = [](const MeshChoice& rChoice_)
	{
		if(rChoice_.kind == MeshChoiceKind::Disc) return std::format("Disc: {}", rChoice_.name);
		if(rChoice_.kind == MeshChoiceKind::External) return std::format("External: {}", rChoice_.name);
		return std::string("Default (the entity's own)");
	};
	auto drawBox = [&](bool forPrefab_)
	{
		const MeshChoice& rChoice = forPrefab_ ? Globals::Game::prefabs[prefabId].mesh : rEntity.meshChoice;
		ImGui::TextUnformatted(describe(rChoice).c_str());
		ImGui::SameLine();
		if(rChoice.kind != MeshChoiceKind::Default && ImGui::SmallButton("Default")) this->giveMesh(entityId_, forPrefab_, MeshChoice{});
		std::string external = this->pickExternalModel("##external");
		if(!external.empty()) this->giveMesh(entityId_, forPrefab_, MeshChoice{MeshChoiceKind::External, external});
		if(rEntity.sourceEngine == STH2005_ONE && ImGui::BeginCombo("##archive", "Model of the entity's archive..."))
		{
			for(const std::string& rName : rEntity.discMeshOptions())
			{
				if(ImGui::Selectable(rName.c_str(), rChoice.kind == MeshChoiceKind::Disc && rChoice.name == rName)) this->giveMesh(entityId_, forPrefab_, MeshChoice{MeshChoiceKind::Disc, rName});
			}
			ImGui::EndCombo();
		}
	};

	ImGui::TextDisabled("Drag a model from the Assets tab onto a box.");
	if(!this->message.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", this->message.c_str());
	ImGui::TextUnformatted("This entity:");
	ImGui::BeginChild("EntityMesh", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
	ImGui::PushID("entity");
	drawBox(false);
	ImGui::PopID();
	ImGui::EndChild();
	this->acceptMesh(entityId_, false);

	if(prefabId != UninitializedPoolId)
	{
		ImGui::Separator();
		ImGui::Text("Every entity of %s:", Globals::Game::prefabs.names[prefabId.id].c_str());
		ImGui::BeginChild("PrefabMesh", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		ImGui::PushID("prefab");
		drawBox(true);
		ImGui::PopID();
		ImGui::EndChild();
		this->acceptMesh(entityId_, true);
	}
}

/* Gives the texture to one entity or to every entity of its prefab; a failure (the texture does not fit the model, a missing entry) is shown on the tab. */
void InspectorPanel::giveTexture(PoolId entityId_, bool forPrefab_, const TextureChoice& rChoice_)
{
	this->message.clear();
	try
	{
		if(!forPrefab_) setEntityTexture(entityId_, rChoice_);
		else if(uint32_t failed = setPrefabTexture(getEntity(entityId_).prefabId, rChoice_); failed != 0) this->message = std::format("{} entities of the prefab could not use it (the first reason is on stderr).", failed);
	}
	catch(const std::runtime_error& rError)
	{
		this->message = std::format("Texture not used: {}.", rError.what());
	}
}

/*
Makes the box drawn just before a place a texture tile from the Assets tab can be dropped on ("TEXTURE" payload: "disc" or "external", a tab, the name). A SADX disc texture is a .GVM set (it replaces the entity's whole set); a Shadow one is a
.TXD entry of the entity's own archive, so only the tiles of that archive's .ONE fit (the Assets tab shows the archive in the tile's tooltip).
*/
void InspectorPanel::acceptTexture(PoolId entityId_, bool forPrefab_)
{
	if(!ImGui::BeginDragDropTarget()) return;
	if(const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload("TEXTURE"))
	{
		std::string text(static_cast<const char*>(pPayload->Data));
		size_t tab = text.find('\t');
		TextureChoice choice;
		choice.kind = text.substr(0, tab) == "external" ? TextureChoiceKind::External : TextureChoiceKind::Disc;
		choice.name = text.substr(tab + 1);
		this->giveTexture(entityId_, forPrefab_, choice);
	}
	ImGui::EndDragDropTarget();
}

/* A combo of the external textures (the opened level's, then the project's whose name the level does not use); returns the name picked this frame, "" if none. */
std::string InspectorPanel::pickExternalTexture(const char* id_)
{
	std::string picked;
	if(ImGui::BeginCombo(id_, "External texture (png / jpeg)..."))
	{
		for(int level = 0; level < 2; level++)
		{
			ExternalPools& rPools = level == 0 ? externalLevel : externalProject;
			for(size_t i = 0; i < rPools.textures.allocatedSize(); i++)
			{
				if(!rPools.textures.alive[i]) continue;
				const std::string& rName = rPools.textures.names[i];
				if(level == 1 && externalLevel.textures.nameToId.contains(rName)) continue;
				if(ImGui::Selectable(std::format("{}{}", rName, level == 0 ? " (level)" : "").c_str())) picked = rName;
			}
		}
		ImGui::EndCombo();
	}
	return picked;
}

/*
The texture the entity is drawn with, in two boxes like the Behaviour tab: "This entity" and, for an entity made from a prefab, "Every entity of <prefab>" (which new entities get too, and which replaces the choice of an entity that had its own).
Each box shows the choice, a Default button (the model's own texture), a combo of the external textures (png / jpeg files of Assets/External, drawn on every textured submesh) and, for a Shadow entity, a combo of the .TXD entries of its archive; a
tile dragged from the Assets tab (a SADX .GVM, a Shadow .TXD of the entity's archive, an External texture) is dropped on the box.
*/
void InspectorPanel::drawTexture(PoolId entityId_)
{
	Entity& rEntity = getEntity(entityId_);
	PoolId prefabId = rEntity.prefabId;
	auto describe = [](const TextureChoice& rChoice_)
	{
		if(rChoice_.kind == TextureChoiceKind::Disc) return std::format("Disc: {}", rChoice_.name);
		if(rChoice_.kind == TextureChoiceKind::External) return std::format("External: {}", rChoice_.name);
		return std::string("Default (the model's own)");
	};
	auto drawBox = [&](bool forPrefab_)
	{
		const TextureChoice& rChoice = forPrefab_ ? Globals::Game::prefabs[prefabId].texture : rEntity.textureChoice;
		ImGui::TextUnformatted(describe(rChoice).c_str());
		ImGui::SameLine();
		if(rChoice.kind != TextureChoiceKind::Default && ImGui::SmallButton("Default")) this->giveTexture(entityId_, forPrefab_, TextureChoice{});
		std::string external = this->pickExternalTexture("##external");
		if(!external.empty()) this->giveTexture(entityId_, forPrefab_, TextureChoice{TextureChoiceKind::External, external});
		if(rEntity.sourceEngine == STH2005_ONE && ImGui::BeginCombo("##archive", "Texture of the model's archive..."))
		{
			for(const std::string& rName : rEntity.discTextureOptions())
			{
				if(ImGui::Selectable(rName.c_str(), rChoice.kind == TextureChoiceKind::Disc && rChoice.name == rName)) this->giveTexture(entityId_, forPrefab_, TextureChoice{TextureChoiceKind::Disc, rName});
			}
			ImGui::EndCombo();
		}
	};

	ImGui::TextDisabled("Drag a texture from the Assets tab onto a box.");
	if(!this->message.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", this->message.c_str());
	ImGui::TextUnformatted("This entity:");
	ImGui::BeginChild("EntityTexture", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
	ImGui::PushID("entity");
	drawBox(false);
	ImGui::PopID();
	ImGui::EndChild();
	this->acceptTexture(entityId_, false);

	if(prefabId != UninitializedPoolId)
	{
		ImGui::Separator();
		ImGui::Text("Every entity of %s:", Globals::Game::prefabs.names[prefabId.id].c_str());
		ImGui::BeginChild("PrefabTexture", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		ImGui::PushID("prefab");
		drawBox(true);
		ImGui::PopID();
		ImGui::EndChild();
		this->acceptTexture(entityId_, true);
	}
}

/*
The camera's settings, and where it renders: the screen or one of the render targets, with a button that makes a new target (and points the camera at it) and, for a target, a preview of what it shows. The main camera is
the one the mouse and the keys fly in the debug mode.
*/
void InspectorPanel::drawCamera(PoolId entityId_)
{
	PoolId cameraId = getEntity(entityId_).cameraId;
	Camera& rCamera = Globals::Game::cameras[cameraId];

	bool isMain = Globals::Game::mainCamera == cameraId;
	if(isMain) ImGui::TextDisabled("Main camera");
	else if(ImGui::Button("Make main camera")) Globals::Game::mainCamera = cameraId;
	ImGui::Checkbox("Enabled", &rCamera.enabled);

	glm::vec3 position = rCamera.position;
	if(ImGui::DragFloat3("Position", &position.x, 0.5f)) rCamera.set(position);
	ImGui::Text("Yaw %.1f  Pitch %.1f", rCamera.yaw, rCamera.pitch);
	ImGui::SliderFloat("Speed (units/s)", &rCamera.speed, 1.0f, 5000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Boost (Shift multiplier)", &rCamera.speedUp, 1.0f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	if(ImGui::SliderFloat("Field of view", &rCamera.fov, 10.0f, 150.0f, "%.0f")) rCamera.refreshProjection();
	if(ImGui::SliderFloat("Near plane", &rCamera.nearPlane, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) rCamera.refreshProjection();
	if(ImGui::SliderFloat("Render distance (far plane: levels, entities, behaviours)", &rCamera.farPlane, 100.0f, 500000.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) rCamera.refreshProjection();
	ImGui::Separator();

	Graphics::RenderTarget* pTarget = rCamera.renderTarget();
	std::string current = pTarget == nullptr ? "Screen" : std::format("Target {} ({}x{})", rCamera.renderTargetId.id, pTarget->width, pTarget->height);
	if(ImGui::BeginCombo("Renders to", current.c_str()))
	{
		if(ImGui::Selectable("Screen", pTarget == nullptr)) rCamera.setRenderToScreen();
		for(uint32_t i = 0; i < Globals::Game::renderTargets.objects.size(); i++)
		{
			if(!Globals::Game::renderTargets.alive[i]) continue;
			const Graphics::RenderTarget& rOption = Globals::Game::renderTargets.objects[i];
			PoolId optionId(i, Globals::Game::renderTargets.generation[i]);
			std::string label = std::format("Target {} ({}x{})", i, rOption.width, rOption.height);
			if(ImGui::Selectable(label.c_str(), optionId == rCamera.renderTargetId)) rCamera.setRenderTarget(optionId);
		}
		ImGui::EndCombo();
	}
	ImGui::InputInt("New target width", &this->newTargetWidth);
	ImGui::InputInt("New target height", &this->newTargetHeight);
	this->newTargetWidth = std::clamp(this->newTargetWidth, 1, 8192);
	this->newTargetHeight = std::clamp(this->newTargetHeight, 1, 8192);
	if(ImGui::Button("New render target for this camera"))
	{
		rCamera.setRenderTarget(addRenderTarget(static_cast<uint32_t>(this->newTargetWidth), static_cast<uint32_t>(this->newTargetHeight)));
		pTarget = rCamera.renderTarget();
	}
	if(pTarget != nullptr)
	{
		if(ImGui::Button("Resize target to the size above"))
		{
			pTarget->resize(static_cast<uint32_t>(this->newTargetWidth), static_cast<uint32_t>(this->newTargetHeight));
			/* Every camera that renders to this target takes the new aspect. */
			for(uint32_t i = 0; i < Globals::Game::cameras.objects.size(); i++)
			{
				if(Globals::Game::cameras.alive[i] && Globals::Game::cameras.objects[i].renderTargetId == rCamera.renderTargetId) Globals::Game::cameras.objects[i].refreshProjection();
			}
		}
		/* The texture is upside down (GL's origin is the bottom left), hence the flipped UVs; the display view skips the sRGB decode, see RenderTarget. */
		float width = ImGui::GetContentRegionAvail().x;
		float height = width * static_cast<float>(pTarget->height) / static_cast<float>(pTarget->width);
		ImGui::Image(static_cast<ImTextureID>(pTarget->displayTexture), ImVec2(width, height), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	}
}
}
