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

/* The level an edit or an entity's sets are of: the one given, else the active level. */
static PoolId resolveLevel(PoolId levelId_)
{
	return levelId_ == UninitializedPoolId ? activeLevelId() : levelId_;
}

static const PrefabEdits NoEdits{};

const PrefabEdits& prefabSourceEdits(const Prefab& rPrefab_, PoolId levelId_)
{
	auto found = rPrefab_.inLevels.find(resolveLevel(levelId_).id);
	return found == rPrefab_.inLevels.end() ? NoEdits : found->second.source;
}

const PrefabEdits& prefabLevelEdits(const Prefab& rPrefab_, PoolId levelId_)
{
	auto found = rPrefab_.inLevels.find(resolveLevel(levelId_).id);
	return found == rPrefab_.inLevels.end() ? NoEdits : found->second.level;
}

/*
Every live entity made from the prefab, by pool id (the behaviours given or removed do not add or remove entities): all of them for a Global edit, for a Level edit those of the level (an entity without one counts as the active level's).
*/
static void forEachEntityOfPrefab(PoolId prefabId_, PrefabScope scope_, PoolId levelId_, auto&& rDo_)
{
	auto& rEntities = Globals::Game::entities;
	PoolId editLevel = resolveLevel(levelId_);
	for(uint32_t i = 0; i < rEntities.objects.size(); i++)
	{
		if(!rEntities.alive[i] || rEntities.objects[i].prefabId != prefabId_) continue;
		if(scope_ == PrefabScope::Level && resolveLevel(rEntities.objects[i].levelId) != editLevel) continue;
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

/* The set of the scope (Level: the level's, made if it has none); Global is refused for an External prefab (its definition is saved with the prefab, its level edits with the level, not in a Changed file). */
static PrefabEdits& editsOf(Prefab& rPrefab_, PrefabScope scope_, PoolId levelId_)
{
	if(scope_ == PrefabScope::Level) return rPrefab_.inLevels[resolveLevel(levelId_).id].level;
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

void addPrefabBehavior(PoolId prefabId_, PoolId behaviorId_, PrefabScope scope_, PoolId levelId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	PrefabEdits& rEdits = editsOf(rPrefab, scope_, levelId_);
	eraseId(rEdits.removedBehaviors, behaviorId_);
	if(!hasId(rEdits.addedBehaviors, behaviorId_)) rEdits.addedBehaviors.push_back(behaviorId_);
	forEachEntityOfPrefab(prefabId_, scope_, levelId_, [&](PoolId entityId_)
	{
		Entity& rEntity = Globals::Game::entities[entityId_];
		bool levelDecides = scope_ == PrefabScope::Global && (hasId(prefabLevelEdits(rPrefab, rEntity.levelId).removedBehaviors, behaviorId_) || hasId(prefabSourceEdits(rPrefab, rEntity.levelId).removedBehaviors, behaviorId_));
		if(levelDecides) return;
		if(!entityHasBehavior(rEntity, behaviorId_)) giveEntityBehavior(entityId_, behaviorId_);
	});
	changedEdited(prefabId_, scope_);
}

void removePrefabBehavior(PoolId prefabId_, PoolId behaviorId_, PrefabScope scope_, PoolId levelId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	PrefabEdits& rEdits = editsOf(rPrefab, scope_, levelId_);
	eraseId(rEdits.addedBehaviors, behaviorId_);
	if(!hasId(rEdits.removedBehaviors, behaviorId_)) rEdits.removedBehaviors.push_back(behaviorId_);
	forEachEntityOfPrefab(prefabId_, scope_, levelId_, [&](PoolId entityId_)
	{
		Entity& rEntity = Globals::Game::entities[entityId_];
		bool levelDecides = scope_ == PrefabScope::Global && (hasId(prefabLevelEdits(rPrefab, rEntity.levelId).addedBehaviors, behaviorId_) || hasId(prefabSourceEdits(rPrefab, rEntity.levelId).addedBehaviors, behaviorId_));
		if(levelDecides) return;
		while(removeEntityBehavior(entityId_, behaviorId_)) {}
	});
	changedEdited(prefabId_, scope_);
}

void addPrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_, PrefabScope scope_, PoolId levelId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	PrefabEdits& rEdits = editsOf(rPrefab, scope_, levelId_);
	auto removed = std::find(rEdits.removedShapes.begin(), rEdits.removedShapes.end(), rShape_);
	bool cancelsRemoval = removed != rEdits.removedShapes.end();
	if(cancelsRemoval) rEdits.removedShapes.erase(removed);
	else rEdits.addedShapes.push_back(rShape_);
	forEachEntityOfPrefab(prefabId_, scope_, levelId_, [&](PoolId entityId_)
	{
		std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);
		if(cancelsRemoval && std::find(shapes.begin(), shapes.end(), rShape_) != shapes.end()) return;
		/* A shape of the Changed file goes before the level's added ones, the order a level load gives (applyPrefabShapesTo), so the entity does not look edited. */
		PoolId entityLevel = Globals::Game::entities[entityId_].levelId;
		std::vector<Physics::CollisionShape> laterAdded = prefabSourceEdits(rPrefab, entityLevel).addedShapes;
		const std::vector<Physics::CollisionShape>& rLevelAdded = prefabLevelEdits(rPrefab, entityLevel).addedShapes;
		laterAdded.insert(laterAdded.end(), rLevelAdded.begin(), rLevelAdded.end());
		size_t levelAdded = laterAdded.size();
		bool endsWithLevelAdded = scope_ == PrefabScope::Global && shapes.size() >= levelAdded && std::equal(laterAdded.begin(), laterAdded.end(), shapes.end() - levelAdded);
		shapes.insert(endsWithLevelAdded ? shapes.end() - levelAdded : shapes.end(), rShape_);
		setEntityShapes(entityId_, shapes);
	});
	changedEdited(prefabId_, scope_);
}

void removePrefabShape(PoolId prefabId_, const Physics::CollisionShape& rShape_, PrefabScope scope_, PoolId levelId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	PrefabEdits& rEdits = editsOf(rPrefab, scope_, levelId_);
	auto added = std::find(rEdits.addedShapes.begin(), rEdits.addedShapes.end(), rShape_);
	if(added != rEdits.addedShapes.end()) rEdits.addedShapes.erase(added);
	else rEdits.removedShapes.push_back(rShape_);
	forEachEntityOfPrefab(prefabId_, scope_, levelId_, [&](PoolId entityId_)
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

void applyPrefabShapesTo(const Prefab& rPrefab_, std::vector<Physics::CollisionShape>& rShapes_, PoolId levelId_)
{
	applyShapeEdits(rPrefab_.changed, rShapes_);
	applyShapeEdits(prefabSourceEdits(rPrefab_, levelId_), rShapes_);
	applyShapeEdits(prefabLevelEdits(rPrefab_, levelId_), rShapes_);
}

static void applyBehaviorEdits(const PrefabEdits& rEdits_, std::vector<PoolId>& rBehaviors_)
{
	for(PoolId behaviorId : rEdits_.addedBehaviors)
	{
		if(!hasId(rBehaviors_, behaviorId)) rBehaviors_.push_back(behaviorId);
	}
	for(PoolId behaviorId : rEdits_.removedBehaviors) eraseId(rBehaviors_, behaviorId);
}

void applyPrefabBehaviorsTo(const Prefab& rPrefab_, std::vector<PoolId>& rBehaviors_, PoolId levelId_)
{
	applyBehaviorEdits(rPrefab_.changed, rBehaviors_);
	applyBehaviorEdits(prefabSourceEdits(rPrefab_, levelId_), rBehaviors_);
	applyBehaviorEdits(prefabLevelEdits(rPrefab_, levelId_), rBehaviors_);
}

MeshChoice prefabMesh(const Prefab& rPrefab_, PoolId levelId_)
{
	if(rPrefab_.kind == PrefabKind::External) return rPrefab_.definition.mesh;
	const PrefabEdits& rLevel = prefabLevelEdits(rPrefab_, levelId_);
	if(rLevel.mesh.kind != MeshChoiceKind::Default) return rLevel.mesh;
	const PrefabEdits& rSource = prefabSourceEdits(rPrefab_, levelId_);
	return rSource.mesh.kind != MeshChoiceKind::Default ? rSource.mesh : rPrefab_.changed.mesh;
}

TextureChoice prefabTexture(const Prefab& rPrefab_, PoolId levelId_)
{
	if(rPrefab_.kind == PrefabKind::External) return rPrefab_.definition.texture;
	const PrefabEdits& rLevel = prefabLevelEdits(rPrefab_, levelId_);
	if(rLevel.texture.kind != TextureChoiceKind::Default) return rLevel.texture;
	const PrefabEdits& rSource = prefabSourceEdits(rPrefab_, levelId_);
	return rSource.texture.kind != TextureChoiceKind::Default ? rSource.texture : rPrefab_.changed.texture;
}

void clearPrefabEdits(PoolId levelId_)
{
	auto& rPrefabs = Globals::Game::prefabs;
	uint32_t levelSlot = resolveLevel(levelId_).id;
	for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
	{
		if(!rPrefabs.alive[i]) continue;
		auto found = rPrefabs.objects[i].inLevels.find(levelSlot);
		if(found != rPrefabs.objects[i].inLevels.end()) found->second.level = PrefabEdits{};
	}
}

void forgetLevelPrefabEdits(PoolId levelId_)
{
	auto& rPrefabs = Globals::Game::prefabs;
	for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
	{
		if(rPrefabs.alive[i]) rPrefabs.objects[i].inLevels.erase(levelId_.id);
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
	PoolId levelId = Globals::Game::entities[entityId_].levelId;
	for(const PrefabEdits* pEdits : {&rPrefab.changed, &prefabSourceEdits(rPrefab, levelId), &prefabLevelEdits(rPrefab, levelId)})
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
	PoolId levelId = Globals::Game::entities[entityId_].levelId;
	const PrefabEdits& rSource = prefabSourceEdits(rPrefab, levelId);
	const PrefabEdits& rLevel = prefabLevelEdits(rPrefab, levelId);
	if(rPrefab.changed.addedShapes.empty() && rPrefab.changed.removedShapes.empty() && rSource.addedShapes.empty() && rSource.removedShapes.empty() && rLevel.addedShapes.empty() && rLevel.removedShapes.empty()) return;
	std::vector<Physics::CollisionShape> shapes = entityShapes(entityId_);
	applyPrefabShapesTo(rPrefab, shapes, levelId);
	setEntityShapes(entityId_, shapes);
}

/* An External prefab's model or texture changed: its picture and listing are stale, and the definition is written to its file (the editor only; the game never edits). */
static void externalPrefabChanged(PoolId prefabId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	rPrefab.hasModel = rPrefab.definition.mesh.kind != MeshChoiceKind::Default;
	prefabRevisionCounter++;
#ifdef EDITOR
	Loading::saveExternalPrefabs(rPrefab.inLevel ? Loading::ExternalScope::Level : Loading::ExternalScope::Project, rPrefab.level);
#endif
}

uint32_t setPrefabTexture(PoolId prefabId_, const TextureChoice& rChoice_, PrefabScope scope_, PoolId levelId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	/* An External prefab's model and texture are its definition: every entity of it takes them, whichever level it is in. */
	bool definition = rPrefab.kind == PrefabKind::External && scope_ == PrefabScope::Level;
	if(definition) rPrefab.definition.texture = rChoice_;
	else editsOf(rPrefab, scope_, levelId_).texture = rChoice_;
	uint32_t failed = 0;
	forEachEntityOfPrefab(prefabId_, definition ? PrefabScope::Global : scope_, levelId_, [&](PoolId entityId_)
	{
		Entity& rEntity = Globals::Game::entities[entityId_];
		if(!rEntity.hasModelSlot()) return;
		bool levelDecides = scope_ == PrefabScope::Global && (prefabLevelEdits(rPrefab, rEntity.levelId).texture.kind != TextureChoiceKind::Default || prefabSourceEdits(rPrefab, rEntity.levelId).texture.kind != TextureChoiceKind::Default);
		if(levelDecides) return;
		try
		{
			setEntityTexture(entityId_, prefabTexture(rPrefab, rEntity.levelId));
		}
		catch(const std::runtime_error& rError)
		{
			if(failed == 0) std::fprintf(stderr, "Texture: %s %s: %s\n", rPrefab.name.c_str(), Globals::Game::entities.getName(entityId_).c_str(), rError.what());
			failed++;
		}
	});
	if(rPrefab.kind == PrefabKind::External) externalPrefabChanged(prefabId_);
	changedEdited(prefabId_, scope_);
	return failed;
}

void applyPrefabTexture(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	TextureChoice choice = prefabTexture(rPrefab, Globals::Game::entities[entityId_].levelId);
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

uint32_t setPrefabMesh(PoolId prefabId_, const MeshChoice& rChoice_, PrefabScope scope_, PoolId levelId_)
{
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	bool definition = rPrefab.kind == PrefabKind::External && scope_ == PrefabScope::Level;
	if(definition) rPrefab.definition.mesh = rChoice_;
	else editsOf(rPrefab, scope_, levelId_).mesh = rChoice_;
	uint32_t failed = 0;
	forEachEntityOfPrefab(prefabId_, definition ? PrefabScope::Global : scope_, levelId_, [&](PoolId entityId_)
	{
		Entity& rEntity = Globals::Game::entities[entityId_];
		if(!rEntity.hasModelSlot()) return;
		bool levelDecides = scope_ == PrefabScope::Global && (prefabLevelEdits(rPrefab, rEntity.levelId).mesh.kind != MeshChoiceKind::Default || prefabSourceEdits(rPrefab, rEntity.levelId).mesh.kind != MeshChoiceKind::Default);
		if(levelDecides) return;
		try
		{
			setEntityMesh(entityId_, prefabMesh(rPrefab, rEntity.levelId));
		}
		catch(const std::runtime_error& rError)
		{
			if(failed == 0) std::fprintf(stderr, "Model: %s %s: %s\n", rPrefab.name.c_str(), Globals::Game::entities.getName(entityId_).c_str(), rError.what());
			failed++;
		}
	});
	if(rPrefab.kind == PrefabKind::External) externalPrefabChanged(prefabId_);
	changedEdited(prefabId_, scope_);
	return failed;
}

void applyPrefabMesh(PoolId prefabId_, PoolId entityId_)
{
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	MeshChoice choice = prefabMesh(rPrefab, Globals::Game::entities[entityId_].levelId);
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

PoolId addExternalPrefab(bool inLevel_, const std::string& levelName_, const std::string& name_, const std::string& folder_, const MeshChoice& rMesh_, const TextureChoice& rTexture_)
{
	std::string poolName = inLevel_ ? "Level/" + levelName_ + "/" + name_ : "Global/" + name_;
	if(Globals::Game::prefabs.nameToId.contains(poolName)) throw std::runtime_error(std::format("the prefab name '{}' is taken.", poolName));
	Prefab prefab;
	prefab.name = name_;
	prefab.game = GLTF_FILE;
	prefab.kind = PrefabKind::External;
	prefab.hasModel = rMesh_.kind != MeshChoiceKind::Default;
	prefab.hasBehaviour = Scripting::hasPrefabBehavior(poolName.c_str());
	prefab.definition.mesh = rMesh_;
	prefab.definition.texture = rTexture_;
	prefab.inLevel = inLevel_;
	prefab.level = inLevel_ ? levelName_ : std::string();
	prefab.folder = folder_;
	PoolId id = Globals::Game::prefabs.add(poolName.c_str(), std::move(prefab));
	prefabRevisionCounter++;
	return id;
}

void removeLevelPrefabs(const std::string& levelName_)
{
	auto& rPrefabs = Globals::Game::prefabs;
	for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
	{
		if(rPrefabs.alive[i] && rPrefabs.objects[i].inLevel && rPrefabs.objects[i].level == levelName_) rPrefabs.remove(PoolId(i, rPrefabs.generation[i]));
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
