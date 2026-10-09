#pragma once
#include "Entity.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include "PoolId.h"
#include "TextureChoice.h"
#include "MeshChoice.h"
#include "CollisionShape.h"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Ninja
{
struct SAPlacement;
}

/* The folders the editor sorts prefabs into, inside each game's folder. */
enum class PrefabGroup
{
	Enemies,
	Npcs,
	Characters,
	StaticObjects,
	InteractibleObjects
};

/*
How many entities one placement record of a Shadow object makes, and where they are (see RingLine.h): Single is one per model of the first variant at the record's position, RingLine is the layout of the ring object (a record
is a count of rings along a line, or the other shapes of the game's ring code), Variant is for objects whose record names one of the BIN's variants: the u32 at the start of its parameters is an index into Prefab::variants (observed
on the stage art objects; hypothesis that it is the index of the .BIN's info), and the record makes one entity per model of that variant.
*/
enum class PlacementLayout
{
	Single,
	RingLine,
	Variant
};

/* What spawnPrefab (API.h) makes an entity of a prefab from. */
enum class PrefabKind
{
	SAObject,
	ONEObject,
	SonicPlayer,
	ShadowPlayer,
	External
};

/* Which set of a prefab's edits an edit goes to (Prefab::changed / Prefab::level). Global is saved at once to the prefab's Changed file (editor only) and is only for the prefabs built from a key, not External ones. */
enum class PrefabScope
{
	Level,
	Global
};

/*
One set of edits to every entity of a prefab. Behaviours (Globals::Game::entityBehaviorTypes) given to every entity and ones taken from every entity. The same for bound areas: shapes (compared by value, all fields) given and taken. The texture and the model every entity gets
(Default = none); the model is applied before the texture (a texture is checked against the model it is drawn on).
*/
struct PrefabEdits
{
	std::vector<PoolId> addedBehaviors;
	std::vector<PoolId> removedBehaviors;
	std::vector<Physics::CollisionShape> addedShapes;
	std::vector<Physics::CollisionShape> removedShapes;
	TextureChoice texture;
	MeshChoice mesh;
};

/*
One rotation of an SADX object's display code (see Prefab::rotation): about axis ('X', 'Y' or 'Z') by the task's angle `field` (0 x, 1 y, 2 z, in BAMS), negated if negate, plus addend. A step with
field 3 uses no angle: it turns by addend alone.
*/
struct SARotationStep
{
	char axis;
	uint8_t field;
	int32_t addend;
	bool negate;
};

/*
A prefab is a kind of entity the engine can make, built from the entity resource keys (SAEntityKey, STH2005::EntityKey) of the generated tables a level file can name, plus the two player characters. Every entity
is made from one with spawnPrefab (API.h), which remembers it in Entity::prefabId; the editor lists the prefabs by game and group, and groups the entities by their prefab.
It is registered once, by registerPrefabs, in Globals::Game::prefabs under the name "<game>/<key name>" ("SADX/SPRING", "STH2005/SPRING"; the games' folders are SADX and STH2005), which is also the name scripts use (sf.prefabBehavior).
saKey / oneKey point into the generated tables, which live for the whole run. name is the key's name, for reading.
hasModel, hasCollision and hasBehaviour say what the prefab is made of: a model it is drawn with, collision shapes the key lists (the player characters get theirs from DebugSetup), and a script behaviour registered for it (sf.prefabBehavior; the characters get theirs from attachPlayer). The editor draws a prefab without a
model as a cube: dark blue if it only has collision, dark red if it only has behaviour, dark purple if it has both and dark grey if it has neither (PrefabThumbnails).
group is a hypothesis of mine, from External/Prefabs/Groups.json (StaticObjects where it lists nothing): the keys do not say what an object is. layout, variants and loopMotion (Shadow objects) come from External/Prefabs/STH2005/Placement.json, made by tools/genEntityKeys.py
(Loading::loadPrefabData reads both after registerPrefabs): see PlacementLayout; variants lists, per variant (the .BIN's info), the indices into the key's models of the models it is made of, and the collision with the variant's index (if any) goes to the first of them (empty: model 0 alone);
loopMotion makes the motion the entity starts on loop (set for living things, enemies and characters, whose idle goes on) instead of playing once.
SADX objects have rotation and variantField from External/Prefabs/SADX/Placement.json (also made by tools/genSAEntityKeys.py; an object it does not list has the defaults): rotation is the order and angles the object's display code turns by when it is not the usual z, x, y of the three angles (empty = the usual; found by the generator from the object's code),
and variantField is the component of the record's scale (0 x, 1 y, 2 z) that, as a byte modulo the count, picks the key's variantModels / variantCollisions entry (O PARASOL by x, O PIER by y).
An External prefab (PrefabKind::External, game GLTF_FILE) is made by the user in the editor's Prefabs tab, in the "Global" folder (every level) or the "Level" folder (the open level's), under the pool name "Global/<name>" / "Level/<name>"; it has no key and starts empty. Its
entities are External-only (Entity::setExternalOnly): drawn by an external model alone. folder is where the tab lists it ("" = the top, else "a/b"); inLevel says which of the two it is. Its model and texture (mesh, texture) are part of its definition, saved with the prefab
(Loading/ExternalPrefabs.h), not with the level's edits; the other edits (behaviours, shapes) are the level's like any prefab's (no Changed file).
*/
struct Prefab
{
	std::string name;
	SourceEngineEnum game = SA_NINJA;
	PrefabGroup group = PrefabGroup::StaticObjects;
	PrefabKind kind = PrefabKind::SAObject;
	const SAEntityKey* saKey = nullptr;
	const STH2005::EntityKey* oneKey = nullptr;
	bool hasModel = false;
	bool hasCollision = false;
	bool hasBehaviour = false;
	PlacementLayout layout = PlacementLayout::Single;
	std::vector<std::vector<uint32_t>> variants;
	bool loopMotion = false;
	std::vector<SARotationStep> rotation;
	uint8_t variantField = 0;
	/* What the editor changed for every entity of the prefab: "changed" in every level (the prefab's Changed file, read at startup, Loading::loadChangedPrefabs), "source" in the open level's Source Game Level (Levels/<level>/SourceGamePrefabs/<prefab name>/prefab.json, read at each level load by Loading::loadSourcePrefabs, written by File > Save Source Game Level), "level" in the open level (the level file's "prefabs", cleared at each level load). An entity gets the game's data, then changed, then source, then level (spawnPrefab). An External prefab has no changed set; its model and texture are level.mesh and level.texture (its definition, see above). */
	PrefabEdits changed;
	PrefabEdits source;
	PrefabEdits level;
	/* External prefabs only (see above). */
	bool inLevel = false;
	std::string folder;
};

/*
What differs from one entity of a prefab to the next. name is the entity's name (the prefab's if empty), what is where it comes from, for a message when it can't be made. The rest is for the kinds that use it, and a null pointer means the prefab's own:
SADX objects: the model and collision tables to use instead of the key's (the variant a SET record picks) and the SET record (given to the script that sets the object up). Shadow objects: modelIndex, the index into EntityKey::models, and
the collision shapes (a pointer to an empty vector: none).
*/
struct SpawnParams
{
	glm::vec3 position = glm::vec3(0.0f);
	glm::mat4 rotation = glm::mat4(1.0f);
	std::string name;
	std::string what;
	const SAModelKey* pModel = nullptr;
	const std::vector<SACollisionShapesKey>* pCollisions = nullptr;
	const Ninja::SAPlacement* pPlacement = nullptr;
	uint32_t modelIndex = 0;
	const std::vector<STH2005::CollisionKey>* pONECollisions = nullptr;
};

/* Adds a prefab for every entity key of the generated tables (Loading::forEachSAEntityKey / forEachONEEntityKey) and for the two players. Once, after the scripts have registered their behaviours (hasBehaviour reads them). */
void registerPrefabs();

/*
Adds an External prefab under "Global/<name>" or "Level/<name>" (inLevel_) with the model and texture it is defined with (Default or External choices; setPrefabMesh later changes them). Throws std::runtime_error if that name is taken. It is only registered here:
Loading/ExternalPrefabs.h reads and writes the files and is what the editor calls.
*/
PoolId addExternalPrefab(bool inLevel_, const std::string& name_, const std::string& folder_, const MeshChoice& rMesh_, const TextureChoice& rTexture_);

/* Removes the prefabs of the "Level" folder from the pool (the level that owned them is being removed, with its entities). */
void removeLevelPrefabs();

/* Counts every change of the prefab pool and of what the tabs show of a prefab (added, removed, its model or texture set): a list or picture built from them is stale when it differs from the value it was built at. */
uint32_t prefabRevision();

/* The prefab of a game with that key name (SAEntityKey::name, EntityKey::name; "Sonic" and "Shadow" for the players), UninitializedPoolId if there is none. */
PoolId findPrefab(SourceEngineEnum game_, const char* keyName_);

/*
The edit functions below change the set of the scope (Level: Prefab::level, Global: Prefab::changed, also saved at once to the prefab's Changed file, EDITOR only; Global throws std::runtime_error for an External prefab or if the file can't be written) and every entity of the prefab now.
A Global edit leaves the entities alone where the level's set decides the same thing (a level's removed behaviour, its model).

Gives the behaviour to every entity of the prefab that has not got it, now and in every entity spawnPrefab makes later (addedBehaviors); undoes an earlier removePrefabBehavior of it in that set. */
void addPrefabBehavior(PoolId prefabId_, PoolId behaviorId_, PrefabScope scope_ = PrefabScope::Level);

/* Takes the behaviour from every entity of the prefab, now and from every entity spawnPrefab makes later (removedBehaviors, applied after the prefab's script setup); undoes an earlier addPrefabBehavior of it in that set. */
void removePrefabBehavior(PoolId prefabId_, PoolId behaviorId_, PrefabScope scope_ = PrefabScope::Level);

/* Gives a bound area shape to every entity of the prefab, now and to every entity spawnPrefab makes later (addedShapes); a shape equal to one removePrefabShape took only cancels that removal (on the entities that lack an equal one). An entity without a collision gets a hand-made one (setEntityShapes). */
void addPrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_, PrefabScope scope_ = PrefabScope::Level);

/* Takes the first shape equal to this one off every entity of the prefab, now and from every entity spawnPrefab makes later (removedShapes); a shape equal to one addPrefabShape added only cancels that addition. */
void removePrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_, PrefabScope scope_ = PrefabScope::Level);

/* The prefab's shape edits applied to a list of shapes: for changed, then source, then level, each removed shape takes the first equal one off, then the added ones go on the end (so a level's removal can take a shape the Changed file added). */
void applyPrefabShapesTo(const Prefab& rPrefab_, std::vector<Physics::CollisionShape>& rShapes_);

/* The prefab's behaviour edits applied to a list of behaviours, in the same order (changed, then source, then level; each its added, then its removed). */
void applyPrefabBehaviorsTo(const Prefab& rPrefab_, std::vector<PoolId>& rBehaviors_);

/* The model / texture every entity of the prefab gets: the level's if it is set, else the source level's, else the Changed file's. */
MeshChoice prefabMesh(const Prefab& rPrefab_);
TextureChoice prefabTexture(const Prefab& rPrefab_);

/* Gives every entity of the prefab this texture (setEntityTexture), now and to every entity spawnPrefab makes later (the set's texture); it replaces the choice of an entity that had its own. An entity it does not fit (a texture set that lacks a texture the model uses, a Shadow archive without the entry) is left as it was, and the first reason is printed on stderr; returns how many entities that was. */
uint32_t setPrefabTexture(PoolId prefabId_, const TextureChoice& rChoice_, PrefabScope scope_ = PrefabScope::Level);

/* Gives every entity of the prefab this model (setEntityMesh), now and to every entity spawnPrefab makes later (the set's mesh); it replaces the choice of an entity that had its own. An entity it does not fit is left as it was, the first reason is printed on stderr; returns how many entities that was. */
uint32_t setPrefabMesh(PoolId prefabId_, const MeshChoice& rChoice_, PrefabScope scope_ = PrefabScope::Level);

/* Empties the level set (Prefab::level) of every prefab, not the Changed or source ones, without touching any entity (the level load starts from the game's data; the editor's restore puts the entities back itself). */
void clearPrefabEdits();

/* Applies the behaviour edits (changed, source, then level) to one entity just made from the prefab (spawnPrefab). */
void applyPrefabBehaviors(PoolId prefabId_, PoolId entityId_);

/*
The edits of a level set on top of a source set as one set that does the same when applied to a prefab's data (Save Source Game Level writes it as the next source): a removal of something the source added cancels that addition, an addition of something the source removed cancels that removal; the level's model and texture win where set.
*/
PrefabEdits mergePrefabEdits(const PrefabEdits& rSource_, const PrefabEdits& rLevel_);

/* Applies the shape edits to one entity just made from the prefab (spawnPrefab); nothing when the prefab has none. */
void applyPrefabShapes(PoolId prefabId_, PoolId entityId_);

/* Gives one entity just made from the prefab the prefab's texture (prefabTexture; spawnPrefab); nothing when it has none, a failure is printed on stderr. */
void applyPrefabTexture(PoolId prefabId_, PoolId entityId_);

/* Gives one entity just made from the prefab the prefab's model (prefabMesh; spawnPrefab); nothing when it has none, a failure is printed on stderr. */
void applyPrefabMesh(PoolId prefabId_, PoolId entityId_);

/* "SADX" / "STH2005" (the prefab names' prefix) and the folder names the editor shows. */
const char* prefabGameName(SourceEngineEnum game_);
const char* prefabGameFolder(SourceEngineEnum game_);
const char* prefabGroupFolder(PrefabGroup group_);
