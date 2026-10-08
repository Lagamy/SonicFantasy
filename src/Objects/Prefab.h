#pragma once
#include "Entity.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include "PoolId.h"
#include "TextureChoice.h"
#include "MeshChoice.h"
#include "CollisionShape.h"
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

/* What spawnPrefab (API.h) makes an entity of a prefab from. */
enum class PrefabKind
{
	SAObject,
	ONEObject,
	SonicPlayer,
	ShadowPlayer,
	External
};

/*
A prefab is a kind of entity the engine can make, built from the entity resource keys (SAEntityKey, STH2005::EntityKey) of the generated tables a level file can name, plus the two player characters. Every entity
is made from one with spawnPrefab (API.h), which remembers it in Entity::prefabId; the editor lists the prefabs by game and group, and groups the entities by their prefab.
It is registered once, by registerPrefabs, in Globals::Game::prefabs under the name "<game>/<key name>" ("SADX/SPRING", "STH2005/SPRING"; the games' folders are SADX and STH2005), which is also the name scripts use (sf.prefabBehavior).
saKey / oneKey point into the generated tables, which live for the whole run. name is the key's name, for reading.
hasModel, hasCollision and hasBehaviour say what the prefab is made of: a model it is drawn with, collision shapes the key lists (the player characters get theirs from DebugSetup), and a script behaviour registered for it (sf.prefabBehavior; the characters get theirs from attachPlayer). The editor draws a prefab without a
model as a cube: dark blue if it only has collision, dark red if it only has behaviour, dark purple if it has both and dark grey if it has neither (PrefabThumbnails).
group is a hypothesis of mine, from a hand-written table in Prefab.cpp: the keys do not say what an object is.
An External prefab (PrefabKind::External, game GLTF_FILE) is made by the user in the editor's Prefabs tab, in the "Global" folder (every level) or the "Level" folder (the open level's), under the pool name "Global/<name>" / "Level/<name>"; it has no key and starts empty. Its
entities are External-only (Entity::setExternalOnly): drawn by an external model alone. folder is where the tab lists it ("" = the top, else "a/b"); inLevel says which of the two it is. Its model and texture (mesh, texture) are part of its definition, saved with the prefab
(Loading/ExternalPrefabs.h), not with the level's edits; the other edits (behaviours, shapes) are the level's like any prefab's.
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
	/* Behaviours (Globals::Game::entityBehaviorTypes) the editor gave to every entity of the prefab, and ones it took from every entity of it. Saved with the level (Loading::saveLevelEdits, "prefabs"); spawnPrefab applies both after the prefab's script setup. */
	std::vector<PoolId> addedBehaviors;
	std::vector<PoolId> removedBehaviors;
	/* The same for bound areas: shapes (compared by value, all fields) the editor gave to every entity of the prefab, and ones it took from every entity of it. Saved with the level ("prefabs"); spawnPrefab applies them after the script setup. */
	std::vector<Physics::CollisionShape> addedShapes;
	std::vector<Physics::CollisionShape> removedShapes;
	/* The texture the editor gave every entity of the prefab (Default = none); saved with the level ("prefabs"), applied by spawnPrefab after the script setup. */
	TextureChoice texture;
	/* The model the editor gave every entity of the prefab (Default = none); saved with the level ("prefabs"), applied by spawnPrefab before the texture (a texture is checked against the model it is drawn on). */
	MeshChoice mesh;
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

/* Gives the behaviour to every entity of the prefab that has not got it, now and in every entity spawnPrefab makes later (addedBehaviors); undoes an earlier removePrefabBehavior of it. */
void addPrefabBehavior(PoolId prefabId_, PoolId behaviorId_);

/* Takes the behaviour from every entity of the prefab, now and from every entity spawnPrefab makes later (removedBehaviors, applied after the prefab's script setup); undoes an earlier addPrefabBehavior of it. */
void removePrefabBehavior(PoolId prefabId_, PoolId behaviorId_);

/* Gives a bound area shape to every entity of the prefab, now and to every entity spawnPrefab makes later (addedShapes); a shape equal to one removePrefabShape took only cancels that removal (on the entities that lack an equal one). An entity without a collision gets a hand-made one (setEntityShapes). */
void addPrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_);

/* Takes the first shape equal to this one off every entity of the prefab, now and from every entity spawnPrefab makes later (removedShapes); a shape equal to one addPrefabShape added only cancels that addition. */
void removePrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_);

/* The prefab's shape edits applied to a list of shapes: each removed shape takes the first equal one off, then the added ones go on the end. */
void applyPrefabShapesTo(const Prefab& rPrefab_, std::vector<Physics::CollisionShape>& rShapes_);

/* Gives every entity of the prefab this texture (setEntityTexture), now and to every entity spawnPrefab makes later (Prefab::texture); it replaces the choice of an entity that had its own. An entity it does not fit (a texture set that lacks a texture the model uses, a Shadow archive without the entry) is left as it was, and the first reason is printed on stderr; returns how many entities that was. */
uint32_t setPrefabTexture(PoolId prefabId_, const TextureChoice& rChoice_);

/* Gives every entity of the prefab this model (setEntityMesh), now and to every entity spawnPrefab makes later (Prefab::mesh); it replaces the choice of an entity that had its own. An entity it does not fit is left as it was, the first reason is printed on stderr; returns how many entities that was. */
uint32_t setPrefabMesh(PoolId prefabId_, const MeshChoice& rChoice_);

/* Empties the behaviour, shape, texture and model edits of every prefab without touching any entity (the level load starts from the game's data; the editor's restore puts the entities back itself). */
void clearPrefabEdits();

/* Applies addedBehaviors and removedBehaviors to one entity just made from the prefab (spawnPrefab). */
void applyPrefabBehaviors(PoolId prefabId_, PoolId entityId_);

/* Applies addedShapes and removedShapes to one entity just made from the prefab (spawnPrefab); nothing when the prefab has none. */
void applyPrefabShapes(PoolId prefabId_, PoolId entityId_);

/* Gives one entity just made from the prefab the prefab's texture (spawnPrefab); nothing when it has none, a failure is printed on stderr. */
void applyPrefabTexture(PoolId prefabId_, PoolId entityId_);

/* Gives one entity just made from the prefab the prefab's model (spawnPrefab); nothing when it has none, a failure is printed on stderr. */
void applyPrefabMesh(PoolId prefabId_, PoolId entityId_);

/* "SADX" / "STH2005" (the prefab names' prefix) and the folder names the editor shows. */
const char* prefabGameName(SourceEngineEnum game_);
const char* prefabGameFolder(SourceEngineEnum game_);
const char* prefabGroupFolder(PrefabGroup group_);
