#pragma once
#include "Entity.h"
#include "SAKeys.h"
#include "GLTFKeys.h"
#include "Camera.h"
#include "PoolId.h"
#include "CollisionShape.h"
#include "Constants.h"
#include <vector>
#include <glm/glm.hpp>

/*
A level is the static world geometry of a stage, the counterpart of Entity for things without behaviour.
It is created first and gets its instance afterwards, with setSAGPUInstance (an SADX land table) or setONEGPUInstance
(a Shadow stage), both from the keys of ResourceKeys.h: whatever the keys name that is not loaded yet is loaded then.
setGLTFGPUInstance (a level of glTF files, GLTFKeys.h, sourceEngine GLTF_FILE; it has no entities of its own: setSAEntities / setONEEntities are not called for it). Only one of the three can be called, once. Until instanceId is set it is neither updated nor drawn. Removing the level from its pool (destroy) removes the instance and its collision.
collisionId is the PoolId of the level's collision in Globals::Physics::collisions (UninitializedPoolId = none), created by setSACollision / setONECollision / setGLTFCollision (the triangles of the key's collision files, all solid); it is independent of the instance, so a level can have collision without being drawn. Its Collision::position follows the level's position (setPosition, and when the collision is set). Setting or removing it, or moving the level, rebuilds Globals::Physics::quadTree (CollisionSystem::rebuildQuadTree).
setONEEntities creates the objects a Shadow stage places: an Entity (added to Globals::Game::entities, owned by the level and removed in destroy) for every record of the key's placement files whose type id the key lists, made from that EntityKey (its first model and its textures, and its collisions as shapes). Positions are the records' ones with the yaw the level is drawn with and the level's position; the records' rotation is given to the entities (Entity::setRotation); the other models (outside a Variant) are not used and the first motion of a key plays once (looping for the enemies and characters, Prefab::loopMotion). A record whose type the key does not list gets a marker (a cross, see CollisionView::markers) instead of an entity. Only once; an object that fails to load is reported on stderr and skipped.
setSAEntities is the same for an SADX stage: an Entity for every record of the land table key's SET files whose type the key lists, made from that SAEntityKey (its model, textures, collision shapes and first motion, looping; rotation from the record's BAMS angles, no scale yet), at the record's position, and a marker for the others. The objects the stage's own code creates (Emerald Coast's floating bridge) are not made here: the level file's "source" adds them (Loading::loadLevel). Only once, shared with setONEEntities (one per level).
Both create each entity with spawnPrefab (API.h) from the prefab registered for its key (Prefab.h; registerPrefabs has to have run), which anything else can call to make an object at run time.
The first sourceEntityCount of entityIds are the level's own, made from the game's data (setSAEntities / setONEEntities), in the order of the records; sourcePositions, sourceRotations and sourceScales are their transform as made, before the level file's edits (Loading::loadLevel) are applied, which the editor returns them to (revertSourceEntity, restoreSource). The entities after them were added (the editor's prefab panel, or the file's "added" edits) and are owned by the level like the others.
sourceEngine says which engine the instance is from, as in Entity.
*/
struct Level
{
	SourceEngineEnum sourceEngine = SA_NINJA;
	PoolId instanceId = UninitializedPoolId;
	glm::vec3 position;
	PoolId collisionId = UninitializedPoolId;
	std::vector<PoolId> entityIds;
	uint32_t sourceEntityCount = 0;
	std::vector<glm::vec3> sourcePositions;
	/* The rotation and scale of each source entity as made (taken with sourcePositions). */
	std::vector<glm::mat4> sourceRotations;
	std::vector<glm::vec3> sourceScales;
	/* The behaviours of each source entity as the game's data made them (taken with sourcePositions, before any prefab-wide edit): what restoreSource puts back and what a saved per-entity edit is the difference from. */
	std::vector<std::vector<PoolId>> sourceBehaviors;
	/* The same for the bound area shapes of each source entity. */
	std::vector<std::vector<Physics::CollisionShape>> sourceShapes;

	void setSAGPUInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_, const std::vector<std::string>& rSkippedTextures_);
	void setONEGPUInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_, const std::vector<std::string>& rSkippedEntries_);
	void setGLTFGPUInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_);
	void setPosition(const glm::vec3& rPosition_);
	void setONEEntities(PoolId shaderId_, const STH2005::LevelKey& rLevel_);
	void setSAEntities(PoolId shaderId_, const SALandTableKey& rLandTable_);
	void setSACollision(const SALandTableKey& rLandTable_);
	void setONECollision(const STH2005::LevelKey& rLevel_);
	void setGLTFCollision(const GLTFLevelKey& rLevel_);

	/*
	Editor, glTF levels only (they throw for another engine): replace the visual with the chunks now in the level's folder (the old instance is removed first); replace the collision with the files the key names (none: no collision);
	add one collision file (rName_, which the key's list must already include) to the live collision. All rebuild Globals::Physics::quadTree when they change the collision.
	*/
	void swapGLTFGPUInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_);
	void swapGLTFCollision(const GLTFLevelKey& rLevel_);
	void appendGLTFCollision(const GLTFLevelKey& rLevel_, const std::string& rName_);
	/* Ends the level's own entities: from now on the entities in entityIds are the source ones, their positions are remembered. */
	void markSourceEntities();

	/* The index of the entity among the level's own entities, -1 if it is not one of them (added, or not the level's). */
	int32_t sourceIndexOf(PoolId entityId_) const;

	/*
	The number a level file (and Editor::KeptEntity) refers to an entity of the level by in its external field values: a level's own entity is its index in entityIds; an added one is sourceEntityCount plus the number of added entities (those with a prefab) before it,
	the order the file's "added" list makes them again. -1 if it is none of the level's. resolveExternalEntities turns the entityIndex of the entity references of the level's entities (ExternalValue) into the entities, none if the index is past the end or its entity is gone.
	*/
	int32_t editIndexOf(PoolId entityId_) const;
	void resolveExternalEntities();

	/* The prefabs (Globals::Game::prefabs) the level's entities hold in prefab fields (ExternalValue::prefab) that still exist, each once: they belong to the level for the editor's Level folders although no entity is made from them yet. */
	std::vector<PoolId> referencedPrefabs() const;

	/* Puts a level's own entity back where the game's data has it. */
	void revertSourceEntity(uint32_t index_);

	/* Everything as the game's data has it: the own entities go back (position, behaviours, shapes, model and texture), the added ones are removed, and the prefabs lose their behaviour, shape and texture edits (clearPrefabEdits). */
	void restoreSource();
	void update(float deltaTime_);
	void draw(const Camera& rCamera_);
	void destroy();
};
