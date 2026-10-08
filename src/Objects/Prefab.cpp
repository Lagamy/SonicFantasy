#include "Prefab.h"
#include "Globals.h"
#include "API.h"
#include "LevelLoader.h"
#include "Scripting.h"
#include "ExternalPrefabs.h"
#include <algorithm>
#include <cstdio>
#include <format>
#include <set>
#include <string_view>
#include <utility>

static uint32_t prefabRevisionCounter = 0;

uint32_t prefabRevision()
{
	return prefabRevisionCounter;
}

const char* prefabGameName(SourceEngineEnum game_)
{
	return game_ == SA_NINJA ? "SADX" : "STH2005";
}

const char* prefabGameFolder(SourceEngineEnum game_)
{
	return game_ == SA_NINJA ? "Sonic Adventure Director's Cut" : "Shadow The Hedgehog 2005";
}

const char* prefabGroupFolder(PrefabGroup group_)
{
	switch(group_)
	{
	case PrefabGroup::Enemies: return "Enemies";
	case PrefabGroup::Npcs: return "NPCs";
	case PrefabGroup::Characters: return "Characters";
	case PrefabGroup::StaticObjects: return "Static objects";
	case PrefabGroup::InteractibleObjects: return "Interactible objects";
	}
	return "";
}

/*
Which group a key name goes in (hypothesis, by what the names and the SA Tools / registry descriptions say they are; anything not listed is a static object). Enemies: SADX's E *, Shadow's GUN_*, BK_* and EGG_*
(the enemies with an archive). Characters: the SET_CHARACTOR people of Shadow's stages. NPCs: Tikal and the Big Froggy. Interactible: what the player touches, uses or breaks.
*/
static PrefabGroup groupOf(SourceEngineEnum game_, std::string_view name_)
{
	static const std::set<std::string_view> saNpcs{"O TIKAL", "S KAERU"};
	static const std::set<std::string_view> saInteractible{
		"RING", "SPRING", "SPRINGB", "O AXPNL", "O IRONB", "O FeBJG", "O TOGE", "O EME P", "O RELEASE", "O SWITCH", "CMN_DRING", "O BALOON", "O ITEMBOX", "O ItemBoxAir",
		"Rocket H", "Rocket HS", "Rocket V", "Rocket VS", "O JPanel", "O Save Point", "O BUBBLE", "O JUMP", "O BGIRO", "O BFLOOT", "S GENE"
	};
	static const std::set<std::string_view> oneCharacters{"Amy", "Charmy", "DoomsEye", "Eggman", "Espio", "Knuckles", "Maria", "Omega", "Rouge", "Sonic", "Tails", "Vector"};
	static const std::set<std::string_view> oneInteractible{
		"BALLOON", "BLASTBREAK", "BOMB", "BREAKOBJ", "CAPTURECAGE", "CONTAINER", "CONTAINERHARD", "CONTAINERSECRET", "CONTAINERUNBREAK", "CONTAINERWEAPON", "DASHPANEL", "DASHRING", "FAN", "FRUIT",
		"GOALRING", "HEALINGUNIT", "HINTRING", "ITEMBOX", "JUMPBOARD", "LONGSPRING", "RING", "ROCKET", "SAVEPOINT", "SECRETDOOR", "SECRETKEY", "SETWEAPON", "SPRING", "SWITCH", "TANK", "TARGETSWITCH",
		"TURNOVER", "UPDOWNREEL", "WEIGHT", "FOOTINGMOVABLE", "CITYLASER", "FALLBUILDING"
	};
	if(game_ == SA_NINJA)
	{
		if(name_.starts_with("E ")) return PrefabGroup::Enemies;
		if(saNpcs.contains(name_)) return PrefabGroup::Npcs;
		if(saInteractible.contains(name_)) return PrefabGroup::InteractibleObjects;
		return PrefabGroup::StaticObjects;
	}
	if(name_.starts_with("GUN_") || name_.starts_with("BK_") || name_.starts_with("EGG_")) return PrefabGroup::Enemies;
	if(oneCharacters.contains(name_)) return PrefabGroup::Characters;
	if(oneInteractible.contains(name_)) return PrefabGroup::InteractibleObjects;
	return PrefabGroup::StaticObjects;
}

PoolId findPrefab(SourceEngineEnum game_, const char* keyName_)
{
	auto found = Globals::Game::prefabs.nameToId.find(std::format("{}/{}", prefabGameName(game_), keyName_));
	return found == Globals::Game::prefabs.nameToId.end() ? UninitializedPoolId : found->second;
}

static bool entityHasBehavior(const Entity& rEntity_, PoolId behaviorId_)
{
	if(rEntity_.behaviorSetId == UninitializedPoolId) return false;
	const std::vector<PoolId>& rIds = Globals::Game::entityBehaviors[rEntity_.behaviorSetId].behaviorIds;
	return std::find(rIds.begin(), rIds.end(), behaviorId_) != rIds.end();
}

/* Every live entity made from the prefab, by pool id (the behaviours given or removed do not add or remove entities). */
static void forEachEntityOfPrefab(PoolId prefabId_, auto&& rDo_)
{
	auto& rEntities = Globals::Game::entities;
	for(uint32_t i = 0; i < rEntities.objects.size(); i++)
	{
		if(!rEntities.alive[i] || rEntities.objects[i].prefabId != prefabId_) continue;
		rDo_(rEntities.objects[i].selfId);
	}
}

static void eraseId(std::vector<PoolId>& rList_, PoolId id_)
{
	rList_.erase(std::remove(rList_.begin(), rList_.end(), id_), rList_.end());
}

void addPrefabBehavior(PoolId prefabId_, PoolId behaviorId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	eraseId(rPrefab.removedBehaviors, behaviorId_);
	if(std::find(rPrefab.addedBehaviors.begin(), rPrefab.addedBehaviors.end(), behaviorId_) == rPrefab.addedBehaviors.end()) rPrefab.addedBehaviors.push_back(behaviorId_);
	forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
	{
		if(!entityHasBehavior(Globals::Game::entities[entityId_], behaviorId_)) giveEntityBehavior(entityId_, behaviorId_);
	});
}

void removePrefabBehavior(PoolId prefabId_, PoolId behaviorId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	eraseId(rPrefab.addedBehaviors, behaviorId_);
	if(std::find(rPrefab.removedBehaviors.begin(), rPrefab.removedBehaviors.end(), behaviorId_) == rPrefab.removedBehaviors.end()) rPrefab.removedBehaviors.push_back(behaviorId_);
	forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
	{
		while(removeEntityBehavior(entityId_, behaviorId_)) {}
	});
}

void addPrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	auto removed = std::find(rPrefab.removedShapes.begin(), rPrefab.removedShapes.end(), rShape_);
	bool cancelsRemoval = removed != rPrefab.removedShapes.end();
	if(cancelsRemoval) rPrefab.removedShapes.erase(removed);
	else rPrefab.addedShapes.push_back(rShape_);
	forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
	{
		std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);
		if(cancelsRemoval && std::find(shapes.begin(), shapes.end(), rShape_) != shapes.end()) return;
		shapes.push_back(rShape_);
		setEntityShapes(entityId_, shapes);
	});
}

void removePrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	auto added = std::find(rPrefab.addedShapes.begin(), rPrefab.addedShapes.end(), rShape_);
	if(added != rPrefab.addedShapes.end()) rPrefab.addedShapes.erase(added);
	else rPrefab.removedShapes.push_back(rShape_);
	forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
	{
		std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);
		auto found = std::find(shapes.begin(), shapes.end(), rShape_);
		if(found == shapes.end()) return;
		shapes.erase(found);
		setEntityShapes(entityId_, shapes);
	});
}

void applyPrefabShapesTo(const Prefab& rPrefab_, std::vector<Physics::CollisionShape>& rShapes_)
{
	for(const Physics::CollisionShape& rShape : rPrefab_.removedShapes)
	{
		auto found = std::find(rShapes_.begin(), rShapes_.end(), rShape);
		if(found != rShapes_.end()) rShapes_.erase(found);
	}
	rShapes_.insert(rShapes_.end(), rPrefab_.addedShapes.begin(), rPrefab_.addedShapes.end());
}

void clearPrefabEdits()
{
	auto& rPrefabs = Globals::Game::prefabs;
	for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
	{
		if(!rPrefabs.alive[i]) continue;
		rPrefabs.objects[i].addedBehaviors.clear();
		rPrefabs.objects[i].removedBehaviors.clear();
		rPrefabs.objects[i].addedShapes.clear();
		rPrefabs.objects[i].removedShapes.clear();
		/* An External prefab's model and texture are its definition, not an edit of the level. */
		if(rPrefabs.objects[i].kind == PrefabKind::External) continue;
		rPrefabs.objects[i].texture = TextureChoice{};
		rPrefabs.objects[i].mesh = MeshChoice{};
	}
}

void applyPrefabBehaviors(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	for(PoolId behaviorId : rPrefab.addedBehaviors)
	{
		if(!entityHasBehavior(Globals::Game::entities[entityId_], behaviorId)) giveEntityBehavior(entityId_, behaviorId);
	}
	for(PoolId behaviorId : rPrefab.removedBehaviors)
	{
		while(removeEntityBehavior(entityId_, behaviorId)) {}
	}
}

void applyPrefabShapes(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	if(rPrefab.addedShapes.empty() && rPrefab.removedShapes.empty()) return;
	std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);
	applyPrefabShapesTo(rPrefab, shapes);
	setEntityShapes(entityId_, shapes);
}

/* An External prefab's model or texture changed: its picture and listing are stale, and the definition is written to its file (the editor only; the game never edits). */
static void externalPrefabChanged(PoolId prefabId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	rPrefab.hasModel = rPrefab.mesh.kind != MeshChoiceKind::Default;
	prefabRevisionCounter++;
#ifdef EDITOR
	Loading::saveExternalPrefabs(rPrefab.inLevel ? Loading::ExternalScope::Level : Loading::ExternalScope::Project);
#endif
}

uint32_t setPrefabTexture(PoolId prefabId_, const TextureChoice& rChoice_)
{
	Globals::Game::prefabs[prefabId_].texture = rChoice_;
	uint32_t failed = 0;
	forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
	{
		if(!Globals::Game::entities[entityId_].hasModelSlot()) return;
		try
		{
			setEntityTexture(entityId_, rChoice_);
		}
		catch(const std::runtime_error& rError)
		{
			if(failed == 0) std::fprintf(stderr, "Texture: %s %s: %s\n", Globals::Game::prefabs[prefabId_].name.c_str(), Globals::Game::entities.getName(entityId_).c_str(), rError.what());
			failed++;
		}
	});
	if(Globals::Game::prefabs[prefabId_].kind == PrefabKind::External) externalPrefabChanged(prefabId_);
	return failed;
}

void applyPrefabTexture(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	if(rPrefab.texture.kind == TextureChoiceKind::Default) return;
	try
	{
		setEntityTexture(entityId_, rPrefab.texture);
	}
	catch(const std::runtime_error& rError)
	{
		std::fprintf(stderr, "Texture: %s: %s\n", rPrefab.name.c_str(), rError.what());
	}
}

uint32_t setPrefabMesh(PoolId prefabId_, const MeshChoice& rChoice_)
{
	Globals::Game::prefabs[prefabId_].mesh = rChoice_;
	uint32_t failed = 0;
	forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
	{
		if(!Globals::Game::entities[entityId_].hasModelSlot()) return;
		try
		{
			setEntityMesh(entityId_, rChoice_);
		}
		catch(const std::runtime_error& rError)
		{
			if(failed == 0) std::fprintf(stderr, "Model: %s %s: %s\n", Globals::Game::prefabs[prefabId_].name.c_str(), Globals::Game::entities.getName(entityId_).c_str(), rError.what());
			failed++;
		}
	});
	if(Globals::Game::prefabs[prefabId_].kind == PrefabKind::External) externalPrefabChanged(prefabId_);
	return failed;
}

void applyPrefabMesh(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	if(rPrefab.mesh.kind == MeshChoiceKind::Default) return;
	try
	{
		setEntityMesh(entityId_, rPrefab.mesh);
	}
	catch(const std::runtime_error& rError)
	{
		std::fprintf(stderr, "Model: %s: %s\n", rPrefab.name.c_str(), rError.what());
	}
}

/* Adds the prefab under "<game>/<name>"; a name that is already registered keeps the first. hasBehaviour also counts a behaviour a script registered for that name. */
static void addPrefab(Prefab prefab_)
{
	std::string poolName = std::format("{}/{}", prefabGameName(prefab_.game), prefab_.name);
	if(Globals::Game::prefabs.nameToId.contains(poolName)) return;
	if(Scripting::hasPrefabBehavior(poolName.c_str())) prefab_.hasBehaviour = true;
	Globals::Game::prefabs.add(poolName.c_str(), std::move(prefab_));
	prefabRevisionCounter++;
}

PoolId addExternalPrefab(bool inLevel_, const std::string& name_, const std::string& folder_, const MeshChoice& rMesh_, const TextureChoice& rTexture_)
{
	std::string poolName = std::string(inLevel_ ? "Level/" : "Global/") + name_;
	if(Globals::Game::prefabs.nameToId.contains(poolName)) throw std::runtime_error(std::format("the prefab name '{}' is taken.", poolName));
	Prefab prefab;
	prefab.name = name_;
	prefab.game = GLTF_FILE;
	prefab.kind = PrefabKind::External;
	prefab.hasModel = rMesh_.kind != MeshChoiceKind::Default;
	prefab.hasBehaviour = Scripting::hasPrefabBehavior(poolName.c_str());
	prefab.mesh = rMesh_;
	prefab.texture = rTexture_;
	prefab.inLevel = inLevel_;
	prefab.folder = folder_;
	PoolId id = Globals::Game::prefabs.add(poolName.c_str(), std::move(prefab));
	prefabRevisionCounter++;
	return id;
}

void removeLevelPrefabs()
{
	auto& rPrefabs = Globals::Game::prefabs;
	for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
	{
		if(rPrefabs.alive[i] && rPrefabs.objects[i].inLevel) rPrefabs.remove(PoolId(i, rPrefabs.generation[i]));
	}
	prefabRevisionCounter++;
}

void registerPrefabs()
{
	/* The players: no entity key, they are made from the constants of ResourceKeys.h and the hand-made shapes of DebugSetup (spawnPrefab). A script attaches their behaviours (attachPlayer). */
	addPrefab(Prefab{"Sonic", SA_NINJA, PrefabGroup::Characters, PrefabKind::SonicPlayer, nullptr, nullptr, true, true, true});
	addPrefab(Prefab{"Shadow", STH2005_ONE, PrefabGroup::Characters, PrefabKind::ShadowPlayer, nullptr, nullptr, true, true, true});

	Loading::forEachSAEntityKey([](const SAEntityKey& rKey_)
	{
		bool hasModel = rKey_.model.rel != nullptr;
		for(const SAModelKey& rVariant : rKey_.variantModels) hasModel = hasModel || rVariant.rel != nullptr;
		bool hasCollision = !rKey_.collisions.empty() || !rKey_.variantCollisions.empty();
		addPrefab(Prefab{rKey_.name, SA_NINJA, groupOf(SA_NINJA, rKey_.name), PrefabKind::SAObject, &rKey_, nullptr, hasModel, hasCollision, false});
	});
	Loading::forEachONEEntityKey([](const STH2005::EntityKey& rKey_)
	{
		addPrefab(Prefab{rKey_.name, STH2005_ONE, groupOf(STH2005_ONE, rKey_.name), PrefabKind::ONEObject, nullptr, &rKey_, !rKey_.models.empty(), !rKey_.collisions.empty(), false});
	});
}
