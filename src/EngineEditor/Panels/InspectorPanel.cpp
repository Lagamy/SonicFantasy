#include "InspectorPanel.h"
#include "Globals.h"
#include "API.h"
#include "Editor.h"
#include "Level.h"
#include "Prefab.h"
#include "EntityBehavior.h"
#include "GPU_Globals.h"
#include "imgui.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
			editor.collisionView.boundAreaTabActive = true;
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

/* A small button beside a transform row that makes the gizmo's mode the one for that row; the active mode's button is shown pressed. */
static void gizmoButton(const char* label_, Gizmo::Mode mode_)
{
	bool active = editor.gizmo.mode == mode_;
	if(active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	ImGui::SameLine();
	if(ImGui::SmallButton(label_)) editor.gizmo.mode = mode_;
	if(active) ImGui::PopStyleColor();
}

/*
The position is set with Entity::setPosition (a teleport, which also refreshes what depends on it). The rotation is shown as Euler angles in degrees (x, y, z: the quaternion's eulerAngles, converted from Entity::rotation each frame) and set with Entity::setRotation
from the three angles; the scale is Entity::scale per axis of the entity's own space (Entity::setScale). Each row has a button that picks the gizmo mode for it.
*/
void InspectorPanel::drawTransform(PoolId entityId_)
{
	Entity& rEntity = getEntity(entityId_);
	glm::vec3 position = rEntity.position;
	if(ImGui::DragFloat3("Position", &position.x, 0.5f)) rEntity.setPosition(position);
	gizmoButton("Move##position", Gizmo::Move);
	glm::vec3 euler = glm::degrees(glm::eulerAngles(glm::quat_cast(rEntity.rotation)));
	if(ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) rEntity.setRotation(glm::mat4_cast(glm::quat(glm::radians(euler))));
	gizmoButton("Rotate##rotation", Gizmo::Rotate);
	glm::vec3 scale = rEntity.scale;
	if(ImGui::DragFloat3("Scale", &scale.x, 0.01f)) rEntity.setScale(scale);
	gizmoButton("Scale##scale", Gizmo::Scale);
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
/* The scope the prefab-wide boxes edit: Global only when the switch says so and the prefab can have a Changed file (not External). */
static PrefabScope scopeOf(bool allLevels_, PoolId prefabId_)
{
	return allLevels_ && prefabId_ != UninitializedPoolId && Globals::Game::prefabs[prefabId_].kind != PrefabKind::External ? PrefabScope::Global : PrefabScope::Level;
}

/* What the boxes show: the Changed file's edits (Global), or the level's on top of the Source Game Level's (Level; the level's own edits are what the boxes change). */
static PrefabEdits editsOf(PoolId prefabId_, PrefabScope scope_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	return scope_ == PrefabScope::Global ? rPrefab.changed : mergePrefabEdits(rPrefab.source, rPrefab.level);
}

/* The switch under the title of a prefab-wide box. Nothing for an External prefab (it has no Changed file). */
void InspectorPanel::drawScopeSwitch(PoolId prefabId_)
{
	if(Globals::Game::prefabs[prefabId_].kind == PrefabKind::External) return;
	if(ImGui::RadioButton("This level", !this->allLevels)) this->allLevels = false;
	ImGui::SameLine();
	if(ImGui::RadioButton("All levels", this->allLevels)) this->allLevels = true;
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("All levels edits the prefab's Changed file (External/Prefabs/<game>/<KEY>/Changed/prefab.json), saved at once.");
}

/* An edit that can fail to write the Changed file: the reason is shown on the tab instead of leaving the editor. */
void InspectorPanel::tryEdit(const std::function<void()>& rEdit_)
{
	this->message.clear();
	try
	{
		rEdit_();
	}
	catch(const std::runtime_error& rError)
	{
		this->message = std::format("Not saved: {}", rError.what());
	}
}

static std::string shapeSummary(const Physics::CollisionShape& rShape_)
{
	return std::format("form {}  a {:.1f} b {:.1f} c {:.1f}  at {:.1f}, {:.1f}, {:.1f}", rShape_.form, rShape_.a, rShape_.b, rShape_.c, rShape_.center.x, rShape_.center.y, rShape_.center.z);
}

/*
The four shapes the editor offers by name (Physics::CollisionShape::form): a sphere is form 0 (radius a), a box one of 3 to 5 and 7 (half sizes a, b, c; 3 is made), a capsule form 6 (radius a, half length b, along the shape's z axis for Shadow, y for SADX),
a cylinder form 1 or 2 (radius a, height b, along y: SADX's; 1 is made) or 11 (radius a, half length b, along z: the form of Shadow's own importer, made for a Shadow entity).
*/
enum ShapeKind
{
	ShapeSphere = 0,
	ShapeBox = 1,
	ShapeCapsule = 2,
	ShapeCylinder = 3,
	ShapeOther = -1,
};

static int shapeKindOf(const Physics::CollisionShape& rShape_)
{
	if(rShape_.form == 0) return ShapeSphere;
	if((rShape_.form >= 3 && rShape_.form <= 5) || rShape_.form == 7) return ShapeBox;
	if(rShape_.form == 6) return ShapeCapsule;
	if(rShape_.form == 1 || rShape_.form == 2 || rShape_.form == 11) return ShapeCylinder;
	return ShapeOther;
}

static const char* shapeKindName(int kind_)
{
	if(kind_ == ShapeSphere) return "Sphere";
	if(kind_ == ShapeBox) return "Box";
	if(kind_ == ShapeCapsule) return "Capsule";
	if(kind_ == ShapeCylinder) return "Cylinder";
	return "Other";
}

/* Makes the shape that kind: sets the form (a cylinder's depends on the game of the entity, sadx_) and gives a size that is still 0 the value of the radius a, so a shape never turns into an empty one. */
static void setShapeKind(Physics::CollisionShape& rShape_, int kind_, bool sadx_)
{
	if(rShape_.a <= 0.0f) rShape_.a = 5.0f;
	if(kind_ == ShapeSphere)
	{
		rShape_.form = 0;
	}
	else if(kind_ == ShapeBox)
	{
		if(shapeKindOf(rShape_) != ShapeBox) rShape_.form = 3;
		if(rShape_.b <= 0.0f) rShape_.b = rShape_.a;
		if(rShape_.c <= 0.0f) rShape_.c = rShape_.a;
	}
	else if(kind_ == ShapeCapsule)
	{
		rShape_.form = 6;
		if(rShape_.b <= 0.0f) rShape_.b = rShape_.a;
	}
	else if(kind_ == ShapeCylinder)
	{
		if(shapeKindOf(rShape_) != ShapeCylinder) rShape_.form = sadx_ ? 1 : 11;
		if(rShape_.b <= 0.0f) rShape_.b = rShape_.form == 11 ? rShape_.a : rShape_.a * 2.0f;
	}
}

/* A new shape for the Add buttons: of the kind, size 5 at the entity's origin, no collision behaviour (counts as a Body); the rest is edited in the shape's fields. */
static Physics::CollisionShape defaultShape(int kind_, bool sadx_)
{
	Physics::CollisionShape shape{};
	shape.a = 5.0f;
	shape.behavior = UninitializedPoolId;
	setShapeKind(shape, kind_, sadx_);
	return shape;
}

/* The picture of a shape kind (a circle with its equator, a cube, a cylinder, a pill) in a 28 x 28 cell around center_. */
static void drawShapeGlyph(ImDrawList* pDraw, ImVec2 center, int kind_, ImU32 color)
{
	if(kind_ == ShapeSphere)
	{
		pDraw->AddCircle(center, 9.0f, color, 24, 1.5f);
		ImVec2 equator[17];
		for(int i = 0; i <= 16; i++)
		{
			float angle = 6.2831853f * i / 16.0f;
			equator[i] = ImVec2(center.x + std::cos(angle) * 9.0f, center.y + std::sin(angle) * 3.0f);
		}
		pDraw->AddPolyline(equator, 17, color, ImDrawFlags_None, 1.0f);
	}
	else if(kind_ == ShapeBox)
	{
		ImVec2 frontMin(center.x - 9.0f, center.y - 5.0f);
		ImVec2 frontMax(center.x + 5.0f, center.y + 9.0f);
		ImVec2 shift(4.0f, -4.0f);
		ImVec2 backMin(frontMin.x + shift.x, frontMin.y + shift.y);
		ImVec2 backMax(frontMax.x + shift.x, frontMax.y + shift.y);
		pDraw->AddRect(backMin, backMax, color, 0.0f, ImDrawFlags_None, 1.0f);
		pDraw->AddRect(frontMin, frontMax, color, 0.0f, ImDrawFlags_None, 1.5f);
		pDraw->AddLine(frontMin, backMin, color, 1.0f);
		pDraw->AddLine(ImVec2(frontMax.x, frontMin.y), ImVec2(backMax.x, backMin.y), color, 1.0f);
		pDraw->AddLine(ImVec2(frontMin.x, frontMax.y), ImVec2(backMin.x, backMax.y), color, 1.0f);
		pDraw->AddLine(frontMax, backMax, color, 1.0f);
	}
	else if(kind_ == ShapeCylinder)
	{
		ImVec2 top[17];
		ImVec2 bottom[9];
		for(int i = 0; i <= 16; i++)
		{
			float angle = 6.2831853f * i / 16.0f;
			top[i] = ImVec2(center.x + std::cos(angle) * 8.0f, center.y - 7.0f + std::sin(angle) * 3.0f);
			if(i <= 8) bottom[i] = ImVec2(center.x + std::cos(angle) * 8.0f, center.y + 7.0f + std::sin(angle) * 3.0f);
		}
		pDraw->AddPolyline(top, 17, color, ImDrawFlags_None, 1.5f);
		pDraw->AddPolyline(bottom, 9, color, ImDrawFlags_None, 1.5f);
		pDraw->AddLine(ImVec2(center.x - 8.0f, center.y - 7.0f), ImVec2(center.x - 8.0f, center.y + 7.0f), color, 1.5f);
		pDraw->AddLine(ImVec2(center.x + 8.0f, center.y - 7.0f), ImVec2(center.x + 8.0f, center.y + 7.0f), color, 1.5f);
	}
	else
	{
		pDraw->AddRect(ImVec2(center.x - 6.0f, center.y - 11.0f), ImVec2(center.x + 6.0f, center.y + 11.0f), color, 6.0f, ImDrawFlags_None, 1.5f);
		pDraw->AddLine(ImVec2(center.x - 6.0f, center.y - 5.0f), ImVec2(center.x + 6.0f, center.y - 5.0f), color, 1.0f);
		pDraw->AddLine(ImVec2(center.x - 6.0f, center.y + 5.0f), ImVec2(center.x + 6.0f, center.y + 5.0f), color, 1.0f);
	}
}

/* A small, not clickable picture of a shape kind for a list entry (the glyph at 0.7 of its size), on the line of the entry that follows (SameLine); color_ tints it. */
static void shapeBadge(int kind_, ImU32 color_)
{
	const float size = 20.0f;
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
	ImGui::Dummy(ImVec2(size, size));
	ImDrawList* pDraw = ImGui::GetWindowDrawList();
	int first = pDraw->VtxBuffer.Size;
	drawShapeGlyph(pDraw, center, kind_, color_);
	for(int v = first; v < pDraw->VtxBuffer.Size; v++)
	{
		ImVec2& rPos = pDraw->VtxBuffer[v].pos;
		rPos = ImVec2(center.x + (rPos.x - center.x) * 0.7f, center.y + (rPos.y - center.y) * 0.7f);
	}
	ImGui::SameLine(0.0f, 4.0f);
}

/* One icon button of a shape kind, drawn with the draw list (a circle with its equator, a cube, a pill); true when clicked. selected_ highlights it. */
static bool shapeIcon(const char* id_, int kind_, bool selected_)
{
	const ImVec2 size(28.0f, 28.0f);
	ImVec2 origin = ImGui::GetCursorScreenPos();
	bool pressed = ImGui::InvisibleButton(id_, size);
	bool hovered = ImGui::IsItemHovered();
	ImDrawList* pDraw = ImGui::GetWindowDrawList();
	ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
	ImU32 background = selected_ ? IM_COL32(80, 66, 24, 255) : hovered ? IM_COL32(64, 64, 64, 255) : IM_COL32(40, 40, 40, 255);
	ImU32 color = selected_ ? IM_COL32(255, 200, 60, 255) : IM_COL32(190, 190, 190, 255);
	pDraw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), background, 4.0f);

	drawShapeGlyph(pDraw, center, kind_, color);
	if(hovered) ImGui::SetTooltip("%s", shapeKindName(kind_));
	return pressed;
}

/* The four shape icons in a row, none selected (selectedKind_ = ShapeOther) or the shape's kind highlighted; the kind clicked this frame, ShapeOther otherwise. */
static int shapeIconRow(int selectedKind_)
{
	int clicked = ShapeOther;
	for(int kind : {ShapeSphere, ShapeBox, ShapeCapsule, ShapeCylinder})
	{
		ImGui::PushID(kind);
		if(kind != ShapeSphere) ImGui::SameLine();
		if(shapeIcon("shape", kind, kind == selectedKind_)) clicked = kind;
		ImGui::PopID();
	}
	return clicked;
}

/*
The fields of one shape, for a shape of "This entity" and for a prefab-wide one: the kind icons (changing the kind keeps the sizes that are set), the size fields of the kind, center, angle, the raw fields, and the collision behaviour with its tags.
sadx_ (the entity's game) decides the form a cylinder is given. True when anything was edited.
*/
static bool drawShapeFields(Physics::CollisionShape& rShape_, bool sadx_)
{
	bool changed = false;
	int kind = shapeKindOf(rShape_);
	int picked = shapeIconRow(kind);
	if(picked != ShapeOther && picked != kind)
	{
		setShapeKind(rShape_, picked, sadx_);
		kind = picked;
		changed = true;
	}
	if(kind == ShapeSphere)
	{
		changed |= ImGui::DragFloat("radius", &rShape_.a, 0.1f, 0.0f, 100000.0f);
	}
	else if(kind == ShapeBox)
	{
		changed |= ImGui::DragFloat3("half size (x, y, z)", &rShape_.a, 0.1f, 0.0f, 100000.0f);
	}
	else if(kind == ShapeCapsule)
	{
		changed |= ImGui::DragFloat("radius", &rShape_.a, 0.1f, 0.0f, 100000.0f);
		changed |= ImGui::DragFloat("half length", &rShape_.b, 0.1f, 0.0f, 100000.0f);
		ImGui::TextDisabled("The segment lies along z (Shadow entities) or y (SADX entities).");
	}
	else if(kind == ShapeCylinder)
	{
		changed |= ImGui::DragFloat("radius", &rShape_.a, 0.1f, 0.0f, 100000.0f);
		if(rShape_.form == 11)
		{
			changed |= ImGui::DragFloat("half length", &rShape_.b, 0.1f, 0.0f, 100000.0f);
			ImGui::TextDisabled("Form 11: the axis lies along z.");
		}
		else
		{
			changed |= ImGui::DragFloat("height", &rShape_.b, 0.1f, 0.0f, 100000.0f);
			ImGui::TextDisabled("Form %u: the axis stands along y, centered on the shape's center.", rShape_.form);
		}
	}
	else
	{
		ImGui::TextDisabled("Form %u is none of the four: edit its raw fields below, or pick one above.", rShape_.form);
	}
	changed |= ImGui::DragFloat3("center", &rShape_.center.x, 0.1f);
	changed |= ImGui::DragInt3("angle", &rShape_.angle.x, 16.0f);
	if(ImGui::TreeNode("Raw fields"))
	{
		changed |= ImGui::InputScalar("form", ImGuiDataType_U8, &rShape_.form);
		changed |= ImGui::DragFloat("a", &rShape_.a, 0.1f);
		changed |= ImGui::DragFloat("b", &rShape_.b, 0.1f);
		changed |= ImGui::DragFloat("c", &rShape_.c, 0.1f);
		changed |= ImGui::DragFloat("d", &rShape_.d, 0.1f);
		changed |= ImGui::InputScalar("kind", ImGuiDataType_U8, &rShape_.kind);
		changed |= ImGui::InputScalar("push", ImGuiDataType_U8, &rShape_.push);
		changed |= ImGui::InputScalar("damage", ImGuiDataType_U8, &rShape_.damage);
		changed |= ImGui::InputScalar("attr", ImGuiDataType_U32, &rShape_.attr, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
		ImGui::TreePop();
	}
	const auto& rBehaviors = Globals::Game::behaviors;
	const char* current = rShape_.behavior == UninitializedPoolId ? "(none: counts as a Body)" : rBehaviors.names[rShape_.behavior.id].c_str();
	if(ImGui::BeginCombo("behaviour", current))
	{
		if(ImGui::Selectable("(none: counts as a Body)", rShape_.behavior == UninitializedPoolId))
		{
			rShape_.behavior = UninitializedPoolId;
			changed = true;
		}
		for(uint32_t b = 0; b < rBehaviors.objects.size(); b++)
		{
			if(!rBehaviors.alive[b]) continue;
			PoolId behaviorId(b, rBehaviors.generation[b]);
			if(ImGui::Selectable(rBehaviors.names[b].c_str(), rShape_.behavior == behaviorId))
			{
				rShape_.behavior = behaviorId;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	if(rShape_.behavior != UninitializedPoolId)
	{
		const BABehavior& rBehavior = rBehaviors[rShape_.behavior];
		for(PoolId tag : rBehavior.tags) ImGui::BulletText("tag %s", Globals::Game::baTags.names[tag.id].c_str());
		ImGui::Text("Collides with %u tags, interacts with %u", static_cast<uint32_t>(rBehavior.availableCollisions.size()), static_cast<uint32_t>(rBehavior.availableInteractions.size()));
	}
	return changed;
}

/*
The bound area in two boxes. "This entity": the shapes of its collision, each editable in place (the raw fields of Physics::CollisionShape, the collision behaviour by combo; what the values mean is only what CollisionShape.h says), with Remove (this entity),
Remove from all and Add to all (a copy, for every entity of the prefab) and Add shape icons (sphere, box, capsule, cylinder: the icon row of a shape node changes its kind; the size fields follow the kind, the raw fields are under Raw fields). For an entity made from a prefab, "Every entity of <prefab>" (This level / All levels): the shapes added to all of them, which new entities get too, edited in place there (and no longer listed under This entity), and the shapes taken from them, with Undo.
An entity without a collision gets a hand-made one by its first shape. Shapes from the game's tables are read as the game has them; the shape's tags and interactions are those of its behaviour.
*/
void InspectorPanel::drawBoundArea(PoolId entityId_)
{
	Entity& rEntity = getEntity(entityId_);
	PoolId prefabId = rEntity.prefabId;
	PrefabScope scope = scopeOf(this->allLevels, prefabId);
	std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);
	if(!this->message.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", this->message.c_str());

	if(rEntity.collisionId != UninitializedPoolId)
	{
		const Physics::Collision& rCollision = Globals::Physics::collisions[rEntity.collisionId];
		ImGui::Text("Source: %s", rCollision.name.empty() ? "(made by hand)" : rCollision.name.c_str());
		ImGui::Text("Shapes: %u", static_cast<uint32_t>(rCollision.shapes.size()));
		ImGui::Text("Bounds: %.1f, %.1f, %.1f to %.1f, %.1f, %.1f", rCollision.boundsMin.x, rCollision.boundsMin.y, rCollision.boundsMin.z, rCollision.boundsMax.x, rCollision.boundsMax.y, rCollision.boundsMax.z);
	}
	/*
	A shape the prefab-wide box below adds (in the scope it shows) is the prefab's, not the entity's own: it is listed and edited there, so a character's body shape is one place for every entity and level, and is not listed here.
	Each added shape hides the first equal one of this entity.
	*/
	std::vector<bool> hidden(shapes.size(), false);
	uint32_t hiddenCount = 0;
	if(prefabId != UninitializedPoolId)
	{
		for(const Physics::CollisionShape& rAdded : editsOf(prefabId, scope).addedShapes)
		{
			for(uint32_t i = 0; i < shapes.size(); i++)
			{
				if(!hidden[i] && shapes[i] == rAdded)
				{
					hidden[i] = true;
					hiddenCount++;
					break;
				}
			}
		}
	}
	ImGui::TextUnformatted("This entity:");
	ImGui::BeginChild("EntityShapes", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
	if(shapes.size() == hiddenCount) ImGui::TextDisabled("none");
	if(hiddenCount > 0) ImGui::TextDisabled("%u shape(s) of the whole prefab are in the box below.", hiddenCount);
	int removeIndex = -1;
	int removeAllIndex = -1;
	int addAllIndex = -1;
	bool changed = false;
	for(uint32_t i = 0; i < shapes.size(); i++)
	{
		if(hidden[i]) continue;
		Physics::CollisionShape& rShape = shapes[i];
		ImGui::PushID(static_cast<int>(i));
		int kind = shapeKindOf(rShape);
		shapeBadge(kind, IM_COL32(190, 190, 190, 255));
		if(ImGui::TreeNode("Shape", "Shape %u: %s (form %u), attr 0x%08X", i, shapeKindName(kind), rShape.form, rShape.attr))
		{
			changed |= drawShapeFields(rShape, rEntity.sourceEngine == SA_NINJA);
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
	ImGui::TextUnformatted("Add shape:");
	ImGui::SameLine();
	ImGui::PushID("addShape");
	int addKind = shapeIconRow(ShapeOther);
	ImGui::PopID();
	if(addKind != ShapeOther)
	{
		shapes.push_back(defaultShape(addKind, rEntity.sourceEngine == SA_NINJA));
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
		this->tryEdit([&] { addPrefabShape(prefabId, copy, scope); });
	}
	else if(removeAllIndex >= 0)
	{
		if(changed) setEntityShapes(entityId_, shapes);
		this->tryEdit([&] { removePrefabShape(prefabId, shapes[removeAllIndex], scope); });
	}
	else if(changed || removeIndex >= 0)
	{
		setEntityShapes(entityId_, shapes);
	}

	if(prefabId != UninitializedPoolId)
	{
		ImGui::Separator();
		ImGui::Text("Every entity of %s:", Globals::Game::prefabs.names[prefabId.id].c_str());
		this->drawScopeSwitch(prefabId);
		ImGui::BeginChild("PrefabShapes", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		std::vector<Physics::CollisionShape> added = editsOf(prefabId, scope).addedShapes;
		std::vector<Physics::CollisionShape> removed = editsOf(prefabId, scope).removedShapes;
		if(added.empty() && removed.empty()) ImGui::TextDisabled("nothing added or removed");
		ImGui::PushID("added");
		for(uint32_t i = 0; i < added.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			Physics::CollisionShape edited = added[i];
			/* Blue: a shape every entity of the prefab has, so it stands out from the entity's own shapes. */
			shapeBadge(shapeKindOf(added[i]), IM_COL32(90, 160, 255, 255));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.63f, 1.0f, 1.0f));
			bool open = ImGui::TreeNode("Added", "+ %s", shapeSummary(added[i]).c_str());
			ImGui::PopStyleColor();
			if(open)
			{
				/* An edit is a removal of the old shape and an addition of the new one in the scope's set, so every entity of the prefab and the file follow at once. */
				if(drawShapeFields(edited, rEntity.sourceEngine == SA_NINJA))
				{
					this->tryEdit([&]
					{
						removePrefabShape(prefabId, added[i], scope);
						addPrefabShape(prefabId, edited, scope);
					});
				}
				if(ImGui::SmallButton("Undo")) this->tryEdit([&] { removePrefabShape(prefabId, added[i], scope); });
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::PopID();
		ImGui::PushID("removed");
		for(uint32_t i = 0; i < removed.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			shapeBadge(shapeKindOf(removed[i]), IM_COL32(190, 190, 190, 255));
			ImGui::Text("- %s", shapeSummary(removed[i]).c_str());
			ImGui::SameLine();
			if(ImGui::SmallButton("Undo")) this->tryEdit([&] { addPrefabShape(prefabId, removed[i], scope); });
			ImGui::PopID();
		}
		ImGui::PopID();
		ImGui::TextUnformatted("Add shape to all:");
		ImGui::SameLine();
		ImGui::PushID("addShapeToAll");
		int addAllKind = shapeIconRow(ShapeOther);
		ImGui::PopID();
		if(addAllKind != ShapeOther) this->tryEdit([&] { addPrefabShape(prefabId, defaultShape(addAllKind, rEntity.sourceEngine == SA_NINJA), scope); });
		ImGui::EndChild();
	}

	ImGui::Separator();
	this->drawBoundAreaRules();
}

/* A combo that lists every tag as a check mark; the popup stays open while ticking. rSelected_ holds tag names, so a tag that is gone stays listed. True when it changed. */
static bool tagChecklist(const char* label_, std::vector<std::string>& rSelected_)
{
	std::string summary;
	for(const std::string& rName : rSelected_) summary += (summary.empty() ? "" : ", ") + rName;
	if(summary.empty()) summary = "(none)";
	bool changed = false;
	if(ImGui::BeginCombo(label_, summary.c_str()))
	{
		const auto& rTags = Globals::Game::baTags;
		for(uint32_t t = 0; t < rTags.objects.size(); t++)
		{
			if(!rTags.alive[t]) continue;
			auto found = std::find(rSelected_.begin(), rSelected_.end(), rTags.names[t]);
			bool on = found != rSelected_.end();
			if(ImGui::Selectable(rTags.names[t].c_str(), on, ImGuiSelectableFlags_NoAutoClosePopups))
			{
				if(on) rSelected_.erase(found);
				else rSelected_.push_back(rTags.names[t]);
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	return changed;
}

/*
"Tags and behaviours": creates the project's own tags and bound area behaviours (BABehaviors::boundAreas, saved to External/Collision/BoundAreas.json at once) and edits their rules. A behaviour has the tags it is (what a shape
that has it is), the tags it collides with, and a list of onTouch entries: a function (a sf.onTouch one from Globals::Game::boundAreaFunctions, by combo or by dropping a "TOUCH" tile of the Assets tab on the Add button) with the tags
whose overlap calls it. The shape combo above lists the new behaviours at once. Editing one changes it for everything that uses it.
Tags and behaviours made here can be renamed and deleted (a tag or behaviour that is still used is refused, the reason shown on the tab). The built-in behaviours and the ones a script made are listed under "Built-in and script behaviours": Customise copies one
into the project's data (a script's functions are named "<behaviour>/<tag>" or by their sf.onTouch name), which then replaces the original rules at every start; Revert removes the copy.
*/
void InspectorPanel::drawBoundAreaRules()
{
	BABehaviors& rBehaviors = Globals::Game::baBehaviors;
	if(!ImGui::CollapsingHeader("Tags and behaviours")) return;

	ImGui::InputText("##newTag", this->newTagName, sizeof(this->newTagName));
	ImGui::SameLine();
	if(ImGui::Button("New tag"))
	{
		this->tryEdit([&]
		{
			rBehaviors.addBoundAreaTag(this->newTagName);
			rBehaviors.saveBoundAreas();
			this->newTagName[0] = '\0';
		});
	}
	ImGui::InputText("##newBehavior", this->newBehaviorName, sizeof(this->newBehaviorName));
	ImGui::SameLine();
	if(ImGui::Button("New behaviour"))
	{
		this->tryEdit([&]
		{
			rBehaviors.addBoundAreaBehavior(this->newBehaviorName);
			rBehaviors.saveBoundAreas();
			this->newBehaviorName[0] = '\0';
		});
	}

	/* The edits that change what exists wait until the lists are walked: they erase from what the rows walk. */
	std::string removeTag;
	int removeBehavior = -1;
	int revertBehavior = -1;
	bool openRename = false;

	if(!rBehaviors.boundAreas.tags.empty() && ImGui::TreeNode("Tags"))
	{
		for(size_t t = 0; t < rBehaviors.boundAreas.tags.size(); t++)
		{
			const std::string& rTag = rBehaviors.boundAreas.tags[t];
			bool fixed = std::find(rBehaviors.boundAreas.fixedTags.begin(), rBehaviors.boundAreas.fixedTags.end(), rTag) != rBehaviors.boundAreas.fixedTags.end();
			ImGui::PushID(static_cast<int>(t));
			ImGui::TextUnformatted(rTag.c_str());
			if(fixed)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(a script uses it)");
			}
			else
			{
				ImGui::SameLine();
				if(ImGui::SmallButton("Rename"))
				{
					this->renameKind = 1;
					this->renameTag = rTag;
					std::snprintf(this->renameText, sizeof(this->renameText), "%s", rTag.c_str());
					openRename = true;
				}
				ImGui::SameLine();
				if(ImGui::SmallButton("Delete")) removeTag = rTag;
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}

	const auto& rFunctions = Globals::Game::boundAreaFunctions;
	for(size_t b = 0; b < rBehaviors.boundAreas.behaviors.size(); b++)
	{
		BoundAreaBehaviorData& rData = rBehaviors.boundAreas.behaviors[b];
		ImGui::PushID(static_cast<int>(b));
		bool fromScript = rBehaviors.scriptBehaviorNamed(rData.name) != nullptr;
		if(ImGui::TreeNode("Behavior", "%s%s", rData.name.c_str(), fromScript ? " (customised built-in or script behaviour)" : ""))
		{
			bool changed = false;
			changed |= tagChecklist("Tags (what it is)", rData.tags);
			changed |= tagChecklist("Collides with", rData.collidesWith);

			ImGui::TextUnformatted("onTouch:");
			int removeRule = -1;
			for(size_t r = 0; r < rData.onTouch.size(); r++)
			{
				BoundAreaRule& rRule = rData.onTouch[r];
				ImGui::PushID(static_cast<int>(r));
				bool missing = !rFunctions.nameToId.contains(rRule.function);
				std::string current = missing ? std::format("{} (not registered)", rRule.function) : rRule.function;
				if(ImGui::BeginCombo("function", current.c_str()))
				{
					for(uint32_t f = 0; f < rFunctions.objects.size(); f++)
					{
						if(!rFunctions.alive[f]) continue;
						if(ImGui::Selectable(rFunctions.names[f].c_str(), rFunctions.names[f] == rRule.function))
						{
							rRule.function = rFunctions.names[f];
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				if(missing) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Function %s is not registered (its script is not loaded): the entry is kept and does nothing.", rRule.function.c_str());
				changed |= tagChecklist("when it touches", rRule.tags);
				if(ImGui::SmallButton("Remove")) removeRule = static_cast<int>(r);
				ImGui::PopID();
			}
			if(removeRule >= 0)
			{
				rData.onTouch.erase(rData.onTouch.begin() + removeRule);
				changed = true;
			}
			ImGui::Button("Add onTouch (drop a function here)");
			if(ImGui::BeginDragDropTarget())
			{
				if(const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload("TOUCH"))
				{
					rData.onTouch.push_back(BoundAreaRule{static_cast<const char*>(pPayload->Data), {}});
					changed = true;
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SameLine();
			if(ImGui::SmallButton("Add empty"))
			{
				rData.onTouch.push_back(BoundAreaRule{});
				changed = true;
			}

			if(changed)
			{
				this->tryEdit([&]
				{
					rBehaviors.applyBoundArea(b);
					rBehaviors.saveBoundAreas();
				});
			}

			if(fromScript)
			{
				if(ImGui::SmallButton("Revert to original")) revertBehavior = static_cast<int>(b);
			}
			else
			{
				if(ImGui::SmallButton("Rename"))
				{
					this->renameKind = 2;
					this->renameIndex = b;
					std::snprintf(this->renameText, sizeof(this->renameText), "%s", rData.name.c_str());
					openRename = true;
				}
				ImGui::SameLine();
				if(ImGui::SmallButton("Delete")) removeBehavior = static_cast<int>(b);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	/* What code made (built-ins and scripts) and nobody customised yet: read only, with the button that copies it into the project's data. */
	int customize = -1;
	if(!rBehaviors.boundAreas.scriptBehaviors.empty() && ImGui::TreeNode("Built-in and script behaviours"))
	{
		for(size_t s = 0; s < rBehaviors.boundAreas.scriptBehaviors.size(); s++)
		{
			const BoundAreaBehaviorData& rData = rBehaviors.boundAreas.scriptBehaviors[s];
			bool customised = std::any_of(rBehaviors.boundAreas.behaviors.begin(), rBehaviors.boundAreas.behaviors.end(), [&](const BoundAreaBehaviorData& rOther_) { return rOther_.name == rData.name; });
			if(customised) continue;
			ImGui::PushID(static_cast<int>(s));
			if(ImGui::TreeNode("Script behavior", "%s", rData.name.c_str()))
			{
				for(const std::string& rTag : rData.tags) ImGui::BulletText("tag %s", rTag.c_str());
				for(const std::string& rTag : rData.collidesWith) ImGui::BulletText("collides with %s", rTag.c_str());
				for(const BoundAreaRule& rRule : rData.onTouch)
				{
					std::string tags;
					for(const std::string& rTag : rRule.tags) tags += (tags.empty() ? "" : ", ") + rTag;
					ImGui::BulletText("%s when it touches %s", rRule.function.c_str(), tags.c_str());
				}
				if(ImGui::SmallButton("Customise")) customize = static_cast<int>(s);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}

	if(!removeTag.empty())
	{
		this->tryEdit([&]
		{
			rBehaviors.removeBoundAreaTag(removeTag);
			rBehaviors.saveBoundAreas();
		});
	}
	if(removeBehavior >= 0)
	{
		this->tryEdit([&]
		{
			rBehaviors.removeBoundAreaBehavior(static_cast<size_t>(removeBehavior));
			rBehaviors.saveBoundAreas();
		});
	}
	if(revertBehavior >= 0)
	{
		this->tryEdit([&]
		{
			rBehaviors.revertBoundAreaBehavior(static_cast<size_t>(revertBehavior));
			rBehaviors.saveBoundAreas();
		});
	}
	if(customize >= 0)
	{
		this->tryEdit([&]
		{
			rBehaviors.customizeScriptBehavior(static_cast<size_t>(customize));
			rBehaviors.saveBoundAreas();
		});
	}

	/* The rename popup of tags and behaviours (renameKind 1 / 2): opened here, outside the rows' ID scopes, with the text the row put in renameText. */
	if(openRename) ImGui::OpenPopup("RenameBoundArea");
	if(ImGui::BeginPopup("RenameBoundArea"))
	{
		ImGui::InputText("New name", this->renameText, sizeof(this->renameText));
		if(ImGui::Button("Rename"))
		{
			this->tryEdit([&]
			{
				if(this->renameKind == 1) rBehaviors.renameBoundAreaTag(this->renameTag, this->renameText);
				else rBehaviors.renameBoundAreaBehavior(this->renameIndex, this->renameText);
				rBehaviors.saveBoundAreas();
			});
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
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
	if(forPrefab_)
	{
		PoolId prefabId = getEntity(entityId_).prefabId;
		this->tryEdit([&] { addPrefabBehavior(prefabId, behaviorId_, scopeOf(this->allLevels, prefabId)); });
	}
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
The external fields (EntityBehavior::fields, the ones declared external = true) of one behaviour of the entity, under its row: a number, bool, string or vec3 is a plain widget; an entity field takes an entity dragged from the list ("ENTITY" payload: the packed PoolId, as EntityListPanel
makes it), a prefab field a prefab dragged from the Prefabs tab ("PREFAB" payload: its PoolId). A value is stored on the entity (Entity::externals) only once its widget was touched; Reset forgets it, back to the declared default.
*/
void InspectorPanel::drawFields(PoolId entityId_, PoolId behaviorId_)
{
	Entity& rEntity = getEntity(entityId_);
	const EntityBehavior& rBehavior = Globals::Game::entityBehaviorTypes[behaviorId_];
	const std::string& rBehaviorName = Globals::Game::entityBehaviorTypes.names[behaviorId_.id];
	for(const ExternalField& rField : rBehavior.fields)
	{
		if(!rField.external) continue;
		ExternalValue value = rField.defaultValue;
		bool isSet = false;
		auto set = rEntity.externals.find(rBehaviorName);
		if(set != rEntity.externals.end())
		{
			auto found = set->second.find(rField.name);
			if(found != set->second.end())
			{
				value = found->second;
				isSet = true;
			}
		}

		ImGui::PushID(rField.name.c_str());
		bool changed = false;
		switch(rField.type)
		{
		case ExternalType::Number:
			changed = ImGui::DragScalar(rField.name.c_str(), ImGuiDataType_Double, &value.number, 0.05f);
			break;
		case ExternalType::Bool:
			changed = ImGui::Checkbox(rField.name.c_str(), &value.flag);
			break;
		case ExternalType::String:
		{
			char buffer[256];
			std::snprintf(buffer, sizeof(buffer), "%s", value.text.c_str());
			if(ImGui::InputText(rField.name.c_str(), buffer, sizeof(buffer)))
			{
				value.text = buffer;
				changed = true;
			}
			break;
		}
		case ExternalType::Vec3:
			changed = ImGui::DragFloat3(rField.name.c_str(), &value.vector.x, 0.05f);
			break;
		case ExternalType::Entity:
		{
			bool exists = entityExists(value.entity);
			ImGui::Button(exists ? Globals::Game::entities.names[value.entity.id].c_str() : "(drop an entity)", ImVec2(ImGui::GetContentRegionAvail().x * 0.6f, 0.0f));
			if(ImGui::BeginDragDropTarget())
			{
				if(const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload("ENTITY"))
				{
					uint64_t packed = *static_cast<const uint64_t*>(pPayload->Data);
					value.entity = PoolId(static_cast<uint32_t>(packed & 0xFFFFFFFFu), static_cast<uint32_t>(packed >> 32));
					changed = true;
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SameLine();
			if(exists && ImGui::SmallButton("Clear"))
			{
				value.entity = UninitializedPoolId;
				changed = true;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(rField.name.c_str());
			break;
		}
		case ExternalType::Prefab:
		{
			auto& rPrefabs = Globals::Game::prefabs;
			bool exists = value.prefab.id < rPrefabs.objects.size() && rPrefabs.alive[value.prefab.id] && rPrefabs.generation[value.prefab.id] == value.prefab.generation;
			ImGui::Button(exists ? rPrefabs.names[value.prefab.id].c_str() : "(drop a prefab)", ImVec2(ImGui::GetContentRegionAvail().x * 0.6f, 0.0f));
			if(ImGui::BeginDragDropTarget())
			{
				if(const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload("PREFAB"))
				{
					value.prefab = *static_cast<const PoolId*>(pPayload->Data);
					changed = true;
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SameLine();
			if(exists && ImGui::SmallButton("Clear"))
			{
				value.prefab = UninitializedPoolId;
				changed = true;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(rField.name.c_str());
			break;
		}
		}
		if(changed) rEntity.externals[rBehaviorName][rField.name] = value;
		if(isSet)
		{
			ImGui::SameLine();
			if(ImGui::SmallButton("Reset"))
			{
				rEntity.externals[rBehaviorName].erase(rField.name);
				if(rEntity.externals[rBehaviorName].empty()) rEntity.externals.erase(rBehaviorName);
			}
		}
		ImGui::PopID();
	}
}

/*
The behaviours in the order they run each gameplay tick, in two boxes: "This entity" (Remove takes one off this entity; Remove from all takes it off every entity of the prefab) and, for an entity made from a prefab, "Every entity of <prefab>"
(what the editor gave to or took from all of them, which new entities get too). Each box has an Add combo and takes a script dropped on it.
*/
void InspectorPanel::drawBehaviour(PoolId entityId_)
{
	PoolId prefabId = getEntity(entityId_).prefabId;
	PrefabScope scope = scopeOf(this->allLevels, prefabId);
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
			if(ImGui::SmallButton("Remove from all")) this->tryEdit([&] { removePrefabBehavior(prefabId, ids[i], scope); });
		}
		ImGui::Indent();
		this->drawFields(entityId_, ids[i]);
		ImGui::Unindent();
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
		this->drawScopeSwitch(prefabId);
		ImGui::BeginChild("PrefabBehaviours", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		std::vector<PoolId> added = editsOf(prefabId, scope).addedBehaviors;
		std::vector<PoolId> removed = editsOf(prefabId, scope).removedBehaviors;
		if(added.empty() && removed.empty()) ImGui::TextDisabled("nothing added or removed");
		ImGui::PushID("added");
		for(uint32_t i = 0; i < added.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::Text("+ %s", rTypes.names[added[i].id].c_str());
			ImGui::SameLine();
			if(ImGui::SmallButton("Undo")) this->tryEdit([&] { removePrefabBehavior(prefabId, added[i], scope); });
			ImGui::PopID();
		}
		ImGui::PopID();
		ImGui::PushID("removed");
		for(uint32_t i = 0; i < removed.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::Text("- %s", rTypes.names[removed[i].id].c_str());
			ImGui::SameLine();
			if(ImGui::SmallButton("Undo")) this->tryEdit([&] { addPrefabBehavior(prefabId, removed[i], scope); });
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
		else
		{
			PoolId prefabId = getEntity(entityId_).prefabId;
			uint32_t failed = setPrefabMesh(prefabId, rChoice_, scopeOf(this->allLevels, prefabId));
			if(failed != 0) this->message = std::format("{} entities of the prefab could not use it (the first reason is on stderr).", failed);
		}
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
	/* The slot shows the model in use: the external one when it is set, otherwise the instance's own disc model (what discMesh reports, also after a Disc choice). The prefab's box has no single model when it is on Default. */
	auto describe = [&](const MeshChoice& rChoice_, bool forPrefab_)
	{
		if(rChoice_.kind == MeshChoiceKind::External) return std::format("External: {}", rChoice_.name);
		if(forPrefab_ && rChoice_.kind == MeshChoiceKind::Default) return std::string("Default (each entity keeps its own)");
		std::string used = "none";
		try
		{
			used = rEntity.discMesh();
		}
		catch(const std::runtime_error&)
		{
		}
		return std::format("{}{}", used, rChoice_.kind == MeshChoiceKind::Default ? " (default)" : "");
	};
	auto drawBox = [&](bool forPrefab_)
	{
		const MeshChoice& rChoice = forPrefab_ ? editsOf(prefabId, scopeOf(this->allLevels, prefabId)).mesh : rEntity.meshChoice;
		ImGui::TextUnformatted(describe(rChoice, forPrefab_).c_str());
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
		this->drawScopeSwitch(prefabId);
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
		else
		{
			PoolId prefabId = getEntity(entityId_).prefabId;
			uint32_t failed = setPrefabTexture(prefabId, rChoice_, scopeOf(this->allLevels, prefabId));
			if(failed != 0) this->message = std::format("{} entities of the prefab could not use it (the first reason is on stderr).", failed);
		}
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
	/* The slot shows the texture in use: the external one when it is set, otherwise the instance's own disc texture (what discTexture reports, also after a Disc choice; empty = untextured). The prefab's box has no single texture when it is on Default. */
	auto describe = [&](const TextureChoice& rChoice_, bool forPrefab_)
	{
		if(rChoice_.kind == TextureChoiceKind::External) return std::format("External: {}", rChoice_.name);
		if(forPrefab_ && rChoice_.kind == TextureChoiceKind::Default) return std::string("Default (each entity keeps its own)");
		std::string used = "none";
		try
		{
			used = rEntity.discTexture();
			if(used.empty()) used = "none (untextured)";
		}
		catch(const std::runtime_error&)
		{
		}
		return std::format("{}{}", used, rChoice_.kind == TextureChoiceKind::Default ? " (default)" : "");
	};
	auto drawBox = [&](bool forPrefab_)
	{
		const TextureChoice& rChoice = forPrefab_ ? editsOf(prefabId, scopeOf(this->allLevels, prefabId)).texture : rEntity.textureChoice;
		ImGui::TextUnformatted(describe(rChoice, forPrefab_).c_str());
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
		this->drawScopeSwitch(prefabId);
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
	if(ImGui::DragFloat3("Position", &position.x, 0.5f))
		getEntity(entityId_).setPosition(position);
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
