#pragma once
#include "PoolId.h"
#include "TextureChoice.h"
#include "MeshChoice.h"
#include <functional>
#include <string>
#include <vector>

namespace Graphics
{
/*
The inspector of the editor: a panel along the right edge of the window for the entity selected in the EntityListPanel. Its name, then a tab bar with the Transform and one tab per module the entity has (the
handles of Entity that are not UninitializedPoolId): BoundArea (always shown: collisionId's shapes, which the engine tests for intersections and whose tags and interactions decide what an overlap does, so collisions, triggers
and death planes; they can be edited, added and removed, for one entity or its whole prefab), Physics (physicsId), Behaviour (always shown: behaviours can be added to any entity, one entity or all of its prefab, by combo or by dropping a script from the Assets tab), Texture (shown for an entity with a model: the texture it is drawn with, one entity or all of its prefab, from a disc texture dropped from the Assets tab or an external png/jpeg picked from a combo) Mesh (shown for an entity with a model: the model it is drawn with, one entity or all of its prefab, from a disc model dropped from the Assets tab, a .DFF of a Shadow entity's archive or an external model picked from a combo), and Camera (cameraId). Drawn by DebugUI::draw, inside its ImGui frame; Editor::inspector.
*/
struct InspectorPanel
{
	/* The panel's width in pixels: the user drags its left edge, draw() reads the result back, and Editor saves it (editor_settings.json). */
	float width = 380.0f;

	/* The size a "New render target" button of the Cameras tab makes. */
	int newTargetWidth = 512;
	int newTargetHeight = 512;

	void draw(PoolId selected_);

private:
	/* The Behaviour tab's script drop: the behaviours a dropped file registered, kept for the "Pick behaviour" popup when there are several; candidatesForPrefab says whether they go to the whole prefab. */
	std::vector<PoolId> candidates;
	bool candidatesForPrefab = false;
	bool openPick = false;
	std::string message;

	/* The BoundArea tab's "Tags and behaviours" box: the names being typed for a new tag and a new behaviour. */
	char newTagName[64] = "";
	char newBehaviorName[64] = "";

	/* The rename popup of that box: what is being renamed (0 nothing, 1 a tag by name, 2 a behaviour by its index in BoundAreaData::behaviors) and the text typed. */
	int renameKind = 0;
	std::string renameTag;
	size_t renameIndex = 0;
	char renameText[64] = "";

	/* The switch of the "Every entity of <prefab>" boxes (all four tabs): false = the open level's edits (Prefab::level, saved with the level), true = every level's (Prefab::changed, the prefab's Changed file, saved at once). Not for an External prefab. */
	bool allLevels = false;

	void drawScopeSwitch(PoolId prefabId_);
	void tryEdit(const std::function<void()>& rEdit_);
	PoolId pickBehaviour(const char* id_);
	void giveBehaviour(PoolId entityId_, bool forPrefab_, PoolId behaviorId_);
	void acceptScript(PoolId entityId_, bool forPrefab_);
	void drawTransform(PoolId entityId_);
	void drawBoundArea(PoolId entityId_);
	void drawBoundAreaRules();
	void drawPhysics(PoolId entityId_);
	void drawBehaviour(PoolId entityId_);
	void drawFields(PoolId entityId_, PoolId behaviorId_);
	void drawMesh(PoolId entityId_);
	void giveMesh(PoolId entityId_, bool forPrefab_, const MeshChoice& rChoice_);
	void acceptMesh(PoolId entityId_, bool forPrefab_);
	std::string pickExternalModel(const char* id_);
	void drawTexture(PoolId entityId_);
	void giveTexture(PoolId entityId_, bool forPrefab_, const TextureChoice& rChoice_);
	void acceptTexture(PoolId entityId_, bool forPrefab_);
	std::string pickExternalTexture(const char* id_);
	void drawCamera(PoolId entityId_);
};
}
