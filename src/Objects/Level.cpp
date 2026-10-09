#include "Level.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "ResourceLoader.h"
#include "Globals.h"
#include "API.h"
#include "Prefab.h"
#ifdef EDITOR
#include "ExternalPrefabs.h"
#endif
#include "ONEInstance.h"
#include "Placement.h"
#include "RingLine.h"
#include "SASet.h"
#include "SAMesh.h"
#include "BinaryRead.h"
#include <cstdio>
#include <format>
#include <algorithm>
#include <stdexcept>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

void Level::setSAGPUInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_, const std::vector<std::string>& rSkippedTextures_)
{
	if(this->instanceId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has an instance.");
	}
	this->instanceId = Loading::addSALevelInstance(shaderId_, rLandTable_, rTextures_, rSkippedTextures_);
	this->sourceEngine = SA_NINJA;
}

void Level::setONEGPUInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_, const std::vector<std::string>& rSkippedEntries_)
{
	if(this->instanceId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has an instance.");
	}
	this->instanceId = Loading::addONELevelInstance(shaderId_, rLevel_, rSkippedEntries_);
	this->sourceEngine = STH2005_ONE;
}

void Level::setGLTFGPUInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_)
{
	if(this->instanceId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has an instance.");
	}
	this->instanceId = Loading::addGLTFLevelInstance(shaderId_, rLevel_);
	this->sourceEngine = GLTF_FILE;
}

void Level::setPosition(const glm::vec3& rPosition_)
{
	this->position = rPosition_;
	if(this->collisionId != UninitializedPoolId)
	{
		Globals::Physics::collisions[this->collisionId].setPosition(rPosition_);
		Globals::Physics::collisionSystem.rebuildQuadTree();
	}
}

/*
Entities of a Shadow stage's placement files, see Level.h. The records are in the game's space: the level is drawn with RenderWareYawDegrees about +Y
(hypothesis, see ONEInstance.h) and ONEInstance gives every model the same yaw, so the position gets it too (as DebugSetup::addShadowPlacementMarkers does).
Which entity a type is made of is in the key (generated, see tools/genEntityKeys.py); a type it does not list (invisible triggers, effects, enemies without an archive entry) gets a marker instead (Editor::collisionView).
*/
void Level::setONEEntities(PoolId shaderId_, const STH2005::LevelKey& rLevel_)
{
	if(!this->entityIds.empty())
	{
		throw std::runtime_error("Level: already has entities.");
	}
	glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), glm::radians(Graphics::RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));

	/*
	A record's rotation in the engine's space. The angles (degrees by their values, hypothesis) are applied about Y, then X, then Z, the order the game's ring code builds
	its matrix in (external, decompile of FUN_80112cbc; which axis is which is a hypothesis), in the game's space; the level and its objects are drawn with the yaw about the
	origin, and ONEInstance adds the yaw to every model itself, so the rotation is conjugated by the yaw: yaw * rotation * inverse(yaw), then ONEInstance's own yaw completes yaw * rotation.
	*/
	auto worldRotation = [&](const glm::vec3& rDegrees_)
	{
		glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(rDegrees_.y), glm::vec3(0.0f, 1.0f, 0.0f));
		rotation = glm::rotate(rotation, glm::radians(rDegrees_.x), glm::vec3(1.0f, 0.0f, 0.0f));
		rotation = glm::rotate(rotation, glm::radians(rDegrees_.z), glm::vec3(0.0f, 0.0f, 1.0f));
		return yaw * rotation * glm::inverse(yaw);
	};
	for(const char* pPath : rLevel_.placementFiles)
	{
		std::vector<STH2005::Placement> placements = STH2005::readPlacements(Globals::Discs::shadow.readFile(pPath));
		for(const STH2005::Placement& rPlacement : placements)
		{
			auto found = std::find_if(rLevel_.entities.begin(), rLevel_.entities.end(), [&](const STH2005::PlacedEntityKey& rKey_)
			{
				if(rKey_.type != rPlacement.type) return false;
				return rKey_.parameter < 0 || (rPlacement.params.size() >= 4 && readLE<uint32_t>(rPlacement.params, 0) == static_cast<uint32_t>(rKey_.parameter));
			});
			if(found == rLevel_.entities.end())
			{
				/* No key for the type (invisible triggers, effects, other enemies): a cross, colored by the type, so it can be told what they are. */
				const char* pLabel = nullptr;
				if(rLevel_.typeNames != nullptr)
				{
					auto name = rLevel_.typeNames->find(rPlacement.type);
					if(name != rLevel_.typeNames->end()) pLabel = name->second;
				}
#ifdef EDITOR
				editor.collisionView.markers.push_back(Graphics::CollisionView::Marker{glm::vec3(yaw * glm::vec4(rPlacement.position, 1.0f)) + this->position, rPlacement.type, pLabel});
#endif
				continue;
			}
			if(found->entity.models.empty()) continue;
			const STH2005::EntityKey& rKey = found->entity;
			PoolId prefabId = findPrefab(STH2005_ONE, rKey.name);
			if(prefabId == UninitializedPoolId)
			{
				std::fprintf(stderr, "Level: skipped a record of %s: there is no prefab for it.\n", rKey.name);
				continue;
			}
			const Prefab& rPrefab = Globals::Game::prefabs[prefabId];

			/*
			What a record makes (see PlacementLayout, the prefab's): Single, one entity per model of the key's first variant (just the first model if it has none); RingLine, one per ring; Variant, one per model of the variant the record
			names, the variant's collision (the one with its index, if the key has that many) going to the first of them.
			A rotation is the record's three angles in degrees; a ring takes only the one about Y: the ring code copies one angle of the record (the one it rotates the
			line about first) into each ring (external, decompile of FUN_80112cbc; that it is the yaw is a hypothesis).
			*/
			struct Spawn
			{
				glm::vec3 gamePosition;
				glm::vec3 gameRotation;
				uint32_t modelIndex;
				std::vector<STH2005::CollisionKey> collisions;
			};
			std::vector<Spawn> spawns;
			if(rPrefab.layout == PlacementLayout::RingLine)
			{
				for(const glm::vec3& rRingPosition : STH2005::readRingPositions(rPlacement)) spawns.push_back(Spawn{rRingPosition, glm::vec3(0.0f, rPlacement.rotation.y, 0.0f), 0, rKey.collisions});
			}
			else if(rPrefab.layout == PlacementLayout::Variant)
			{
				if(rPlacement.params.size() < 4) throw std::runtime_error(std::format("Level: a record of {} has no variant parameter.", rKey.name));
				uint32_t variant = readLE<uint32_t>(rPlacement.params, 0);
				if(variant >= rPrefab.variants.size()) throw std::runtime_error(std::format("Level: a record of {} names variant {} of {}.", rKey.name, variant, rPrefab.variants.size()));
				for(uint32_t modelIndex : rPrefab.variants[variant])
				{
					std::vector<STH2005::CollisionKey> collisions;
					if(spawns.empty() && variant < rKey.collisions.size()) collisions.push_back(rKey.collisions[variant]);
					spawns.push_back(Spawn{rPlacement.position, rPlacement.rotation, modelIndex, collisions});
				}
			}
			else
			{
				/* The first variant (the .BIN's first info) is the object: all of its models, the first of them getting the collision. */
				std::vector<uint32_t> modelIndices = rPrefab.variants.empty() ? std::vector<uint32_t>{0} : rPrefab.variants[0];
				for(uint32_t modelIndex : modelIndices)
				{
					spawns.push_back(Spawn{rPlacement.position, rPlacement.rotation, modelIndex, spawns.empty() ? rKey.collisions : std::vector<STH2005::CollisionKey>{}});
				}
			}

			for(const Spawn& rSpawn : spawns)
			{
				glm::vec3 position = glm::vec3(yaw * glm::vec4(rSpawn.gamePosition, 1.0f)) + this->position;
				SpawnParams params;
				params.position = position;
				params.rotation = worldRotation(rSpawn.gameRotation);
				params.name = std::format("{}#{}", rKey.name, this->entityIds.size());
				params.what = std::format("(type {}) of {}", static_cast<unsigned>(rPlacement.type), pPath);
				params.modelIndex = rSpawn.modelIndex;
				params.pONECollisions = &rSpawn.collisions;
				PoolId id = spawnPrefab(prefabId, shaderId_, params);
				if(id == UninitializedPoolId) continue;
				this->entityIds.push_back(id);
			}
		}
	}
}

/*
The orientation an SADX SET record gives an object. External, from the decompile of _Main.rel: the objects' display code turns by the angles of their task (x at +0x14, y at +0x18, z at
+0x1C) with the Ninja rotation functions (the ones that build a rotation about 'x', 'y' or 'z' and multiply it into the current matrix on the right), and the order that comes up
most is z, then x, then y (92 functions with all three, 98 more with z and x); so the matrix is Rz * Rx * Ry, which turns the object about its own y first. Objects whose code differs have
a rotation recipe in their prefab (Prefab::rotation, from External/Prefabs/SADX/Placement.json): e.g. the dash panel turns about y by the angle plus 0x8000, the launch board turns about y before x. The angles are BAMS (65536 = a turn).
*/
static glm::mat4 saObjectRotation(const glm::ivec3& rBams_, const std::vector<SARotationStep>& rSteps_)
{
	constexpr float BamsToRadians = glm::two_pi<float>() / 65536.0f;
	if(rSteps_.empty())
	{
		glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), rBams_.z * BamsToRadians, glm::vec3(0.0f, 0.0f, 1.0f));
		rotation = glm::rotate(rotation, rBams_.x * BamsToRadians, glm::vec3(1.0f, 0.0f, 0.0f));
		return glm::rotate(rotation, rBams_.y * BamsToRadians, glm::vec3(0.0f, 1.0f, 0.0f));
	}
	glm::mat4 rotation(1.0f);
	for(const SARotationStep& rStep : rSteps_)
	{
		float angle = rStep.field < 3 ? static_cast<float>(rBams_[rStep.field]) : 0.0f;
		angle = (rStep.negate ? -angle : angle) + static_cast<float>(rStep.addend);
		glm::vec3 axis(rStep.axis == 'X' ? 1.0f : 0.0f, rStep.axis == 'Y' ? 1.0f : 0.0f, rStep.axis == 'Z' ? 1.0f : 0.0f);
		rotation = glm::rotate(rotation, angle * BamsToRadians, axis);
	}
	return rotation;
}

/*
Entities of an SADX stage's SET files, see Level.h. Positions are the records' own (the land table and the objects share a space, observed on Emerald Coast: the SET positions are in
the land table's range) and the rotation is the record's three BAMS angles in the order the game's object code uses most (saObjectRotation). An object that
fails to load is reported on stderr and skipped, as in setONEEntities.
*/
void Level::setSAEntities(PoolId shaderId_, const SALandTableKey& rLandTable_)
{
	if(!this->entityIds.empty())
	{
		throw std::runtime_error("Level: already has entities.");
	}

	/* One entity of the level (spawnPrefab): what_ names the source in a message. */
	auto createEntity = [&](const SAEntityKey& rKey_, const SAModelKey& rModel_, const std::vector<SACollisionShapesKey>& rCollisions_, const glm::vec3& rPosition_, const glm::mat4& rRotation_, const std::string& what_, const Ninja::SAPlacement* pPlacement_)
	{
		SpawnParams params;
		params.position = rPosition_;
		params.rotation = rRotation_;
		params.name = std::format("{}#{}", rKey_.name, this->entityIds.size());
		params.what = what_;
		params.pModel = &rModel_;
		params.pCollisions = &rCollisions_;
		params.pPlacement = pPlacement_;
		PoolId id = spawnPrefab(findPrefab(SA_NINJA, rKey_.name), shaderId_, params);
		if(id != UninitializedPoolId) this->entityIds.push_back(id);
	};

	for(const char* pPath : rLandTable_.placementFiles)
	{
		std::vector<Ninja::SAPlacement> placements = Ninja::readSAPlacements(Globals::Discs::sadx.readFile(pPath));
		for(const Ninja::SAPlacement& rPlacement : placements)
		{
			glm::vec3 position = rPlacement.position + this->position;
			auto found = std::find_if(rLandTable_.entities.begin(), rLandTable_.entities.end(), [&](const SAPlacedEntityKey& rKey_) { return rKey_.type == rPlacement.type; });
			if(found == rLandTable_.entities.end())
			{
				const char* pLabel = nullptr;
				if(rLandTable_.typeNames != nullptr)
				{
					auto name = rLandTable_.typeNames->find(rPlacement.type);
					if(name != rLandTable_.typeNames->end()) pLabel = name->second;
				}
#ifdef EDITOR
				editor.collisionView.markers.push_back(Graphics::CollisionView::Marker{position, rPlacement.type, pLabel});
#endif
				continue;
			}
			const SAEntityKey& rKey = found->entity;
			const Prefab& rPrefab = Globals::Game::prefabs[findPrefab(SA_NINJA, rKey.name)];
			uint32_t variantValue = static_cast<uint32_t>(static_cast<int32_t>(rPlacement.scale[rPrefab.variantField]) & 0xFF);
			const SAModelKey& rModel = rKey.variantModels.empty() ? rKey.model : rKey.variantModels[variantValue % rKey.variantModels.size()];
			const std::vector<SACollisionShapesKey>& rCollisions = rKey.variantCollisions.empty() ? rKey.collisions : rKey.variantCollisions[variantValue % rKey.variantCollisions.size()];
			if(rModel.rel == nullptr)
			{
#ifdef EDITOR
				editor.collisionView.markers.push_back(Graphics::CollisionView::Marker{position, rPlacement.type, rKey.name});
#endif
				continue;
			}
			createEntity(rKey, rModel, rCollisions, position, saObjectRotation(rPlacement.rotation, rPrefab.rotation), std::format("(type {}) of {}", static_cast<unsigned>(rPlacement.type), pPath), &rPlacement);
		}
	}
}

void Level::setSACollision(const SALandTableKey& rLandTable_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has a collision.");
	}
	this->collisionId = Loading::addSACollision(rLandTable_);
	Globals::Physics::collisions[this->collisionId].setPosition(this->position);
	Globals::Physics::collisionSystem.rebuildQuadTree();
}

void Level::setONECollision(const STH2005::LevelKey& rLevel_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has a collision.");
	}
	this->collisionId = Loading::addONECollision(rLevel_);
	Globals::Physics::collisions[this->collisionId].setPosition(this->position);
	Globals::Physics::collisionSystem.rebuildQuadTree();
}

void Level::setGLTFCollision(const GLTFLevelKey& rLevel_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has a collision.");
	}
	this->collisionId = Loading::addGLTFCollision(rLevel_);
	Globals::Physics::collisions[this->collisionId].setPosition(this->position);
	Globals::Physics::collisionSystem.rebuildQuadTree();
}

void Level::swapGLTFGPUInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_)
{
	if(this->sourceEngine != GLTF_FILE) throw std::runtime_error("Level: not a glTF level.");
	if(this->instanceId != UninitializedPoolId)
	{
		Graphics::gltfLevelInstances.remove(this->instanceId);
		this->instanceId = UninitializedPoolId;
	}
	this->setGLTFGPUInstance(shaderId_, rLevel_);
}

void Level::swapGLTFCollision(const GLTFLevelKey& rLevel_)
{
	if(this->sourceEngine != GLTF_FILE) throw std::runtime_error("Level: not a glTF level.");
	if(this->collisionId != UninitializedPoolId)
	{
		Globals::Physics::collisions.remove(this->collisionId);
		this->collisionId = UninitializedPoolId;
	}
	if(rLevel_.collisions.empty()) Globals::Physics::collisionSystem.rebuildQuadTree();
	else this->setGLTFCollision(rLevel_);
}

void Level::appendGLTFCollision(const GLTFLevelKey& rLevel_, const std::string& rName_)
{
	if(this->sourceEngine != GLTF_FILE) throw std::runtime_error("Level: not a glTF level.");
	if(this->collisionId == UninitializedPoolId)
	{
		this->setGLTFCollision(rLevel_);
		return;
	}
	Loading::appendGLTFCollision(this->collisionId, rLevel_, rName_);
	Globals::Physics::collisionSystem.rebuildQuadTree();
}

void Level::update(float deltaTime_)
{
	if(this->instanceId == UninitializedPoolId) return;
}

void Level::draw(const Camera& rCamera_)
{
	if(this->instanceId == UninitializedPoolId) return;
	glm::mat4 base = glm::translate(glm::mat4(1.0f), this->position);
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::getSALevelInstance(this->instanceId).draw(base, rCamera_);
		break;
	case STH2005_ONE:
		Graphics::getONELevelInstance(this->instanceId).draw(base, rCamera_);
		break;
	case GLTF_FILE:
		Graphics::gltfLevelInstances[this->instanceId].draw(base, rCamera_);
		break;
	}
}

/* Removes the level's instance, which gives back the users of its pieces. Called by PoolMap::remove. */
void Level::markSourceEntities()
{
	this->sourceEntityCount = static_cast<uint32_t>(this->entityIds.size());
	this->sourcePositions.clear();
	this->sourceRotations.clear();
	this->sourceScales.clear();
	this->sourceBehaviors.clear();
	this->sourceShapes.clear();
	for(PoolId id : this->entityIds)
	{
		/* A gone entity (an entry of the saved source that could not be made) keeps its index with empty values. */
		if(!entityExists(id))
		{
			this->sourcePositions.push_back(glm::vec3(0.0f));
			this->sourceRotations.push_back(glm::mat4(1.0f));
			this->sourceScales.push_back(glm::vec3(1.0f));
			this->sourceBehaviors.emplace_back();
			this->sourceShapes.emplace_back();
			continue;
		}
		const Entity& rEntity = getEntity(id);
		this->sourcePositions.push_back(rEntity.position);
		this->sourceRotations.push_back(rEntity.rotation);
		this->sourceScales.push_back(rEntity.scale);
		this->sourceBehaviors.push_back(rEntity.behaviorSetId == UninitializedPoolId ? std::vector<PoolId>() : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds);
		this->sourceShapes.push_back(entityShapes(id));
	}
}

int32_t Level::sourceIndexOf(PoolId entityId_) const
{
	for(uint32_t i = 0; i < this->sourceEntityCount; i++)
	{
		if(this->entityIds[i].id == entityId_.id && this->entityIds[i].generation == entityId_.generation) return static_cast<int32_t>(i);
	}
	return -1;
}

int32_t Level::editIndexOf(PoolId entityId_) const
{
	if(entityId_ == UninitializedPoolId) return -1;
	uint32_t added = this->sourceEntityCount;
	for(uint32_t i = 0; i < this->entityIds.size(); i++)
	{
		bool own = i < this->sourceEntityCount;
		if(this->entityIds[i] == entityId_)
		{
			if(own) return static_cast<int32_t>(i);
			return entityExists(entityId_) && getEntity(entityId_).prefabId != UninitializedPoolId ? static_cast<int32_t>(added) : -1;
		}
		if(!own && entityExists(this->entityIds[i]) && getEntity(this->entityIds[i]).prefabId != UninitializedPoolId) added++;
	}
	return -1;
}

void Level::resolveExternalEntities()
{
	for(PoolId id : this->entityIds)
	{
		if(!entityExists(id)) continue;
		for(auto& [behaviorName, rValues] : getEntity(id).externals)
		{
			for(auto& [fieldName, rValue] : rValues)
			{
				if(rValue.entityIndex < 0) continue;
				bool found = static_cast<size_t>(rValue.entityIndex) < this->entityIds.size() && entityExists(this->entityIds[rValue.entityIndex]);
				rValue.entity = found ? this->entityIds[rValue.entityIndex] : UninitializedPoolId;
				rValue.entityIndex = -1;
			}
		}
	}
}

std::vector<PoolId> Level::referencedPrefabs() const
{
	std::vector<PoolId> found;
	for(PoolId id : this->entityIds)
	{
		if(!entityExists(id)) continue;
		for(const auto& [behaviorName, rValues] : getEntity(id).externals)
		{
			for(const auto& [fieldName, rValue] : rValues)
			{
				PoolId prefab = rValue.prefab;
				if(prefab.id >= Globals::Game::prefabs.objects.size() || !Globals::Game::prefabs.alive[prefab.id] || Globals::Game::prefabs.generation[prefab.id] != prefab.generation) continue;
				if(std::find(found.begin(), found.end(), prefab) == found.end()) found.push_back(prefab);
			}
		}
	}
	return found;
}

void Level::revertSourceEntity(uint32_t index_)
{
	if(index_ >= this->sourceEntityCount || !entityExists(this->entityIds[index_])) return;
	Entity& rEntity = getEntity(this->entityIds[index_]);
	rEntity.setPosition(this->sourcePositions[index_]);
	rEntity.setRotation(this->sourceRotations[index_]);
	rEntity.setScale(this->sourceScales[index_]);
}

void Level::restoreSource()
{
	clearPrefabEdits();
	for(uint32_t i = 0; i < this->sourceEntityCount; i++)
	{
		this->revertSourceEntity(i);
		if(!entityExists(this->entityIds[i])) continue;
		getEntity(this->entityIds[i]).externals.clear();
		getEntity(this->entityIds[i]).externalsApplied.clear();
		getEntity(this->entityIds[i]).externalsKept.clear();
		setEntityBehaviors(this->entityIds[i], this->sourceBehaviors[i]);
		setEntityShapes(this->entityIds[i], this->sourceShapes[i]);
		if(entityMesh(this->entityIds[i]).kind != MeshChoiceKind::Default) setEntityMesh(this->entityIds[i], MeshChoice{});
		if(entityTexture(this->entityIds[i]).kind != TextureChoiceKind::Default) setEntityTexture(this->entityIds[i], TextureChoice{});
	}
	for(uint32_t i = this->sourceEntityCount; i < this->entityIds.size(); i++)
	{
		if(entityExists(this->entityIds[i])) Globals::Game::entities.remove(this->entityIds[i]);
	}
	this->entityIds.resize(this->sourceEntityCount);
}

void Level::destroy()
{
#ifdef EDITOR
	/* The markers do not say which level they belong to; one level is loaded at a time, so all of them go. */
	editor.collisionView.markers.clear();
#endif
	for(PoolId id : this->entityIds)
	{
		if(entityExists(id)) Globals::Game::entities.remove(id);
	}
	this->entityIds.clear();
	this->sourceEntityCount = 0;
	this->sourcePositions.clear();
	this->sourceRotations.clear();
	this->sourceScales.clear();
	this->sourceBehaviors.clear();
	this->sourceShapes.clear();
#ifdef EDITOR
	/* The level's own External prefabs go with its entities. */
	Loading::unloadLevelPrefabs();
#endif
	if(this->collisionId != UninitializedPoolId)
	{
		Globals::Physics::collisions.remove(this->collisionId);
		this->collisionId = UninitializedPoolId;
		Globals::Physics::collisionSystem.rebuildQuadTree();
	}
	if(this->instanceId == UninitializedPoolId) return;
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::saLevelInstances.remove(this->instanceId);
		break;
	case STH2005_ONE:
		Graphics::oneLevelInstances.remove(this->instanceId);
		break;
	case GLTF_FILE:
		Graphics::gltfLevelInstances.remove(this->instanceId);
		break;
	}
	this->instanceId = UninitializedPoolId;
}
