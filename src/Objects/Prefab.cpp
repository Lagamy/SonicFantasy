#include "Prefab.h"
#include "Globals.h"
#include "API.h"
#include "LevelLoader.h"
#include "Scripting.h"
#include "ExternalPrefabs.h"
#include <algorithm>
#include <cstdio>
#include <format>
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

static bool hasId(const std::vector<PoolId>& rList_, PoolId id_)
{
	return std::find(rList_.begin(), rList_.end(), id_) != rList_.end();
}

/* The set of the scope; Global is refused for an External prefab (its definition and level edits are saved with the level, not in a Changed file). */
static PrefabEdits& editsOf(Prefab& rPrefab_, PrefabScope scope_)
{
	if(scope_ == PrefabScope::Level) return rPrefab_.level;
	if(rPrefab_.kind == PrefabKind::External) throw std::runtime_error(std::format("{} is an External prefab: it has no Changed file.", rPrefab_.name));
	return rPrefab_.changed;
}

/* A Global edit was made: the prefab's Changed file is written (the editor only; the game never edits). */
static void changedEdited(PoolId prefabId_, PrefabScope scope_)
{
#ifdef EDITOR
	if(scope_ == PrefabScope::Global) Loading::saveChangedPrefab(prefabId_);
#endif
}

void addPrefabBehavior(PoolId prefabId_, PoolId behaviorId_, PrefabScope scope_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	PrefabEdits& rEdits = editsOf(rPrefab, scope_);
	eraseId(rEdits.removedBehaviors, behaviorId_);
	if(!hasId(rEdits.addedBehaviors, behaviorId_)) rEdits.addedBehaviors.push_back(behaviorId_);
	bool levelDecides = scope_ == PrefabScope::Global && (hasId(rPrefab.level.removedBehaviors, behaviorId_) || hasId(rPrefab.source.removedBehaviors, behaviorId_));
	if(!levelDecides)
	{
		forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
		{
			if(!entityHasBehavior(Globals::Game::entities[entityId_], behaviorId_)) giveEntityBehavior(entityId_, behaviorId_);
		});
	}
	changedEdited(prefabId_, scope_);
}

void removePrefabBehavior(PoolId prefabId_, PoolId behaviorId_, PrefabScope scope_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	PrefabEdits& rEdits = editsOf(rPrefab, scope_);
	eraseId(rEdits.addedBehaviors, behaviorId_);
	if(!hasId(rEdits.removedBehaviors, behaviorId_)) rEdits.removedBehaviors.push_back(behaviorId_);
	bool levelDecides = scope_ == PrefabScope::Global && (hasId(rPrefab.level.addedBehaviors, behaviorId_) || hasId(rPrefab.source.addedBehaviors, behaviorId_));
	if(!levelDecides)
	{
		forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
		{
			while(removeEntityBehavior(entityId_, behaviorId_)) {}
		});
	}
	changedEdited(prefabId_, scope_);
}

void addPrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_, PrefabScope scope_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	PrefabEdits& rEdits = editsOf(rPrefab, scope_);
	auto removed = std::find(rEdits.removedShapes.begin(), rEdits.removedShapes.end(), rShape_);
	bool cancelsRemoval = removed != rEdits.removedShapes.end();
	if(cancelsRemoval) rEdits.removedShapes.erase(removed);
	else rEdits.addedShapes.push_back(rShape_);
	forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
	{
		std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);
		if(cancelsRemoval && std::find(shapes.begin(), shapes.end(), rShape_) != shapes.end()) return;
		/* A shape of the Changed file goes before the level's added ones, the order a level load gives (applyPrefabShapesTo), so the entity does not look edited. */
		std::vector<Physics::CollisionShape> laterAdded = rPrefab.source.addedShapes;
		laterAdded.insert(laterAdded.end(), rPrefab.level.addedShapes.begin(), rPrefab.level.addedShapes.end());
		size_t levelAdded = laterAdded.size();
		bool endsWithLevelAdded = scope_ == PrefabScope::Global && shapes.size() >= levelAdded && std::equal(laterAdded.begin(), laterAdded.end(), shapes.end() - levelAdded);
		shapes.insert(endsWithLevelAdded ? shapes.end() - levelAdded : shapes.end(), rShape_);
		setEntityShapes(entityId_, shapes);
	});
	changedEdited(prefabId_, scope_);
}

void removePrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_, PrefabScope scope_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	PrefabEdits& rEdits = editsOf(rPrefab, scope_);
	auto added = std::find(rEdits.addedShapes.begin(), rEdits.addedShapes.end(), rShape_);
	if(added != rEdits.addedShapes.end()) rEdits.addedShapes.erase(added);
	else rEdits.removedShapes.push_back(rShape_);
	forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
	{
		std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);
		auto found = std::find(shapes.begin(), shapes.end(), rShape_);
		if(found == shapes.end()) return;
		shapes.erase(found);
		setEntityShapes(entityId_, shapes);
	});
	changedEdited(prefabId_, scope_);
}

static void applyShapeEdits(const PrefabEdits& rEdits_, std::vector<Physics::CollisionShape>& rShapes_)
{
	for(const Physics::CollisionShape& rShape : rEdits_.removedShapes)
	{
		auto found = std::find(rShapes_.begin(), rShapes_.end(), rShape);
		if(found != rShapes_.end()) rShapes_.erase(found);
	}
	rShapes_.insert(rShapes_.end(), rEdits_.addedShapes.begin(), rEdits_.addedShapes.end());
}

void applyPrefabShapesTo(const Prefab& rPrefab_, std::vector<Physics::CollisionShape>& rShapes_)
{
	applyShapeEdits(rPrefab_.changed, rShapes_);
	applyShapeEdits(rPrefab_.source, rShapes_);
	applyShapeEdits(rPrefab_.level, rShapes_);
}

static void applyBehaviorEdits(const PrefabEdits& rEdits_, std::vector<PoolId>& rBehaviors_)
{
	for(PoolId behaviorId : rEdits_.addedBehaviors)
	{
		if(!hasId(rBehaviors_, behaviorId)) rBehaviors_.push_back(behaviorId);
	}
	for(PoolId behaviorId : rEdits_.removedBehaviors) eraseId(rBehaviors_, behaviorId);
}

void applyPrefabBehaviorsTo(const Prefab& rPrefab_, std::vector<PoolId>& rBehaviors_)
{
	applyBehaviorEdits(rPrefab_.changed, rBehaviors_);
	applyBehaviorEdits(rPrefab_.source, rBehaviors_);
	applyBehaviorEdits(rPrefab_.level, rBehaviors_);
}

MeshChoice prefabMesh(const Prefab& rPrefab_)
{
	if(rPrefab_.level.mesh.kind != MeshChoiceKind::Default) return rPrefab_.level.mesh;
	return rPrefab_.source.mesh.kind != MeshChoiceKind::Default ? rPrefab_.source.mesh : rPrefab_.changed.mesh;
}

TextureChoice prefabTexture(const Prefab& rPrefab_)
{
	if(rPrefab_.level.texture.kind != TextureChoiceKind::Default) return rPrefab_.level.texture;
	return rPrefab_.source.texture.kind != TextureChoiceKind::Default ? rPrefab_.source.texture : rPrefab_.changed.texture;
}

void clearPrefabEdits()
{
	auto& rPrefabs = Globals::Game::prefabs;
	for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
	{
		if(!rPrefabs.alive[i]) continue;
		PrefabEdits& rLevel = rPrefabs.objects[i].level;
		rLevel.addedBehaviors.clear();
		rLevel.removedBehaviors.clear();
		rLevel.addedShapes.clear();
		rLevel.removedShapes.clear();
		/* An External prefab's model and texture are its definition, not an edit of the level. */
		if(rPrefabs.objects[i].kind == PrefabKind::External) continue;
		rLevel.texture = TextureChoice{};
		rLevel.mesh = MeshChoice{};
	}
}

PrefabEdits mergePrefabEdits(const PrefabEdits& rSource_, const PrefabEdits& rLevel_)
{
	PrefabEdits merged = rSource_;
	for(PoolId behaviorId : rLevel_.removedBehaviors)
	{
		if(hasId(merged.addedBehaviors, behaviorId)) eraseId(merged.addedBehaviors, behaviorId);
		else if(!hasId(merged.removedBehaviors, behaviorId)) merged.removedBehaviors.push_back(behaviorId);
	}
	for(PoolId behaviorId : rLevel_.addedBehaviors)
	{
		if(hasId(merged.removedBehaviors, behaviorId)) eraseId(merged.removedBehaviors, behaviorId);
		else if(!hasId(merged.addedBehaviors, behaviorId)) merged.addedBehaviors.push_back(behaviorId);
	}
	for(const Physics::CollisionShape& rShape : rLevel_.removedShapes)
	{
		auto added = std::find(merged.addedShapes.begin(), merged.addedShapes.end(), rShape);
		if(added != merged.addedShapes.end()) merged.addedShapes.erase(added);
		else merged.removedShapes.push_back(rShape);
	}
	for(const Physics::CollisionShape& rShape : rLevel_.addedShapes)
	{
		auto removed = std::find(merged.removedShapes.begin(), merged.removedShapes.end(), rShape);
		if(removed != merged.removedShapes.end()) merged.removedShapes.erase(removed);
		else merged.addedShapes.push_back(rShape);
	}
	if(rLevel_.mesh.kind != MeshChoiceKind::Default) merged.mesh = rLevel_.mesh;
	if(rLevel_.texture.kind != TextureChoiceKind::Default) merged.texture = rLevel_.texture;
	return merged;
}

void applyPrefabBehaviors(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	for(const PrefabEdits* pEdits : {&rPrefab.changed, &rPrefab.source, &rPrefab.level})
	{
		for(PoolId behaviorId : pEdits->addedBehaviors)
		{
			if(!entityHasBehavior(Globals::Game::entities[entityId_], behaviorId)) giveEntityBehavior(entityId_, behaviorId);
		}
		for(PoolId behaviorId : pEdits->removedBehaviors)
		{
			while(removeEntityBehavior(entityId_, behaviorId)) {}
		}
	}
}

void applyPrefabShapes(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	if(rPrefab.changed.addedShapes.empty() && rPrefab.changed.removedShapes.empty() && rPrefab.source.addedShapes.empty() && rPrefab.source.removedShapes.empty() && rPrefab.level.addedShapes.empty() && rPrefab.level.removedShapes.empty()) return;
	std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);
	applyPrefabShapesTo(rPrefab, shapes);
	setEntityShapes(entityId_, shapes);
}

/* An External prefab's model or texture changed: its picture and listing are stale, and the definition is written to its file (the editor only; the game never edits). */
static void externalPrefabChanged(PoolId prefabId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	rPrefab.hasModel = rPrefab.level.mesh.kind != MeshChoiceKind::Default;
	prefabRevisionCounter++;
#ifdef EDITOR
	Loading::saveExternalPrefabs(rPrefab.inLevel ? Loading::ExternalScope::Level : Loading::ExternalScope::Project);
#endif
}

uint32_t setPrefabTexture(PoolId prefabId_, const TextureChoice& rChoice_, PrefabScope scope_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	editsOf(rPrefab, scope_).texture = rChoice_;
	uint32_t failed = 0;
	bool levelDecides = scope_ == PrefabScope::Global && (rPrefab.level.texture.kind != TextureChoiceKind::Default || rPrefab.source.texture.kind != TextureChoiceKind::Default);
	TextureChoice choice = prefabTexture(rPrefab);
	if(!levelDecides)
	{
		forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
		{
			if(!Globals::Game::entities[entityId_].hasModelSlot()) return;
			try
			{
				setEntityTexture(entityId_, choice);
			}
			catch(const std::runtime_error& rError)
			{
				if(failed == 0) std::fprintf(stderr, "Texture: %s %s: %s\n", Globals::Game::prefabs[prefabId_].name.c_str(), Globals::Game::entities.getName(entityId_).c_str(), rError.what());
				failed++;
			}
		});
	}
	if(rPrefab.kind == PrefabKind::External) externalPrefabChanged(prefabId_);
	changedEdited(prefabId_, scope_);
	return failed;
}

void applyPrefabTexture(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	TextureChoice choice = prefabTexture(rPrefab);
	if(choice.kind == TextureChoiceKind::Default) return;
	try
	{
		setEntityTexture(entityId_, choice);
	}
	catch(const std::runtime_error& rError)
	{
		std::fprintf(stderr, "Texture: %s: %s\n", rPrefab.name.c_str(), rError.what());
	}
}

uint32_t setPrefabMesh(PoolId prefabId_, const MeshChoice& rChoice_, PrefabScope scope_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	editsOf(rPrefab, scope_).mesh = rChoice_;
	uint32_t failed = 0;
	bool levelDecides = scope_ == PrefabScope::Global && (rPrefab.level.mesh.kind != MeshChoiceKind::Default || rPrefab.source.mesh.kind != MeshChoiceKind::Default);
	MeshChoice choice = prefabMesh(rPrefab);
	if(!levelDecides)
	{
		forEachEntityOfPrefab(prefabId_, [&](PoolId entityId_)
		{
			if(!Globals::Game::entities[entityId_].hasModelSlot()) return;
			try
			{
				setEntityMesh(entityId_, choice);
			}
			catch(const std::runtime_error& rError)
			{
				if(failed == 0) std::fprintf(stderr, "Model: %s %s: %s\n", Globals::Game::prefabs[prefabId_].name.c_str(), Globals::Game::entities.getName(entityId_).c_str(), rError.what());
				failed++;
			}
		});
	}
	if(rPrefab.kind == PrefabKind::External) externalPrefabChanged(prefabId_);
	changedEdited(prefabId_, scope_);
	return failed;
}

void applyPrefabMesh(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	MeshChoice choice = prefabMesh(rPrefab);
	if(choice.kind == MeshChoiceKind::Default) return;
	try
	{
		setEntityMesh(entityId_, choice);
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
	prefab.level.mesh = rMesh_;
	prefab.level.texture = rTexture_;
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
	/* The players: no entity key, they are made from the constants of ResourceKeys.h, and their body shape is their Changed file's (spawnPrefab). A script attaches their behaviours (attachPlayer). */
	addPrefab(Prefab{"Sonic", SA_NINJA, PrefabGroup::Characters, PrefabKind::SonicPlayer, nullptr, nullptr, true, true, true});
	addPrefab(Prefab{"Shadow", STH2005_ONE, PrefabGroup::Characters, PrefabKind::ShadowPlayer, nullptr, nullptr, true, true, true});

	/* The groups of the key prefabs are given afterwards, from Groups.json (Loading::loadPrefabData). */
	Loading::forEachSAEntityKey([](const SAEntityKey& rKey_)
	{
		bool hasModel = rKey_.model.rel != nullptr;
		for(const SAModelKey& rVariant : rKey_.variantModels) hasModel = hasModel || rVariant.rel != nullptr;
		bool hasCollision = !rKey_.collisions.empty() || !rKey_.variantCollisions.empty();
		addPrefab(Prefab{rKey_.name, SA_NINJA, PrefabGroup::StaticObjects, PrefabKind::SAObject, &rKey_, nullptr, hasModel, hasCollision, false});
	});
	Loading::forEachONEEntityKey([](const STH2005::EntityKey& rKey_)
	{
		addPrefab(Prefab{rKey_.name, STH2005_ONE, PrefabGroup::StaticObjects, PrefabKind::ONEObject, nullptr, &rKey_, !rKey_.models.empty(), !rKey_.collisions.empty(), false});
	});
}
