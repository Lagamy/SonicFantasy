#include "LevelLoader.h"
#include "ResourceKeys.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include "Level.h"
#include "API.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include "Prefab.h"
#ifdef EDITOR
#include "ExternalAssets.h"
#include "ExternalPrefabs.h"
#include "GLTFImport.h"
#endif
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <format>

namespace Loading
{
namespace
{
using Json = nlohmann::json;

/*
The generated tables a level file can name (see the "entityTable", "typeNames" and "stageEntities" fields in docs/ENGINE_USAGE.md). A new generated
table is one line here.
*/
const std::map<std::string, const std::vector<SAPlacedEntityKey>*> saEntityTables{
	{"SAEntityTable_EmeraldCoast", &SAEntityTable_EmeraldCoast}
};
const std::map<std::string, const std::vector<SAStageEntityKey>*> saStageEntities{
	{"EmeraldCoastStageEntities", &EmeraldCoastStageEntities}
};
const std::map<std::string, const std::map<uint16_t, const char*>*> typeNameMaps{
	{"SAObjectTypeNames_EmeraldCoast", &SAObjectTypeNames_EmeraldCoast},
	{"ObjectTypeNames", &ObjectTypeNames}
};
const std::map<std::string, const std::vector<STH2005::PlacedEntityKey>*> oneEntityTables{
	{"EntityTable_stg0100", &EntityTable_stg0100}
};

/* The keys of a level made from its file. The keys hold const char* and the instances keep pointers to them, so the strings live here and a record is never freed or moved (std::map nodes, std::deque). */
struct Recipe
{
	std::deque<std::string> strings;
	std::unique_ptr<SALandTableKey> saLandTable;
	std::unique_ptr<SATextureSetKey> saTextures;
	std::unique_ptr<STH2005::LevelKey> oneLevel;
	std::unique_ptr<GLTFLevelKey> gltfLevel;
	glm::vec3 position{0.0f};
	PoolId shaderId;
};
std::map<std::string, Recipe> recipes;

const char* keepString(Recipe& rRecipe_, const std::string& value_)
{
	rRecipe_.strings.push_back(value_);
	return rRecipe_.strings.back().c_str();
}

template<typename Table>
const Table& findTable(const std::map<std::string, const Table*>& rTables_, const Json& rFile_, const char* field_)
{
	std::string name = rFile_.at(field_).get<std::string>();
	auto found = rTables_.find(name);
	if(found == rTables_.end()) throw std::runtime_error(std::format("'{}' is not a table LevelLoader.cpp knows ({}).", name, field_));
	return *found->second;
}

std::vector<const char*> keepStrings(Recipe& rRecipe_, const Json& rFile_, const char* field_)
{
	std::vector<const char*> result;
	if(!rFile_.contains(field_)) return result;
	for(const Json& rValue : rFile_.at(field_)) result.push_back(keepString(rRecipe_, rValue.get<std::string>()));
	return result;
}

/* A number, or a string in any base ("0x1B8D68"): offsets are written in hex, like everywhere in the docs. */
uint32_t readOffset(const Json& rValue_)
{
	if(rValue_.is_string()) return static_cast<uint32_t>(std::stoul(rValue_.get<std::string>(), nullptr, 0));
	return rValue_.get<uint32_t>();
}

void fillSA(Recipe& rRecipe_, const Json& rFile_)
{
	const Json& rLandTable = rFile_.at("landTable");
	std::vector<std::string> skippedTextures;
	if(rFile_.contains("skippedTextures")) skippedTextures = rFile_.at("skippedTextures").get<std::vector<std::string>>();
	rRecipe_.saLandTable = std::make_unique<SALandTableKey>(SALandTableKey{keepString(rRecipe_, rLandTable.at("rel").get<std::string>()), readOffset(rLandTable.at("offset")), skippedTextures, keepStrings(rRecipe_, rFile_, "placementFiles")});
	SALandTableKey& rKey = *rRecipe_.saLandTable;
	if(rFile_.contains("entityTable")) rKey.entities = findTable(saEntityTables, rFile_, "entityTable");
	if(rFile_.contains("typeNames")) rKey.typeNames = &findTable(typeNameMaps, rFile_, "typeNames");
	if(rFile_.contains("stageEntities")) rKey.stageEntities = findTable(saStageEntities, rFile_, "stageEntities");
	rRecipe_.saTextures = std::make_unique<SATextureSetKey>(SATextureSetKey{keepString(rRecipe_, rFile_.at("textures").get<std::string>())});
}

void fillONE(Recipe& rRecipe_, const Json& rFile_)
{
	std::vector<STH2005::ArchiveKey> sections;
	for(const Json& rSection : rFile_.at("sections")) sections.push_back(STH2005::ArchiveKey{keepString(rRecipe_, rSection.get<std::string>())});
	const Json& rTextures = rFile_.at("textures");
	STH2005::TextureKey textures{{keepString(rRecipe_, rTextures.at("archive").get<std::string>())}, keepString(rRecipe_, rTextures.at("txd").get<std::string>())};
	std::vector<std::string> skippedEntries;
	if(rFile_.contains("skippedEntries")) skippedEntries = rFile_.at("skippedEntries").get<std::vector<std::string>>();
	rRecipe_.oneLevel = std::make_unique<STH2005::LevelKey>(STH2005::LevelKey{sections, textures, skippedEntries, keepStrings(rRecipe_, rFile_, "placementFiles")});
	STH2005::LevelKey& rKey = *rRecipe_.oneLevel;
	if(rFile_.contains("entityTable")) rKey.entities = findTable(oneEntityTables, rFile_, "entityTable");
	if(rFile_.contains("typeNames")) rKey.typeNames = &findTable(typeNameMaps, rFile_, "typeNames");
}

/* The level's visual is every chunk in its folder; "collisions" names the collision files (Collision/<name>.collision.json) that make up its collision. */
void fillGLTF(Recipe& rRecipe_, const Json& rFile_, const std::string& rName_)
{
	rRecipe_.gltfLevel = std::make_unique<GLTFLevelKey>();
	rRecipe_.gltfLevel->levelName = rName_;
	if(rFile_.contains("collisions")) rRecipe_.gltfLevel->collisions = rFile_.at("collisions").get<std::vector<std::string>>();
}

/* A level is a folder: <root><name>/<name>.json next to the level's own Models, Textures, Collision and Prefabs (root: "Assets/Levels/", or the source tree's Levels/ in the editor). */
std::string levelFilePath(const std::string& root_, const std::string& name_)
{
	return root_ + name_ + "/" + name_ + ".json";
}

/* Reads Assets/Levels/<name>/<name>.json into the recipe of the name, once. */
void readRecipe(const std::string& name_, Recipe& rRecipe_)
{
	std::string path = levelFilePath("Assets/Levels/", name_);
	std::ifstream stream(path);
	if(!stream) throw std::runtime_error(std::format("unknown level '{}' ({} not found).", name_, path));
	Json file = Json::parse(stream);
	std::string engine = file.at("engine").get<std::string>();
	rRecipe_.shaderId = Graphics::shaders.nameToId.at(file.at("shader").get<std::string>());
	if(file.contains("position"))
	{
		std::vector<float> position = file.at("position").get<std::vector<float>>();
		if(position.size() != 3) throw std::runtime_error("position needs 3 numbers.");
		rRecipe_.position = glm::vec3(position[0], position[1], position[2]);
	}
	if(engine == "sadx") fillSA(rRecipe_, file);
	else if(engine == "sth2005") fillONE(rRecipe_, file);
	else if(engine == "gltf") fillGLTF(rRecipe_, file, name_);
	else throw std::runtime_error(std::format("unknown engine '{}' (\"sadx\", \"sth2005\" or \"gltf\").", engine));
}
}

namespace
{
/* The position of a level file's edit, a world position. */
glm::vec3 readPosition(const Json& rValue_)
{
	std::vector<float> position = rValue_.get<std::vector<float>>();
	if(position.size() != 3) throw std::runtime_error("an edit's position needs 3 numbers.");
	return glm::vec3(position[0], position[1], position[2]);
}

/* A level file edit's rotation: the 9 numbers of the entity's 3x3 orientation matrix, column by column. */
glm::mat4 readRotation(const Json& rValue_)
{
	std::vector<float> values = rValue_.get<std::vector<float>>();
	if(values.size() != 9) throw std::runtime_error("an edit's rotation needs 9 numbers.");
	return glm::mat4(glm::make_mat3(values.data()));
}

/* A bound area shape as the level file has it: every field of Physics::CollisionShape, the behaviour by name (null: none). */
nlohmann::ordered_json writeShape(const Physics::CollisionShape& rShape_)
{
	nlohmann::ordered_json json;
	json["kind"] = rShape_.kind;
	json["form"] = rShape_.form;
	json["push"] = rShape_.push;
	json["damage"] = rShape_.damage;
	json["attr"] = rShape_.attr;
	json["center"] = nlohmann::ordered_json::array({rShape_.center.x, rShape_.center.y, rShape_.center.z});
	json["a"] = rShape_.a;
	json["b"] = rShape_.b;
	json["c"] = rShape_.c;
	json["d"] = rShape_.d;
	json["angle"] = nlohmann::ordered_json::array({rShape_.angle.x, rShape_.angle.y, rShape_.angle.z});
	if(rShape_.behavior == UninitializedPoolId) json["behavior"] = nullptr;
	else json["behavior"] = Globals::Game::behaviors.names[rShape_.behavior.id];
	return json;
}

/* A texture choice as {"kind": "default"|"disc"|"external", "name": ...}. */
nlohmann::ordered_json writeTextureChoice(const TextureChoice& rChoice_)
{
	const char* kind = rChoice_.kind == TextureChoiceKind::Disc ? "disc" : rChoice_.kind == TextureChoiceKind::External ? "external" : "default";
	return {{"kind", kind}, {"name", rChoice_.name}};
}

TextureChoice readTextureChoice(const Json& rJson_)
{
	std::string kind = rJson_.at("kind").get<std::string>();
	TextureChoice choice;
	if(kind == "disc") choice.kind = TextureChoiceKind::Disc;
	else if(kind == "external") choice.kind = TextureChoiceKind::External;
	else if(kind != "default") throw std::runtime_error(std::format("a texture's kind '{}' is not default, disc or external.", kind));
	choice.name = rJson_.value("name", std::string());
	return choice;
}

/* A model choice as {"kind": "default"|"disc"|"external", "name": ...}. */
nlohmann::ordered_json writeMeshChoice(const MeshChoice& rChoice_)
{
	const char* kind = rChoice_.kind == MeshChoiceKind::Disc ? "disc" : rChoice_.kind == MeshChoiceKind::External ? "external" : "default";
	return {{"kind", kind}, {"name", rChoice_.name}};
}

MeshChoice readMeshChoice(const Json& rJson_)
{
	std::string kind = rJson_.at("kind").get<std::string>();
	MeshChoice choice;
	if(kind == "disc") choice.kind = MeshChoiceKind::Disc;
	else if(kind == "external") choice.kind = MeshChoiceKind::External;
	else if(kind != "default") throw std::runtime_error(std::format("a model's kind '{}' is not default, disc or external.", kind));
	choice.name = rJson_.value("name", std::string());
	return choice;
}

/* Gives an entity an edit's model; one that can't be used (an external model that is gone, a disc model that does not fit the entity) is reported on stderr and the entity keeps its own. */
void applyMeshChoice(PoolId entityId_, const MeshChoice& rChoice_, const std::string& rLevel_)
{
	try
	{
		setEntityMesh(entityId_, rChoice_);
	}
	catch(const std::runtime_error& rError)
	{
		fprintf(stderr, "Level '%s': an edit's model '%s' is not used: %s.\n", rLevel_.c_str(), rChoice_.name.c_str(), rError.what());
	}
}

/* Gives an entity an edit's texture; a texture that can't be used (an external file that is gone, a disc texture that does not fit the model) is reported on stderr and the entity keeps its own. */
void applyTextureChoice(PoolId entityId_, const TextureChoice& rChoice_, const std::string& rLevel_)
{
	try
	{
		setEntityTexture(entityId_, rChoice_);
	}
	catch(const std::runtime_error& rError)
	{
		fprintf(stderr, "Level '%s': an edit's texture '%s' is not used: %s.\n", rLevel_.c_str(), rChoice_.name.c_str(), rError.what());
	}
}

/* The shape of writeShape; a collision behaviour name nothing registered is reported on stderr and the shape gets none. */
Physics::CollisionShape readShape(const Json& rJson_, const std::string& rLevel_)
{
	Physics::CollisionShape shape{};
	shape.kind = rJson_.at("kind").get<uint8_t>();
	shape.form = rJson_.at("form").get<uint8_t>();
	shape.push = rJson_.at("push").get<uint8_t>();
	shape.damage = rJson_.at("damage").get<uint8_t>();
	shape.attr = rJson_.at("attr").get<uint32_t>();
	shape.center = readPosition(rJson_.at("center"));
	shape.a = rJson_.at("a").get<float>();
	shape.b = rJson_.at("b").get<float>();
	shape.c = rJson_.at("c").get<float>();
	shape.d = rJson_.at("d").get<float>();
	std::vector<int32_t> angle = rJson_.at("angle").get<std::vector<int32_t>>();
	if(angle.size() != 3) throw std::runtime_error("a shape's angle needs 3 numbers.");
	shape.angle = glm::ivec3(angle[0], angle[1], angle[2]);
	shape.behavior = UninitializedPoolId;
	if(!rJson_.at("behavior").is_null())
	{
		std::string name = rJson_.at("behavior").get<std::string>();
		auto found = Globals::Game::behaviors.nameToId.find(name);
		if(found == Globals::Game::behaviors.nameToId.end()) fprintf(stderr, "Level '%s': an edit names the unknown collision behaviour '%s'.\n", rLevel_.c_str(), name.c_str());
		else shape.behavior = found->second;
	}
	return shape;
}

/* The behaviour with that name, UninitializedPoolId (reported on stderr) if no script registered it. */
PoolId findBehavior(const std::string& rName_, const std::string& rLevel_)
{
	auto found = Globals::Game::entityBehaviorTypes.nameToId.find(rName_);
	if(found == Globals::Game::entityBehaviorTypes.nameToId.end())
	{
		fprintf(stderr, "Level '%s': an edit names the unknown behaviour '%s'.\n", rLevel_.c_str(), rName_.c_str());
		return UninitializedPoolId;
	}
	return found->second;
}

/*
Applies the "edits" of the level's file (see saveLevelEdits) to the level just made, once its own entities are marked as the source: the moved ones are put at their position, the added ones are made from their prefab and
owned by the level. The file is read again (not the recipe's copy): a saved edit is in the file the next time the level is loaded. An edit that does not fit (an index past the entities, an unknown prefab) is reported on stderr and skipped.
*/
void applyEdits(Level& rLevel_, const std::string& name_, PoolId shaderId_)
{
	std::ifstream stream(levelFilePath("Assets/Levels/", name_));
	if(!stream) return;
	Json file = Json::parse(stream);
	if(!file.contains("edits")) return;
	const Json& rEdits = file.at("edits");

	/* "prefabs": {"<prefab>": {"added": [behaviour names], "removed": [...], "shapesAdded": [shapes], "shapesRemoved": [...], "mesh": choice, "texture": choice}}, the behaviours and bound area shapes given to or taken from every entity of a prefab and the model and texture all of them get; applied first, so the per-entity ones below are differences from the result. */
	if(rEdits.contains("prefabs"))
	{
		for(const auto& [prefabName, rLists] : rEdits.at("prefabs").items())
		{
			auto prefab = Globals::Game::prefabs.nameToId.find(prefabName);
			if(prefab == Globals::Game::prefabs.nameToId.end())
			{
				fprintf(stderr, "Level '%s': an edit changes the unknown prefab '%s'.\n", name_.c_str(), prefabName.c_str());
				continue;
			}
			for(const Json& rName : rLists.value("added", Json::array()))
			{
				PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
				if(behaviorId != UninitializedPoolId) addPrefabBehavior(prefab->second, behaviorId);
			}
			for(const Json& rName : rLists.value("removed", Json::array()))
			{
				PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
				if(behaviorId != UninitializedPoolId) removePrefabBehavior(prefab->second, behaviorId);
			}
			for(const Json& rShape : rLists.value("shapesAdded", Json::array())) addPrefabShape(prefab->second, readShape(rShape, name_));
			for(const Json& rShape : rLists.value("shapesRemoved", Json::array())) removePrefabShape(prefab->second, readShape(rShape, name_));
			/* An External prefab's model and texture are saved with the prefab (ExternalPrefabs.h), not here. */
			bool ownFile = Globals::Game::prefabs[prefab->second].kind == PrefabKind::External;
			if(!ownFile && rLists.contains("mesh"))
			{
				MeshChoice choice = readMeshChoice(rLists.at("mesh"));
				if(setPrefabMesh(prefab->second, choice) != 0) fprintf(stderr, "Level '%s': the model '%s' of prefab '%s' does not fit some of its entities.\n", name_.c_str(), choice.name.c_str(), prefabName.c_str());
			}
			if(!ownFile && rLists.contains("texture"))
			{
				TextureChoice choice = readTextureChoice(rLists.at("texture"));
				if(setPrefabTexture(prefab->second, choice) != 0) fprintf(stderr, "Level '%s': the texture '%s' of prefab '%s' does not fit some of its entities.\n", name_.c_str(), choice.name.c_str(), prefabName.c_str());
			}
		}
	}

	/* "behaviors": [{"index", "added", "removed"}], what one of the level's own entities has more or less than the game's data and the prefab edits give it. */
	if(rEdits.contains("behaviors"))
	{
		for(const Json& rEntry : rEdits.at("behaviors"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(index >= rLevel_.sourceEntityCount || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit changes the behaviours of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			PoolId entityId = rLevel_.entityIds[index];
			for(const Json& rName : rEntry.value("removed", Json::array()))
			{
				PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
				if(behaviorId != UninitializedPoolId) removeEntityBehavior(entityId, behaviorId);
			}
			for(const Json& rName : rEntry.value("added", Json::array()))
			{
				PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
				if(behaviorId != UninitializedPoolId) giveEntityBehavior(entityId, behaviorId);
			}
		}
	}
	/* "shapes": [{"index", "shapes": [...]}], the whole bound area of one of the level's own entities when it differs from the game's data plus the prefab edits. */
	if(rEdits.contains("shapes"))
	{
		for(const Json& rEntry : rEdits.at("shapes"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(index >= rLevel_.sourceEntityCount || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit changes the bound area of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			std::vector<Physics::CollisionShape> shapes;
			for(const Json& rShape : rEntry.at("shapes")) shapes.push_back(readShape(rShape, name_));
			setEntityShapes(rLevel_.entityIds[index], shapes);
		}
	}
	/* "meshes": [{"index", "mesh": choice}], the model of one of the level's own entities when it differs from its prefab's; applied before the textures. */
	if(rEdits.contains("meshes"))
	{
		for(const Json& rEntry : rEdits.at("meshes"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(index >= rLevel_.sourceEntityCount || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit changes the model of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			applyMeshChoice(rLevel_.entityIds[index], readMeshChoice(rEntry.at("mesh")), name_);
		}
	}
	/* "textures": [{"index", "texture": choice}], the texture of one of the level's own entities when it differs from its prefab's. */
	if(rEdits.contains("textures"))
	{
		for(const Json& rEntry : rEdits.at("textures"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(index >= rLevel_.sourceEntityCount || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit changes the texture of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			applyTextureChoice(rLevel_.entityIds[index], readTextureChoice(rEntry.at("texture")), name_);
		}
	}
	if(rEdits.contains("moved"))
	{
		for(const Json& rMoved : rEdits.at("moved"))
		{
			uint32_t index = rMoved.at("index").get<uint32_t>();
			if(index >= rLevel_.sourceEntityCount || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit moves entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			getEntity(rLevel_.entityIds[index]).setPosition(readPosition(rMoved.at("position")));
		}
	}
	/* "transforms": [{"index", "rotation", "scale"}], the rotation and scale of one of the level's own entities when either differs from the game's data. */
	if(rEdits.contains("transforms"))
	{
		for(const Json& rEntry : rEdits.at("transforms"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(index >= rLevel_.sourceEntityCount || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit turns or scales entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			Entity& rEntity = getEntity(rLevel_.entityIds[index]);
			rEntity.setRotation(readRotation(rEntry.at("rotation")));
			rEntity.setScale(readPosition(rEntry.at("scale")));
		}
	}
	if(rEdits.contains("added"))
	{
		for(const Json& rAdded : rEdits.at("added"))
		{
			std::string prefabName = rAdded.at("prefab").get<std::string>();
			auto prefab = Globals::Game::prefabs.nameToId.find(prefabName);
			if(prefab == Globals::Game::prefabs.nameToId.end())
			{
				fprintf(stderr, "Level '%s': an edit adds the unknown prefab '%s'.\n", name_.c_str(), prefabName.c_str());
				continue;
			}
			SpawnParams params;
			params.position = readPosition(rAdded.at("position"));
			params.what = "(level file edit)";
			PoolId id = spawnPrefab(prefab->second, shaderId_, params);
			if(id == UninitializedPoolId) continue;
			rLevel_.entityIds.push_back(id);

			/* An added entity keeps its whole list: there is no source to take a difference from. */
			if(rAdded.contains("behaviors"))
			{
				std::vector<PoolId> behaviorIds;
				for(const Json& rName : rAdded.at("behaviors"))
				{
					PoolId behaviorId = findBehavior(rName.get<std::string>(), name_);
					if(behaviorId != UninitializedPoolId) behaviorIds.push_back(behaviorId);
				}
				setEntityBehaviors(id, behaviorIds);
			}
			if(rAdded.contains("shapes"))
			{
				std::vector<Physics::CollisionShape> shapes;
				for(const Json& rShape : rAdded.at("shapes")) shapes.push_back(readShape(rShape, name_));
				setEntityShapes(id, shapes);
			}
			if(rAdded.contains("rotation")) getEntity(id).setRotation(readRotation(rAdded.at("rotation")));
			if(rAdded.contains("scale")) getEntity(id).setScale(readPosition(rAdded.at("scale")));
			if(rAdded.contains("mesh")) applyMeshChoice(id, readMeshChoice(rAdded.at("mesh")), name_);
			if(rAdded.contains("texture")) applyTextureChoice(id, readTextureChoice(rAdded.at("texture")), name_);
		}
	}
}
}

PoolId loadLevel(const std::string& name_)
{
	bool known = recipes.contains(name_);
	Recipe& rRecipe = recipes[name_];
	if(!known)
	{
		try
		{
			readRecipe(name_, rRecipe);
		}
		catch(const std::exception& rError)
		{
			recipes.erase(name_);
			throw std::runtime_error(std::format("Level '{}': {}", name_, rError.what()));
		}
	}

#ifdef EDITOR
	/* The editor opens a level: register the external assets (editor-wide and this level's folder) again before the level exists. */
	scanExternalAssets(name_);
#endif

	/* The entities are made from the game's data alone (Level::sourceBehaviors); the file's prefab edits are applied after them (applyEdits). */
	clearPrefabEdits();

	PoolId id = addLevel(name_.c_str(), rRecipe.position);
	try
	{
		Level& rLevel = getLevel(id);
#ifdef EDITOR
		/* Before the edits: they may name the level's own External prefabs. */
		loadExternalPrefabs(ExternalScope::Level, name_);
#endif
		if(rRecipe.saLandTable != nullptr)
		{
			rLevel.setSAGPUInstance(rRecipe.shaderId, *rRecipe.saLandTable, *rRecipe.saTextures);
			rLevel.setSACollision(*rRecipe.saLandTable);
			rLevel.setSAEntities(rRecipe.shaderId, *rRecipe.saLandTable);
		}
		else if(rRecipe.gltfLevel != nullptr)
		{
			rLevel.setGLTFGPUInstance(rRecipe.shaderId, *rRecipe.gltfLevel);
			rLevel.setGLTFCollision(*rRecipe.gltfLevel);
		}
		else
		{
			rLevel.setONEGPUInstance(rRecipe.shaderId, *rRecipe.oneLevel);
			rLevel.setONECollision(*rRecipe.oneLevel);
			rLevel.setONEEntities(rRecipe.shaderId, *rRecipe.oneLevel);
		}
		rLevel.markSourceEntities();
		applyEdits(rLevel, name_, rRecipe.shaderId);
	}
	catch(...)
	{
		Globals::Game::levels.remove(id);
		throw;
	}
	Globals::Game::spatialIndex.rebuild();
	return id;
}

void forEachSAEntityKey(const std::function<void(const SAEntityKey&)>& rFunction_)
{
	for(const auto& [tableName, pTable] : saEntityTables)
	{
		for(const SAPlacedEntityKey& rPlaced : *pTable) rFunction_(rPlaced.entity);
	}
	for(const auto& [tableName, pTable] : saStageEntities)
	{
		for(const SAStageEntityKey& rStage : *pTable) rFunction_(*rStage.entity);
	}
}

void forEachONEEntityKey(const std::function<void(const STH2005::EntityKey&)>& rFunction_)
{
	for(const auto& [tableName, pTable] : oneEntityTables)
	{
		for(const STH2005::PlacedEntityKey& rPlaced : *pTable) rFunction_(rPlaced.entity);
	}
}

#ifdef EDITOR
void saveLevelEdits(PoolId levelId_, const std::string& fileName_)
{
	const Level& rLevel = getLevel(levelId_);
	std::string levelName = Globals::Game::levels.names[levelId_.id];
	std::ifstream stream(levelFilePath("Assets/Levels/", levelName));
	if(!stream) throw std::runtime_error(std::format("{} not found.", levelFilePath("Assets/Levels/", levelName)));

	/* ordered_json keeps the file's key order, so a save only changes the edits. */
	nlohmann::ordered_json file = nlohmann::ordered_json::parse(stream);
	stream.close();
	file.erase("edits");
	auto position = [](const glm::vec3& rPosition_) { return nlohmann::ordered_json::array({rPosition_.x, rPosition_.y, rPosition_.z}); };
	auto rotation = [](const glm::mat4& rRotation_)
	{
		glm::mat3 matrix(rRotation_);
		return nlohmann::ordered_json::array({matrix[0].x, matrix[0].y, matrix[0].z, matrix[1].x, matrix[1].y, matrix[1].z, matrix[2].x, matrix[2].y, matrix[2].z});
	};
	nlohmann::ordered_json moved = nlohmann::ordered_json::array();
	nlohmann::ordered_json transforms = nlohmann::ordered_json::array();
	nlohmann::ordered_json added = nlohmann::ordered_json::array();
	for(uint32_t i = 0; i < rLevel.entityIds.size(); i++)
	{
		if(!entityExists(rLevel.entityIds[i])) continue;
		const Entity& rEntity = getEntity(rLevel.entityIds[i]);
		if(i < rLevel.sourceEntityCount)
		{
			if(rEntity.position != rLevel.sourcePositions[i]) moved.push_back({{"index", i}, {"position", position(rEntity.position)}, {"source", position(rLevel.sourcePositions[i])}});
			if(rEntity.rotation != rLevel.sourceRotations[i] || rEntity.scale != rLevel.sourceScales[i]) transforms.push_back({{"index", i}, {"rotation", rotation(rEntity.rotation)}, {"scale", position(rEntity.scale)}});
		}
		else if(rEntity.prefabId != UninitializedPoolId)
		{
			nlohmann::ordered_json names = nlohmann::ordered_json::array();
			if(rEntity.behaviorSetId != UninitializedPoolId)
			{
				for(PoolId behaviorId : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds) names.push_back(Globals::Game::entityBehaviorTypes.names[behaviorId.id]);
			}
			nlohmann::ordered_json shapes = nlohmann::ordered_json::array();
			for(const Physics::CollisionShape& rShape : entityShapes(rEntity.selfId)) shapes.push_back(writeShape(rShape));
			added.push_back({{"prefab", Globals::Game::prefabs.names[rEntity.prefabId.id]}, {"position", position(rEntity.position)}, {"behaviors", names}, {"shapes", shapes}});
			if(rEntity.rotation != glm::mat4(1.0f)) added.back()["rotation"] = rotation(rEntity.rotation);
			if(rEntity.scale != glm::vec3(1.0f)) added.back()["scale"] = position(rEntity.scale);
			if(rEntity.meshChoice != Globals::Game::prefabs[rEntity.prefabId].mesh) added.back()["mesh"] = writeMeshChoice(rEntity.meshChoice);
			if(rEntity.textureChoice != Globals::Game::prefabs[rEntity.prefabId].texture) added.back()["texture"] = writeTextureChoice(rEntity.textureChoice);
		}
	}

	/* What each of the level's own entities has more or less than the game's data plus the prefab edits give it (the same order of application as applyEdits), by name; an added entity keeps its whole list. The bound area is kept whole when it differs. */
	auto& rTypes = Globals::Game::entityBehaviorTypes;
	nlohmann::ordered_json prefabs = nlohmann::ordered_json::object();
	for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
	{
		if(!Globals::Game::prefabs.alive[i]) continue;
		const Prefab& rPrefab = Globals::Game::prefabs.objects[i];
		bool ownFile = rPrefab.kind == PrefabKind::External;
		if(rPrefab.addedBehaviors.empty() && rPrefab.removedBehaviors.empty() && rPrefab.addedShapes.empty() && rPrefab.removedShapes.empty() && (ownFile || (rPrefab.texture.kind == TextureChoiceKind::Default && rPrefab.mesh.kind == MeshChoiceKind::Default))) continue;
		nlohmann::ordered_json lists;
		lists["added"] = nlohmann::ordered_json::array();
		lists["removed"] = nlohmann::ordered_json::array();
		for(PoolId behaviorId : rPrefab.addedBehaviors) lists["added"].push_back(rTypes.names[behaviorId.id]);
		for(PoolId behaviorId : rPrefab.removedBehaviors) lists["removed"].push_back(rTypes.names[behaviorId.id]);
		lists["shapesAdded"] = nlohmann::ordered_json::array();
		lists["shapesRemoved"] = nlohmann::ordered_json::array();
		for(const Physics::CollisionShape& rShape : rPrefab.addedShapes) lists["shapesAdded"].push_back(writeShape(rShape));
		for(const Physics::CollisionShape& rShape : rPrefab.removedShapes) lists["shapesRemoved"].push_back(writeShape(rShape));
		if(!ownFile && rPrefab.mesh.kind != MeshChoiceKind::Default) lists["mesh"] = writeMeshChoice(rPrefab.mesh);
		if(!ownFile && rPrefab.texture.kind != TextureChoiceKind::Default) lists["texture"] = writeTextureChoice(rPrefab.texture);
		prefabs[Globals::Game::prefabs.names[i]] = lists;
	}
	nlohmann::ordered_json behaviors = nlohmann::ordered_json::array();
	nlohmann::ordered_json shapes = nlohmann::ordered_json::array();
	nlohmann::ordered_json textures = nlohmann::ordered_json::array();
	nlohmann::ordered_json meshes = nlohmann::ordered_json::array();
	for(uint32_t i = 0; i < rLevel.sourceEntityCount && i < rLevel.entityIds.size(); i++)
	{
		if(!entityExists(rLevel.entityIds[i])) continue;
		const Entity& rEntity = getEntity(rLevel.entityIds[i]);
		std::vector<PoolId> expected = rLevel.sourceBehaviors[i];
		if(rEntity.prefabId != UninitializedPoolId)
		{
			const Prefab& rPrefab = Globals::Game::prefabs[rEntity.prefabId];
			for(PoolId behaviorId : rPrefab.addedBehaviors)
			{
				if(std::find(expected.begin(), expected.end(), behaviorId) == expected.end()) expected.push_back(behaviorId);
			}
			for(PoolId behaviorId : rPrefab.removedBehaviors) expected.erase(std::remove(expected.begin(), expected.end(), behaviorId), expected.end());
		}
		std::vector<PoolId> current = rEntity.behaviorSetId == UninitializedPoolId ? std::vector<PoolId>() : Globals::Game::entityBehaviors[rEntity.behaviorSetId].behaviorIds;
		nlohmann::ordered_json addedNames = nlohmann::ordered_json::array();
		nlohmann::ordered_json removedNames = nlohmann::ordered_json::array();
		std::vector<PoolId> left = expected;
		for(PoolId behaviorId : current)
		{
			auto found = std::find(left.begin(), left.end(), behaviorId);
			if(found == left.end()) addedNames.push_back(rTypes.names[behaviorId.id]);
			else left.erase(found);
		}
		for(PoolId behaviorId : left) removedNames.push_back(rTypes.names[behaviorId.id]);
		if(!addedNames.empty() || !removedNames.empty()) behaviors.push_back({{"index", i}, {"added", addedNames}, {"removed", removedNames}});

		std::vector<Physics::CollisionShape> expectedShapes = rLevel.sourceShapes[i];
		if(rEntity.prefabId != UninitializedPoolId) applyPrefabShapesTo(Globals::Game::prefabs[rEntity.prefabId], expectedShapes);
		std::vector<Physics::CollisionShape> currentShapes = entityShapes(rEntity.selfId);
		if(currentShapes != expectedShapes)
		{
			nlohmann::ordered_json shapeList = nlohmann::ordered_json::array();
			for(const Physics::CollisionShape& rShape : currentShapes) shapeList.push_back(writeShape(rShape));
			shapes.push_back({{"index", i}, {"shapes", shapeList}});
		}

		MeshChoice expectedMesh = rEntity.prefabId != UninitializedPoolId ? Globals::Game::prefabs[rEntity.prefabId].mesh : MeshChoice{};
		if(rEntity.meshChoice != expectedMesh) meshes.push_back({{"index", i}, {"mesh", writeMeshChoice(rEntity.meshChoice)}});
		TextureChoice expectedTexture = rEntity.prefabId != UninitializedPoolId ? Globals::Game::prefabs[rEntity.prefabId].texture : TextureChoice{};
		if(rEntity.textureChoice != expectedTexture) textures.push_back({{"index", i}, {"texture", writeTextureChoice(rEntity.textureChoice)}});
	}
	if(!moved.empty() || !added.empty() || !prefabs.empty() || !behaviors.empty() || !shapes.empty() || !textures.empty() || !meshes.empty() || !transforms.empty())
	{
		file["edits"] = {{"moved", moved}, {"added", added}};
		if(!prefabs.empty()) file["edits"]["prefabs"] = prefabs;
		if(!behaviors.empty()) file["edits"]["behaviors"] = behaviors;
		if(!shapes.empty()) file["edits"]["shapes"] = shapes;
		if(!transforms.empty()) file["edits"]["transforms"] = transforms;
		if(!meshes.empty()) file["edits"]["meshes"] = meshes;
		if(!textures.empty()) file["edits"]["textures"] = textures;
	}

	/*
	Saving under another name copies the level's whole folder first (its Models, Textures, Collision and Prefabs go with it), then writes the file under the new name. The copy of the old file is removed. A glTF level's chunk files are named after the level, so
	the copy of such a level does not find them (the same as before levels were folders).
	*/
	std::string text = file.dump(2);
	for(const std::string& rRoot : {std::string("Assets/Levels/"), std::string(SF_SOURCE_DIR "/Levels/")})
	{
		namespace fs = std::filesystem;
		if(fileName_ != levelName && fs::is_directory(rRoot + levelName))
		{
			fs::copy(rRoot + levelName, rRoot + fileName_, fs::copy_options::recursive | fs::copy_options::skip_existing);
			fs::remove(fs::path(rRoot + fileName_) / (levelName + ".json"));
		}
		fs::create_directories(rRoot + fileName_);
		std::ofstream out(levelFilePath(rRoot, fileName_));
		if(!out) throw std::runtime_error(std::format("can't write {}.", levelFilePath(rRoot, fileName_)));
		out << text;
	}
}

namespace
{
/* The items of a comma separated list, trimmed of spaces; empty items are dropped. */
std::vector<std::string> splitList(const std::string& text_)
{
	std::vector<std::string> result;
	size_t start = 0;
	while(start <= text_.size())
	{
		size_t end = text_.find(',', start);
		if(end == std::string::npos) end = text_.size();
		std::string item = text_.substr(start, end - start);
		size_t first = item.find_first_not_of(' ');
		size_t last = item.find_last_not_of(' ');
		if(first != std::string::npos) result.push_back(item.substr(first, last - first + 1));
		start = end + 1;
	}
	return result;
}
}

void createLevelFile(const NewLevelFile& rLevel_)
{
	if(rLevel_.name.empty()) throw std::runtime_error("the level needs a name.");
	for(char c : rLevel_.name)
	{
		if(!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') throw std::runtime_error("a level name can only have letters, digits, '_' and '-'.");
	}
	const std::string folders[] = {"Assets/Levels/", std::string(SF_SOURCE_DIR "/Levels/")};
	for(const std::string& rFolder : folders)
	{
		if(std::ifstream(levelFilePath(rFolder, rLevel_.name))) throw std::runtime_error(std::format("{} already exists.", levelFilePath(rFolder, rLevel_.name)));
	}

	nlohmann::ordered_json file;
	file["engine"] = rLevel_.engine;
	file["shader"] = rLevel_.shader;
	file["position"] = nlohmann::ordered_json::array({0, 0, 0});
	if(rLevel_.engine == "sadx")
	{
		if(rLevel_.landTableRel.empty() || rLevel_.landTableOffset.empty() || rLevel_.textures.empty()) throw std::runtime_error("an sadx level needs the land table's module and offset and the textures.");
		file["landTable"] = {{"rel", rLevel_.landTableRel}, {"offset", rLevel_.landTableOffset}};
		file["textures"] = rLevel_.textures;
	}
	else if(rLevel_.engine == "sth2005")
	{
		std::vector<std::string> sections = splitList(rLevel_.sections);
		if(sections.empty() || rLevel_.textures.empty() || rLevel_.txd.empty()) throw std::runtime_error("an sth2005 level needs sections and the texture archive with its txd.");
		file["sections"] = sections;
		file["textures"] = {{"archive", rLevel_.textures}, {"txd", rLevel_.txd}};
	}
	else if(rLevel_.engine == "gltf")
	{
		/* The glTF files are read now, once: the chunks and collision files are written to the level's folder (GLTFImport.h) and the level file only names the collision files. */
		if(rLevel_.model.empty()) throw std::runtime_error("a gltf level needs the model file.");
		importGLTFLevelModel(rLevel_.name, rLevel_.model);
		nlohmann::ordered_json collisions = nlohmann::ordered_json::array();
		for(const std::string& rPath : splitList(rLevel_.collisions)) collisions.push_back(importGLTFLevelCollision(rLevel_.name, rPath));
		file["collisions"] = collisions;
	}
	else throw std::runtime_error(std::format("unknown engine '{}' (\"sadx\", \"sth2005\" or \"gltf\").", rLevel_.engine));

	/* The placement files, entity table and type names are disc formats: a gltf level has none of them. */
	std::vector<std::string> placementFiles = splitList(rLevel_.placementFiles);
	if(rLevel_.engine != "gltf" && !placementFiles.empty()) file["placementFiles"] = placementFiles;
	if(rLevel_.engine != "gltf" && !rLevel_.entityTable.empty()) file["entityTable"] = rLevel_.entityTable;
	if(rLevel_.engine != "gltf" && !rLevel_.typeNames.empty()) file["typeNames"] = rLevel_.typeNames;
	if(!rLevel_.stageEntities.empty() && rLevel_.engine == "sadx") file["stageEntities"] = rLevel_.stageEntities;

	std::string text = file.dump(2);
	for(const std::string& rFolder : folders)
	{
		std::filesystem::create_directories(rFolder + rLevel_.name);
		std::ofstream out(levelFilePath(rFolder, rLevel_.name));
		if(!out) throw std::runtime_error(std::format("can't write {}.", levelFilePath(rFolder, rLevel_.name)));
		out << text;
	}
}

namespace
{
/* The glTF level the id names, with its recipe; throws if it is another engine. */
Recipe& gltfRecipeOf(PoolId levelId_)
{
	auto found = recipes.find(Globals::Game::levels.names[levelId_.id]);
	if(found == recipes.end() || found->second.gltfLevel == nullptr) throw std::runtime_error("the level is not a glTF level.");
	return found->second;
}

/* Makes the recipe's collision list the level's: written to "collisions" of both copies of the level file (the rest of the file as it is) and kept in the recipe, which a reload of the level uses. */
void writeCollisionList(Recipe& rRecipe_, const std::vector<std::string>& rNames_)
{
	const std::string& rName = rRecipe_.gltfLevel->levelName;
	for(const std::string& rFolder : {std::string("Assets/Levels/"), std::string(SF_SOURCE_DIR "/Levels/")})
	{
		std::ifstream stream(levelFilePath(rFolder, rName));
		if(!stream) throw std::runtime_error(std::format("{} not found.", levelFilePath(rFolder, rName)));
		nlohmann::ordered_json file = nlohmann::ordered_json::parse(stream);
		stream.close();
		file["collisions"] = rNames_;
		std::ofstream out(levelFilePath(rFolder, rName));
		if(!out) throw std::runtime_error(std::format("can't write {}.", levelFilePath(rFolder, rName)));
		out << file.dump(2);
	}
	rRecipe_.gltfLevel->collisions = rNames_;
}
}

void swapGLTFLevelModel(PoolId levelId_, const std::string& gltfPath_)
{
	Recipe& rRecipe = gltfRecipeOf(levelId_);
	importGLTFLevelModel(rRecipe.gltfLevel->levelName, gltfPath_);
	getLevel(levelId_).swapGLTFGPUInstance(rRecipe.shaderId, *rRecipe.gltfLevel);
}

std::string addGLTFLevelCollision(PoolId levelId_, const std::string& gltfPath_)
{
	Recipe& rRecipe = gltfRecipeOf(levelId_);
	std::string name = importGLTFLevelCollision(rRecipe.gltfLevel->levelName, gltfPath_);
	std::vector<std::string> names = rRecipe.gltfLevel->collisions;
	names.push_back(name);
	writeCollisionList(rRecipe, names);
	getLevel(levelId_).appendGLTFCollision(*rRecipe.gltfLevel, name);
	return name;
}

void swapGLTFLevelCollision(PoolId levelId_, const std::string& name_, const std::string& gltfPath_)
{
	Recipe& rRecipe = gltfRecipeOf(levelId_);
	replaceGLTFLevelCollision(rRecipe.gltfLevel->levelName, name_, gltfPath_);
	const std::vector<std::string>& rActive = rRecipe.gltfLevel->collisions;
	if(std::find(rActive.begin(), rActive.end(), name_) != rActive.end()) getLevel(levelId_).swapGLTFCollision(*rRecipe.gltfLevel);
}

std::vector<std::string> activeGLTFCollisions(PoolId levelId_)
{
	return gltfRecipeOf(levelId_).gltfLevel->collisions;
}

bool isGLTFLevel(PoolId levelId_)
{
	auto found = recipes.find(Globals::Game::levels.names[levelId_.id]);
	return found != recipes.end() && found->second.gltfLevel != nullptr;
}
#endif
}
