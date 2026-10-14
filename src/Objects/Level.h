#pragma once
#include "Entity.h"
#include "SAKeys.h"
#include "GLTFKeys.h"
#include "Camera.h"
#include "PoolId.h"
#include "CollisionShape.h"
#include "Constants.h"
#include "Prefab.h"
#include "TextureChoice.h"
#include "MeshChoice.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

/*
How to make one of a level's entities again without building the level (Level::built, Loading::resetLevel). Placed: from the game's data, by spawnPrefab with the params it was first made with (setSAEntities / setONEEntities; the copies of what its params pointed
at are here: the SET record and a Shadow record's collision keys, the other pointers are into the generated tables, which live for the whole run). Added: an entry of the level file's "source" or "edits" "added" list (fromSource, addedIndex), read again from the file. Camera: an entry of the file's "cameras" list (addedIndex is its place in that list). None: it can't be
made again alone: a reset that finds such an entity changed rebuilds the whole level.
*/
struct EntityRecipe
{
	enum class Kind
	{
		None,
		Placed,
		Added,
		Camera
	};
	Kind kind = Kind::None;
	PoolId prefabId = UninitializedPoolId;
	PoolId shaderId = UninitializedPoolId;
	SpawnParams params;
	std::shared_ptr<const Ninja::SAPlacement> placement;
	std::vector<STH2005::CollisionKey> oneCollisions;
	bool hasOneCollisions = false;
	bool fromSource = false;
	uint32_t addedIndex = 0;
};

/*
The parts of an entity gameplay can change, as the level's build left them (Level::captureBaselines): what Level::entityChanged compares the entity with. The Lua state is not here: Entity::scriptStateDirty says it. The rotation and position are the placed ones (the map's place included).
*/
struct EntityBaseline
{
	glm::vec3 position = glm::vec3(0.0f);
	glm::mat4 rotation = glm::mat4(1.0f);
	glm::vec3 scale = glm::vec3(1.0f);
	glm::mat4 modelMatrix = glm::mat4(1.0f);
	std::vector<PoolId> behaviors;
	std::vector<Physics::CollisionShape> shapes;
	MeshChoice mesh;
	TextureChoice texture;
	AnimationIntent animationIntent;
	AnimationIntent blinkIntent;
	float animationRate = 0.0f;
	float animationBlendRate = 0.0f;
	bool behaviorNotCulled = false;

	/* The view state of the entity's camera module (hasCamera), the part of it a script or the editor changes; the render target is the one the build made. */
	bool hasCamera = false;
	glm::vec3 cameraPosition = glm::vec3(0.0f);
	glm::vec3 cameraFront = glm::vec3(0.0f);
	float cameraYaw = 0.0f;
	float cameraPitch = 0.0f;
	float cameraFov = 0.0f;
	float cameraNear = 0.0f;
	float cameraFar = 0.0f;
	bool cameraEnabled = false;
	bool cameraIsMain = false;
	PoolId cameraRenderTargetId = UninitializedPoolId;
};

/* One entity the level's build made: its position in entityIds as built (index, the number the level file's edits name it by), how to make it again and what it was like. */
struct BuiltEntity
{
	uint32_t index = 0;
	EntityRecipe recipe;
	EntityBaseline baseline;
};

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
	/*
	What the build made (the level's own entities, the file's source and edits adds and its cameras; not what the editor adds later), by entity (the key is generation << 32 | id; builtKey). A reset keeps an entity that is unchanged from its baseline and makes the others again
	from their recipe (Loading::resetLevel). builtCount is entityIds.size() when the build ended: entities after it are not the build's. Emptied by destroyEntities.
	*/
	std::map<uint64_t, BuiltEntity> built;
	uint32_t builtCount = 0;
	uint32_t sourceEntityCount = 0;
	std::vector<glm::vec3> sourcePositions;
	/* The rotation and scale of each source entity as made (taken with sourcePositions). */
	std::vector<glm::mat4> sourceRotations;
	std::vector<glm::vec3> sourceScales;
	/* The behaviours of each source entity as the game's data made them (taken with sourcePositions, before any prefab-wide edit): what restoreSource puts back and what a saved per-entity edit is the difference from. */
	std::vector<std::vector<PoolId>> sourceBehaviors;
	/* The same for the bound area shapes of each source entity. */
	std::vector<std::vector<Physics::CollisionShape>> sourceShapes;
	/* The name it was loaded under (Loading::loadLevel; empty for a level made another way): the key of its External assets (Graphics::externalLevels). */
	std::string name;
	/* The level's own PoolId in Globals::Game::levels (set by addLevel), so the entities it makes can name it (Entity::levelId) and its prefab edits can be found and dropped (Prefab::inLevels). */
	PoolId selfId = UninitializedPoolId;
	/*
	The level's place in the map (Map.h): its Map.json entry, applied by Loading::loadLevel after the level and its edits are made. A level keeps its own local coordinates: the world position of a point of the level (its file's position included) is mapMatrix() * the point, and
	the level file's edits are written back in those local coordinates (Loading::saveLevelEdits). mapRotation is Euler angles in degrees, applied as Y, then X, then Z. Identity by default.
	*/
	glm::vec3 mapPosition = glm::vec3(0.0f);
	glm::vec3 mapRotation = glm::vec3(0.0f);
	/*
	The box around the level's drawn geometry (the union of the boxes of its pieces' bounding spheres; computeBounds, when the instance is set) in the level's own space, position and map place excluded. hasBounds is false while there is no instance or it has no
	piece. The editor's Map view picks a level by it (Gizmo).
	*/
	glm::vec3 boundsMin = glm::vec3(0.0f);
	glm::vec3 boundsMax = glm::vec3(0.0f);
	bool hasBounds = false;

	void setSAGPUInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_, const std::vector<std::string>& rSkippedTextures_);
	void setONEGPUInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_, const std::vector<std::string>& rSkippedEntries_);
	void setGLTFGPUInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_);
	void setPosition(const glm::vec3& rPosition_);

	/* Makes boundsMin / boundsMax from the instance's pieces (a Shadow level's with the yaw its pieces are drawn with, ONELevelInstance::draw). Called by the setXGPUInstance functions. */
	void computeBounds();

	/* The matrix of mapPosition and mapRotation (translate * Y * X * Z). */
	glm::mat4 mapMatrix() const;

	/*
	Moves the level to another place in the map: everything it owns goes from the old place to the new one (entities: position and rotation, with the sourcePositions / sourceRotations they are compared with; the collision; the markers it made; the instance
	is drawn with the matrix). Cameras of its entities move but keep their view direction. Rebuilds the quad tree and marks the spatial index dirty. The same call places a level when it is loaded.
	*/
	/* With collision_ false the collision is left where it was (the editor's drag: turning its vertices and rebuilding the quad tree every frame is the cost); collisionStale says it is behind, and commitMapCollision puts it right once. */
	void setMapTransform(const glm::vec3& rPosition_, const glm::vec3& rRotationDegrees_, bool collision_ = true);
	bool collisionStale = false;
	void commitMapCollision();
	/* Puts the collision where the level's place says (turned by mapRotation, at mapPosition + that turn of position), without rebuilding the quad tree. */
	void placeCollision();
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
	/* Adds an entity the build made to entityIds with its recipe (UninitializedPoolId: only the slot, so the indices after it stay as the file counts them). */
	void addBuilt(PoolId entityId_, const EntityRecipe& rRecipe_);
	static uint64_t builtKey(PoolId entityId_) { return (static_cast<uint64_t>(entityId_.generation) << 32) | entityId_.id; }
	/* Takes the baseline of every entity the build made (the build ended; also for an entity made again, with only_ = its id) and sets builtCount. */
	void captureBaselines();
	void captureBaseline(PoolId entityId_);
	/* Whether the entity is not as the build left it: a baseline field differs, it has a physics body or an entity state, its camera's view differs, or a script changed its state (Entity::scriptStateDirty). An entity the build did not make is changed. */
	bool entityChanged(PoolId entityId_) const;
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

	/* Everything as the game's data has it: the own entities go back (position, behaviours, shapes, model and texture), the added ones are removed, and its prefab edits are emptied (clearPrefabEdits; its Source Game Level ones stay). */
	void restoreSource();
	void update(float deltaTime_);
	void draw(const Camera& rCamera_);
	/* The level's entities, the placed-entity markers and the level's prefab edits go; the instance, the collision and the External assets stay. What Loading::resetLevel does (the GPU instance is the cost of a full rebuild); destroy calls it first. */
	void destroyEntities();
	void destroy();
};
