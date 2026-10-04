#include "Level.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "ResourceLoader.h"
#include "Globals.h"
#include "API.h"
#include "ONEInstance.h"
#include "Placement.h"
#include "RingLine.h"
#include "SASet.h"
#include "SADXGameplay.h"
#include "SAMesh.h"
#include "BinaryRead.h"
#include <cstdio>
#include <format>
#include <algorithm>
#include <stdexcept>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

void Level::setSAGPUInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_)
{
	if(this->instanceId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has an instance.");
	}
	this->instanceId = Loading::addSALevelInstance(shaderId_, rLandTable_, rTextures_);
	this->sourceEngine = SA_NINJA;
}

void Level::setONEGPUInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_)
{
	if(this->instanceId != UninitializedPoolId)
	{
		throw std::runtime_error("Level: already has an instance.");
	}
	this->instanceId = Loading::addONELevelInstance(shaderId_, rLevel_);
	this->sourceEngine = STH2005_ONE;
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
Which entity a type is made of is in the key (generated, see tools/genEntityKeys.py); a type it does not list (invisible triggers, effects, enemies without an archive entry) gets a marker instead (Graphics::collisionView).
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
				Graphics::collisionView.markers.push_back(Graphics::CollisionView::Marker{glm::vec3(yaw * glm::vec4(rPlacement.position, 1.0f)) + this->position, rPlacement.type, pLabel});
				continue;
			}
			if(found->entity.models.empty()) continue;
			const STH2005::EntityKey& rKey = found->entity;

			/*
			What a record makes (see PlacementLayout): Single, one entity per model of the key's first variant (just the first model if it has none); RingLine, one per ring; Variant, one per model of the variant the record
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
			if(rKey.layout == STH2005::PlacementLayout::RingLine)
			{
				for(const glm::vec3& rRingPosition : STH2005::readRingPositions(rPlacement)) spawns.push_back(Spawn{rRingPosition, glm::vec3(0.0f, rPlacement.rotation.y, 0.0f), 0, rKey.collisions});
			}
			else if(rKey.layout == STH2005::PlacementLayout::Variant)
			{
				if(rPlacement.params.size() < 4) throw std::runtime_error(std::format("Level: a record of {} has no variant parameter.", rKey.name));
				uint32_t variant = readLE<uint32_t>(rPlacement.params, 0);
				if(variant >= rKey.variants.size()) throw std::runtime_error(std::format("Level: a record of {} names variant {} of {}.", rKey.name, variant, rKey.variants.size()));
				for(uint32_t modelIndex : rKey.variants[variant])
				{
					std::vector<STH2005::CollisionKey> collisions;
					if(spawns.empty() && variant < rKey.collisions.size()) collisions.push_back(rKey.collisions[variant]);
					spawns.push_back(Spawn{rPlacement.position, rPlacement.rotation, modelIndex, collisions});
				}
			}
			else
			{
				/* The first variant (the .BIN's first info) is the object: all of its models, the first of them getting the collision. */
				std::vector<uint32_t> modelIndices = rKey.variants.empty() ? std::vector<uint32_t>{0} : rKey.variants[0];
				for(uint32_t modelIndex : modelIndices)
				{
					spawns.push_back(Spawn{rPlacement.position, rPlacement.rotation, modelIndex, spawns.empty() ? rKey.collisions : std::vector<STH2005::CollisionKey>{}});
				}
			}

			for(const Spawn& rSpawn : spawns)
			{
				glm::vec3 position = glm::vec3(yaw * glm::vec4(rSpawn.gamePosition, 1.0f)) + this->position;
				PoolId id = addEntity(std::format("{}#{}", rKey.name, this->entityIds.size()).c_str(), position);
				try
				{
					Entity& rEntity = getEntity(id);
					rEntity.setRotation(worldRotation(rSpawn.gameRotation));
					if(rKey.textures.entry != nullptr) rEntity.setONEGPUInstance(shaderId_, rKey.models.at(rSpawn.modelIndex), rKey.textures);
					else rEntity.setONEGPUInstance(shaderId_, rKey.models.at(rSpawn.modelIndex));
					if(!rSpawn.collisions.empty()) rEntity.setONECollisionShapes(rSpawn.collisions);

					/*
					Without behaviour nothing chooses a motion, so the first one the key lists (the generator puts one named idle, stand or fly first, if there is one) is played on the first model, looping for living things (the key's loopMotion: enemies and characters) and otherwise once, holding its last frame
					(placeholder: for TANK and TURNOVER it is the "_on" one, a state change). A motion that can't be read is reported and the entity stays posed at rest: the .MTP layout was decoded from SHADOW.MTP only.
					*/
					if(rSpawn.modelIndex == 0 && !rKey.motions.empty())
					{
						try
						{
							rEntity.playONEMotion(rKey.motions[0], rKey.loopMotion);
						}
						catch(const std::runtime_error& rError)
						{
							std::fprintf(stderr, "Level: %s (type %u) of %s has no motion: %s\n", rKey.name, static_cast<unsigned>(rPlacement.type), pPath, rError.what());
						}
					}
				}
				catch(const std::runtime_error& rError)
				{
					std::fprintf(stderr, "Level: skipped %s (type %u) of %s: %s\n", rKey.name, static_cast<unsigned>(rPlacement.type), pPath, rError.what());
					Globals::Game::entities.remove(id);
					continue;
				}
				this->entityIds.push_back(id);
			}
		}
	}
}

/*
The orientation an SADX SET record gives an object. External, from the decompile of _Main.rel: the objects' display code turns by the angles of their task (x at +0x14, y at +0x18, z at
+0x1C) with the Ninja rotation functions (the ones that build a rotation about 'x', 'y' or 'z' and multiply it into the current matrix on the right), and the order that comes up
most is z, then x, then y (92 functions with all three, 98 more with z and x); so the matrix is Rz * Rx * Ry, which turns the object about its own y first. Objects whose code differs have
a rotation recipe in their key (SAEntityKey::rotation, from the generator): e.g. the dash panel turns about y by the angle plus 0x8000, the launch board turns about y before x. The angles are BAMS (65536 = a turn).
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
Entities of an SADX stage's SET files, see Level.h, and of the objects its own code creates (SALandTableKey::stageEntities, placed as they are). Positions are the records' own (the land table and the objects share a space, observed on Emerald Coast: the SET positions are in
the land table's range) and the rotation is the record's three BAMS angles in the order the game's object code uses most (saObjectRotation). An object that
fails to load is reported on stderr and skipped, as in setONEEntities.
*/
void Level::setSAEntities(PoolId shaderId_, const SALandTableKey& rLandTable_)
{
	if(!this->entityIds.empty())
	{
		throw std::runtime_error("Level: already has entities.");
	}

	/* One entity: its model, textures, collision shapes and first motion. what_ names the source in a message. */
	auto createEntity = [&](const SAEntityKey& rKey_, const SAModelKey& rModel_, const std::vector<SACollisionShapesKey>& rCollisions_, const glm::vec3& rPosition_, const glm::mat4& rRotation_, const std::string& what_, const Ninja::SAPlacement* pPlacement_)
	{
		PoolId id = addEntity(std::format("{}#{}", rKey_.name, this->entityIds.size()).c_str(), rPosition_);
		try
		{
			Entity& rEntity = getEntity(id);
			rEntity.setRotation(rRotation_);
			if(rKey_.textures.path != nullptr) rEntity.setSAGPUInstance(shaderId_, rModel_, rKey_.textures);
			else rEntity.setSAGPUInstance(shaderId_, rModel_);
			if(!rCollisions_.empty()) rEntity.setSACollisionShapes(rCollisions_);
			SADXGameplay::setupObject(id, rKey_.name, pPlacement_);

			/* Without behaviour nothing chooses a motion: the first one the key lists loops (the generator puts one that looks like an idle first). A motion that can't be read is reported. */
			if(!rKey_.motions.empty())
			{
				try
				{
					rEntity.playSAMotion(rKey_.motions[0]);
				}
				catch(const std::runtime_error& rError)
				{
					std::fprintf(stderr, "Level: %s %s has no motion: %s\n", rKey_.name, what_.c_str(), rError.what());
				}
			}
		}
		catch(const std::runtime_error& rError)
		{
			std::fprintf(stderr, "Level: skipped %s %s: %s\n", rKey_.name, what_.c_str(), rError.what());
			Globals::Game::entities.remove(id);
			return;
		}
		this->entityIds.push_back(id);
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
				Graphics::collisionView.markers.push_back(Graphics::CollisionView::Marker{position, rPlacement.type, pLabel});
				continue;
			}
			const SAEntityKey& rKey = found->entity;
			uint32_t variantValue = static_cast<uint32_t>(static_cast<int32_t>(rPlacement.scale[rKey.variantField]) & 0xFF);
			const SAModelKey& rModel = rKey.variantModels.empty() ? rKey.model : rKey.variantModels[variantValue % rKey.variantModels.size()];
			const std::vector<SACollisionShapesKey>& rCollisions = rKey.variantCollisions.empty() ? rKey.collisions : rKey.variantCollisions[variantValue % rKey.variantCollisions.size()];
			if(rModel.rel == nullptr)
			{
				Graphics::collisionView.markers.push_back(Graphics::CollisionView::Marker{position, rPlacement.type, rKey.name});
				continue;
			}
			createEntity(rKey, rModel, rCollisions, position, saObjectRotation(rPlacement.rotation, rKey.rotation), std::format("(type {}) of {}", static_cast<unsigned>(rPlacement.type), pPath), &rPlacement);
		}
	}
	for(const SAStageEntityKey& rStageEntity : rLandTable_.stageEntities)
	{
		createEntity(*rStageEntity.entity, rStageEntity.entity->model, rStageEntity.entity->collisions, glm::vec3(rStageEntity.position[0], rStageEntity.position[1], rStageEntity.position[2]) + this->position, glm::mat4(1.0f), "(created by the stage)", nullptr);
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
	}
}

/* Removes the level's instance, which gives back the users of its pieces. Called by PoolMap::remove. */
void Level::destroy()
{
	for(PoolId id : this->entityIds)
	{
		if(entityExists(id)) Globals::Game::entities.remove(id);
	}
	this->entityIds.clear();
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
	}
	this->instanceId = UninitializedPoolId;
}
