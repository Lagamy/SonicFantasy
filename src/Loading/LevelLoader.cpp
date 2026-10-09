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
The generated tables a level file can name (see the "entityTable" and "typeNames" fields in docs/ENGINE_USAGE.md). A new generated
table is one line here.
*/
const std::map<std::string, const std::vector<SAPlacedEntityKey>*> saEntityTables{
	{"SAEntityTable_EmeraldCoast", &SAEntityTable_EmeraldCoast}
};

/* Objects a stage's own code creates (no SET record): their keys, so they are prefabs; the level file's "source" says where they stand. */
const std::vector<const SAEntityKey*> saStageCreated{
	&EmeraldCoastBridgeDockForward, &EmeraldCoastBridgeDockBackward, &EmeraldCoastBridgePier, &EmeraldCoastBridgeLinks
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
	/* The level file's "skippedTextures" (sadx) or "skippedEntries" (sth2005): our choice of what not to load, not part of the disc keys. */
	std::vector<std::string> skipped;
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
	if(rFile_.contains("skippedTextures")) rRecipe_.skipped = rFile_.at("skippedTextures").get<std::vector<std::string>>();
	rRecipe_.saLandTable = std::make_unique<SALandTableKey>(SALandTableKey{keepString(rRecipe_, rLandTable.at("rel").get<std::string>()), readOffset(rLandTable.at("offset")), keepStrings(rRecipe_, rFile_, "placementFiles")});
	SALandTableKey& rKey = *rRecipe_.saLandTable;
	if(rFile_.contains("entityTable")) rKey.entities = findTable(saEntityTables, rFile_, "entityTable");
	if(rFile_.contains("typeNames")) rKey.typeNames = &findTable(typeNameMaps, rFile_, "typeNames");
	rRecipe_.saTextures = std::make_unique<SATextureSetKey>(SATextureSetKey{keepString(rRecipe_, rFile_.at("textures").get<std::string>())});
}

void fillONE(Recipe& rRecipe_, const Json& rFile_)
{
	std::vector<STH2005::ArchiveKey> sections;
	for(const Json& rSection : rFile_.at("sections")) sections.push_back(STH2005::ArchiveKey{keepString(rRecipe_, rSection.get<std::string>())});
	const Json& rTextures = rFile_.at("textures");
	STH2005::TextureKey textures{{keepString(rRecipe_, rTextures.at("archive").get<std::string>())}, keepString(rRecipe_, rTextures.at("txd").get<std::string>())};
	if(rFile_.contains("skippedEntries")) rRecipe_.skipped = rFile_.at("skippedEntries").get<std::vector<std::string>>();
	rRecipe_.oneLevel = std::make_unique<STH2005::LevelKey>(STH2005::LevelKey{sections, textures, keepStrings(rRecipe_, rFile_, "placementFiles")});
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
A prefab's edits as a level file's "prefabs" entry and a Changed file have them: {"added": [behaviour names], "removed": [...], "shapesAdded": [shapes], "shapesRemoved": [...], "mesh": choice, "texture": choice}. The model and texture are
written only when withModel_ (an External prefab's are its definition, saved with the prefab).
*/
nlohmann::ordered_json writePrefabEdits(const PrefabEdits& rEdits_, bool withModel_)
{
	auto& rTypes = Globals::Game::entityBehaviorTypes;
	nlohmann::ordered_json lists;
	lists["added"] = nlohmann::ordered_json::array();
	lists["removed"] = nlohmann::ordered_json::array();
	for(PoolId behaviorId : rEdits_.addedBehaviors) lists["added"].push_back(rTypes.names[behaviorId.id]);
	for(PoolId behaviorId : rEdits_.removedBehaviors) lists["removed"].push_back(rTypes.names[behaviorId.id]);
	lists["shapesAdded"] = nlohmann::ordered_json::array();
	lists["shapesRemoved"] = nlohmann::ordered_json::array();
	for(const Physics::CollisionShape& rShape : rEdits_.addedShapes) lists["shapesAdded"].push_back(writeShape(rShape));
	for(const Physics::CollisionShape& rShape : rEdits_.removedShapes) lists["shapesRemoved"].push_back(writeShape(rShape));
	if(withModel_ && rEdits_.mesh.kind != MeshChoiceKind::Default) lists["mesh"] = writeMeshChoice(rEdits_.mesh);
	if(withModel_ && rEdits_.texture.kind != TextureChoiceKind::Default) lists["texture"] = writeTextureChoice(rEdits_.texture);
	return lists;
}

bool prefabEditsEmpty(const PrefabEdits& rEdits_, bool withModel_)
{
	bool lists = rEdits_.addedBehaviors.empty() && rEdits_.removedBehaviors.empty() && rEdits_.addedShapes.empty() && rEdits_.removedShapes.empty();
	return lists && (!withModel_ || (rEdits_.texture.kind == TextureChoiceKind::Default && rEdits_.mesh.kind == MeshChoiceKind::Default));
}

/* Fills the set from what writePrefabEdits wrote, without touching any entity (Changed files are read before any exists). A name nothing registered is reported (rWhere_ names the file) and skipped. */
void readPrefabEdits(PrefabEdits& rEdits_, const Json& rJson_, const std::string& rWhere_)
{
	for(const Json& rName : rJson_.value("added", Json::array()))
	{
		PoolId behaviorId = findBehavior(rName.get<std::string>(), rWhere_);
		if(behaviorId != UninitializedPoolId) rEdits_.addedBehaviors.push_back(behaviorId);
	}
	for(const Json& rName : rJson_.value("removed", Json::array()))
	{
		PoolId behaviorId = findBehavior(rName.get<std::string>(), rWhere_);
		if(behaviorId != UninitializedPoolId) rEdits_.removedBehaviors.push_back(behaviorId);
	}
	for(const Json& rShape : rJson_.value("shapesAdded", Json::array())) rEdits_.addedShapes.push_back(readShape(rShape, rWhere_));
	for(const Json& rShape : rJson_.value("shapesRemoved", Json::array())) rEdits_.removedShapes.push_back(readShape(rShape, rWhere_));
	if(rJson_.contains("mesh")) rEdits_.mesh = readMeshChoice(rJson_.at("mesh"));
	if(rJson_.contains("texture")) rEdits_.texture = readTextureChoice(rJson_.at("texture"));
}

/*
The external field values of an entity (Entity::externals) as the level file has them: {"<behaviour>": {"<field>": value}}, a number, bool or string as itself, a vec3 as [x, y, z], an entity as its index in the level (Level::editIndexOf; null if it is not
one of the level's) and a prefab as its name (null: none). A value of a declared field is written from its type; one the file had that no declared field took is written back from Entity::externalsKept.
*/
nlohmann::ordered_json writeExternals(const Entity& rEntity_, const Level& rLevel_)
{
	nlohmann::ordered_json json = nlohmann::ordered_json::object();
	for(const auto& [behaviorName, rValues] : rEntity_.externals)
	{
		auto behavior = Globals::Game::entityBehaviorTypes.nameToId.find(behaviorName);
		if(behavior == Globals::Game::entityBehaviorTypes.nameToId.end()) continue;
		nlohmann::ordered_json fields = nlohmann::ordered_json::object();
		for(const ExternalField& rField : Globals::Game::entityBehaviorTypes[behavior->second].fields)
		{
			auto found = rValues.find(rField.name);
			if(found == rValues.end()) continue;
			const ExternalValue& rValue = found->second;
			switch(rField.type)
			{
			case ExternalType::Number:
				fields[rField.name] = rValue.number;
				break;
			case ExternalType::Bool:
				fields[rField.name] = rValue.flag;
				break;
			case ExternalType::String:
				fields[rField.name] = rValue.text;
				break;
			case ExternalType::Vec3:
				fields[rField.name] = nlohmann::ordered_json::array({rValue.vector.x, rValue.vector.y, rValue.vector.z});
				break;
			case ExternalType::Entity:
			{
				int32_t index = rLevel_.editIndexOf(rValue.entity);
				fields[rField.name] = index >= 0 ? nlohmann::ordered_json(index) : nlohmann::ordered_json(nullptr);
				break;
			}
			case ExternalType::Prefab:
			{
				auto& rPrefabs = Globals::Game::prefabs;
				bool exists = rValue.prefab.id < rPrefabs.objects.size() && rPrefabs.alive[rValue.prefab.id] && rPrefabs.generation[rValue.prefab.id] == rValue.prefab.generation;
				fields[rField.name] = exists ? nlohmann::ordered_json(rPrefabs.names[rValue.prefab.id]) : nlohmann::ordered_json(nullptr);
				break;
			}
			}
		}
		if(!fields.empty()) json[behaviorName] = fields;
	}

	/* What no declared field took when the file was read (Entity::externalsKept) is written back as it was, unless a declared value of the same field was written above. */
	for(const auto& [behaviorName, rValues] : rEntity_.externalsKept)
	{
		for(const auto& [fieldName, rText] : rValues)
		{
			if(json.contains(behaviorName) && json[behaviorName].contains(fieldName)) continue;
			json[behaviorName][fieldName] = nlohmann::ordered_json::parse(rText);
		}
	}
	return json;
}

/* The values of writeExternals onto an entity. An entity value is left as entityIndex (Level::resolveExternalEntities turns it into the entity once all of them exist). What does not fit (an undeclared behaviour or field, a wrong type, an unknown prefab) is reported on stderr and kept as JSON text (Entity::externalsKept) so a save writes it back. */
void readExternals(const Json& rJson_, Entity& rEntity_, const std::string& rLevel_)
{
	for(const auto& [behaviorName, rFields] : rJson_.items())
	{
		auto behavior = Globals::Game::entityBehaviorTypes.nameToId.find(behaviorName);
		if(behavior == Globals::Game::entityBehaviorTypes.nameToId.end())
		{
			fprintf(stderr, "Level '%s': an edit sets fields of the unknown behaviour '%s'; kept as they are.\n", rLevel_.c_str(), behaviorName.c_str());
			for(const auto& [fieldName, rJson] : rFields.items()) rEntity_.externalsKept[behaviorName][fieldName] = rJson.dump();
			continue;
		}
		const std::vector<ExternalField>& rDeclared = Globals::Game::entityBehaviorTypes[behavior->second].fields;
		for(const auto& [fieldName, rJson] : rFields.items())
		{
			auto field = std::find_if(rDeclared.begin(), rDeclared.end(), [&](const ExternalField& rField_) { return rField_.name == fieldName; });
			if(field == rDeclared.end())
			{
				fprintf(stderr, "Level '%s': an edit sets the undeclared field '%s' of behaviour '%s'; kept as it is.\n", rLevel_.c_str(), fieldName.c_str(), behaviorName.c_str());
				rEntity_.externalsKept[behaviorName][fieldName] = rJson.dump();
				continue;
			}
			ExternalValue value;
			try
			{
				switch(field->type)
				{
				case ExternalType::Number:
					value.number = rJson.get<double>();
					break;
				case ExternalType::Bool:
					value.flag = rJson.get<bool>();
					break;
				case ExternalType::String:
					value.text = rJson.get<std::string>();
					break;
				case ExternalType::Vec3:
					value.vector = readPosition(rJson);
					break;
				case ExternalType::Entity:
					if(!rJson.is_null()) value.entityIndex = rJson.get<int32_t>();
					break;
				case ExternalType::Prefab:
					if(!rJson.is_null())
					{
						auto prefab = Globals::Game::prefabs.nameToId.find(rJson.get<std::string>());
						if(prefab == Globals::Game::prefabs.nameToId.end()) throw std::runtime_error("unknown prefab " + rJson.get<std::string>());
						value.prefab = prefab->second;
					}
					break;
				}
			}
			catch(const std::exception& rError)
			{
				fprintf(stderr, "Level '%s': the field '%s' of behaviour '%s' does not fit: %s; kept as it is.\n", rLevel_.c_str(), fieldName.c_str(), behaviorName.c_str(), rError.what());
				rEntity_.externalsKept[behaviorName][fieldName] = rJson.dump();
				continue;
			}
			rEntity_.externals[behaviorName][fieldName] = value;
		}
	}
}

/* A prefab's file in the level's Source Game Level under a root ("Assets/Levels/": the game's copy; the source tree's Levels/: what the next build copies back), by its pool name ("SADX/SPRING", "Level/<name>"). */
std::string sourcePrefabFile(const std::string& rRoot_, const std::string& rLevel_, const std::string& rPoolName_)
{
	return rRoot_ + rLevel_ + "/SourceGamePrefabs/" + rPoolName_ + "/prefab.json";
}

/* "added": [{"prefab", "position", ...}], the entities of a prefab the file adds to the level, after the ones it has; they are owned by the level. With keepIndices_ (the source set) an entry that can't be made leaves a gone entity in entityIds, so the entities after it keep the index the file counts them by. */
void addEditedEntities(Level& rLevel_, const Json& rEdits, const std::string& name_, PoolId shaderId_, bool keepIndices_)
{
	if(!rEdits.contains("added")) return;
	for(const Json& rAdded : rEdits.at("added"))
	{
		std::string prefabName = rAdded.at("prefab").get<std::string>();
		auto prefab = Globals::Game::prefabs.nameToId.find(prefabName);
		if(prefab == Globals::Game::prefabs.nameToId.end())
		{
			fprintf(stderr, "Level '%s': an edit adds the unknown prefab '%s'.\n", name_.c_str(), prefabName.c_str());
			if(keepIndices_) rLevel_.entityIds.push_back(UninitializedPoolId);
			continue;
		}
		SpawnParams params;
		params.position = readPosition(rAdded.at("position"));
		params.what = "(level file edit)";
		PoolId id = spawnPrefab(prefab->second, shaderId_, params);
		if(keepIndices_ || id != UninitializedPoolId) rLevel_.entityIds.push_back(id);
		if(id == UninitializedPoolId) continue;

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
		if(rAdded.contains("externals")) readExternals(rAdded.at("externals"), getEntity(id), name_);
	}
}

/*
Applies one set of edits (the "edits" of the level's file, see saveLevelEdits, or its "source", the same format without "prefabs") to the level just made: the moved ones are put at their position, the added ones are made from their prefab and
owned by the level. An edit that does not fit (an index past the entities, an unknown prefab) is reported on stderr and skipped.
*/
void applyEditSet(Level& rLevel_, const Json& rEdits, const std::string& name_, PoolId shaderId_, bool asSource_)
{
	/* The source set (Save Source Game Level) makes its added entities first, so its per-entity edits can name them (their index follows the level's own entities); the edits set names only entities that exist before it. */
	if(asSource_) addEditedEntities(rLevel_, rEdits, name_, shaderId_, true);

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
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
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
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
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
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
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
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
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
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
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
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit turns or scales entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			Entity& rEntity = getEntity(rLevel_.entityIds[index]);
			rEntity.setRotation(readRotation(rEntry.at("rotation")));
			rEntity.setScale(readPosition(rEntry.at("scale")));
		}
	}
	/* "externals": [{"index", "values"}], the external field values of one of the level's own entities (writeExternals); an added entity keeps its own under "added". Entity references are resolved at the end, once the added entities exist. */
	if(rEdits.contains("externals"))
	{
		for(const Json& rEntry : rEdits.at("externals"))
		{
			uint32_t index = rEntry.at("index").get<uint32_t>();
			if(index >= rLevel_.entityIds.size() || !entityExists(rLevel_.entityIds[index]))
			{
				fprintf(stderr, "Level '%s': an edit sets fields of entity %u, which is not one of its own.\n", name_.c_str(), index);
				continue;
			}
			readExternals(rEntry.at("values"), getEntity(rLevel_.entityIds[index]), name_);
		}
	}
	if(!asSource_) addEditedEntities(rLevel_, rEdits, name_, shaderId_, false);
	rLevel_.resolveExternalEntities();
}

/*
Applies the "source" (if any) and then the "edits" (if any) of the level's file to the level just made. The source is the level as saved by File > Save Source Game Level: applied first, then the level's entities are marked as the source (markSourceEntities), so Restore Source Game Level and the edits' differences are from it.
The file is read again (not the recipe's copy): a saved edit is in the file the next time the level is loaded.
*/
/*
"cameras" (top level of the level file, written by File > Save and Save Source Game Level): [{"name", "position", "front", "yaw", "pitch", "fov", "near", "far", "enabled", "main", "renderTarget": {"width", "height"}}], the camera entities the editor added to the level. Each is made after
the edits as a camera entity (addCamera) that the level owns, so it is removed with the level and by Restore Source Game Level. "main" makes it the main camera; "renderTarget" makes a new render target of that size for it (absent: the screen). A broken entry is reported on stderr and skipped.
*/
void addLevelCameras(Level& rLevel_, const Json& rCameras_, const std::string& name_)
{
	for(const Json& rEntry : rCameras_)
	{
		try
		{
			glm::vec3 position = readPosition(rEntry.at("position"));
			PoolId id = addCamera(rEntry.value("name", std::string("Camera")).c_str(), position);
			rLevel_.entityIds.push_back(id);
			PoolId cameraId = getEntity(id).cameraId;
			Camera& rCamera = Globals::Game::cameras[cameraId];
			if(rEntry.contains("front")) rCamera.front = readPosition(rEntry.at("front"));
			rCamera.yaw = rEntry.value("yaw", rCamera.yaw);
			rCamera.pitch = rEntry.value("pitch", rCamera.pitch);
			rCamera.fov = rEntry.value("fov", rCamera.fov);
			rCamera.nearPlane = rEntry.value("near", rCamera.nearPlane);
			rCamera.farPlane = rEntry.value("far", rCamera.farPlane);
			rCamera.enabled = rEntry.value("enabled", rCamera.enabled);
			if(rEntry.contains("renderTarget"))
			{
				const Json& rTarget = rEntry.at("renderTarget");
				rCamera.setRenderTarget(addRenderTarget(rTarget.at("width").get<uint32_t>(), rTarget.at("height").get<uint32_t>()));
			}
			rCamera.refreshProjection();
			rCamera.set(position);
			if(rEntry.value("main", false)) Globals::Game::mainCamera = cameraId;
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "Level '%s': a camera of the file can't be made: %s\n", name_.c_str(), rError.what());
		}
	}
}

void applyEdits(Level& rLevel_, const std::string& name_, PoolId shaderId_)
{
	Json file;
	std::ifstream stream(levelFilePath("Assets/Levels/", name_));
	if(stream) file = Json::parse(stream);
	if(file.contains("source")) applyEditSet(rLevel_, file.at("source"), name_, shaderId_, true);
	rLevel_.markSourceEntities();
	if(file.contains("edits")) applyEditSet(rLevel_, file.at("edits"), name_, shaderId_, false);
	if(file.contains("cameras")) addLevelCameras(rLevel_, file.at("cameras"), name_);
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
		loadSourcePrefabs(name_);
		if(rRecipe.saLandTable != nullptr)
		{
			rLevel.setSAGPUInstance(rRecipe.shaderId, *rRecipe.saLandTable, *rRecipe.saTextures, rRecipe.skipped);
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
			rLevel.setONEGPUInstance(rRecipe.shaderId, *rRecipe.oneLevel, rRecipe.skipped);
			rLevel.setONECollision(*rRecipe.oneLevel);
			rLevel.setONEEntities(rRecipe.shaderId, *rRecipe.oneLevel);
		}
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

void loadSourcePrefabs(const std::string& levelName_)
{
	for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
	{
		if(!Globals::Game::prefabs.alive[i]) continue;
		PrefabEdits& rSource = Globals::Game::prefabs.objects[i].source;
		rSource = PrefabEdits{};
		const std::string& rName = Globals::Game::prefabs.names[i];
		std::string path = sourcePrefabFile("Assets/Levels/", levelName_, rName);
		std::ifstream stream(path);
		if(!stream) continue;
		try
		{
			readPrefabEdits(rSource, Json::parse(stream), "Source Game Level " + rName);
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "Source Game Level prefab not read (%s): %s\n", path.c_str(), rError.what());
		}
	}
}

void forEachSAEntityKey(const std::function<void(const SAEntityKey&)>& rFunction_)
{
	for(const auto& [tableName, pTable] : saEntityTables)
	{
		for(const SAPlacedEntityKey& rPlaced : *pTable) rFunction_(rPlaced.entity);
	}
	for(const SAEntityKey* pKey : saStageCreated) rFunction_(*pKey);
}

void forEachONEEntityKey(const std::function<void(const STH2005::EntityKey&)>& rFunction_)
{
	for(const auto& [tableName, pTable] : oneEntityTables)
	{
		for(const STH2005::PlacedEntityKey& rPlaced : *pTable) rFunction_(rPlaced.entity);
	}
}

namespace
{
/* A prefab's Changed file under a root ("Assets/External/": the game's copy; the source tree's External/: what the next build copies back), by its pool name ("SADX/SPRING"; a key name has no character a path can't have). */
std::string changedPrefabFile(const std::string& rRoot_, const std::string& rPoolName_)
{
	return rRoot_ + "Prefabs/" + rPoolName_ + "/Changed/prefab.json";
}
}

namespace
{
PrefabGroup readGroup(const std::string& rName_)
{
	for(PrefabGroup group : {PrefabGroup::Enemies, PrefabGroup::Npcs, PrefabGroup::Characters, PrefabGroup::StaticObjects, PrefabGroup::InteractibleObjects})
	{
		if(rName_ == prefabGroupFolder(group)) return group;
	}
	throw std::runtime_error(std::format("'{}' is not a prefab group.", rName_));
}

/* The group a key name goes in for one game of Groups.json: the first prefix that starts it, else the list that names it, else StaticObjects. */
PrefabGroup groupOf(const Json& rGame_, const std::string& rName_)
{
	for(const auto& [prefix, group] : rGame_.at("prefixes").items())
	{
		if(rName_.starts_with(prefix)) return readGroup(group.get<std::string>());
	}
	for(const auto& [group, names] : rGame_.at("names").items())
	{
		for(const Json& rName : names)
		{
			if(rName.get<std::string>() == rName_) return readGroup(group);
		}
	}
	return PrefabGroup::StaticObjects;
}

PlacementLayout readLayout(const std::string& rName_)
{
	if(rName_ == "Single") return PlacementLayout::Single;
	if(rName_ == "RingLine") return PlacementLayout::RingLine;
	if(rName_ == "Variant") return PlacementLayout::Variant;
	throw std::runtime_error(std::format("'{}' is not a placement layout.", rName_));
}
}

void loadPrefabData()
{
	auto& rPrefabs = Globals::Game::prefabs;
	try
	{
		std::ifstream stream("Assets/External/Prefabs/Groups.json");
		if(!stream) throw std::runtime_error("the file is missing");
		Json groups = Json::parse(stream);
		for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
		{
			if(!rPrefabs.alive[i]) continue;
			Prefab& rPrefab = rPrefabs.objects[i];
			if(rPrefab.kind != PrefabKind::SAObject && rPrefab.kind != PrefabKind::ONEObject) continue;
			rPrefab.group = groupOf(groups.at(prefabGameName(rPrefab.game)), rPrefab.name);
		}
	}
	catch(const std::exception& rError)
	{
		fprintf(stderr, "Prefab groups not read (Assets/External/Prefabs/Groups.json): %s\n", rError.what());
	}
	try
	{
		std::ifstream stream("Assets/External/Prefabs/STH2005/Placement.json");
		if(!stream) throw std::runtime_error("the file is missing");
		Json placement = Json::parse(stream);
		for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
		{
			if(!rPrefabs.alive[i] || rPrefabs.objects[i].kind != PrefabKind::ONEObject) continue;
			Prefab& rPrefab = rPrefabs.objects[i];
			auto found = placement.find(rPrefab.name);
			if(found == placement.end()) continue;
			rPrefab.layout = readLayout(found->value("layout", std::string("Single")));
			rPrefab.variants = found->value("variants", std::vector<std::vector<uint32_t>>{});
			rPrefab.loopMotion = found->value("loopMotion", false);
		}
	}
	catch(const std::exception& rError)
	{
		fprintf(stderr, "Prefab placement data not read (Assets/External/Prefabs/STH2005/Placement.json): %s\n", rError.what());
	}
	try
	{
		std::ifstream stream("Assets/External/Prefabs/SADX/Placement.json");
		if(!stream) throw std::runtime_error("the file is missing");
		Json placement = Json::parse(stream);
		for(uint32_t i = 0; i < rPrefabs.objects.size(); i++)
		{
			if(!rPrefabs.alive[i] || rPrefabs.objects[i].kind != PrefabKind::SAObject) continue;
			Prefab& rPrefab = rPrefabs.objects[i];
			auto found = placement.find(rPrefab.saKey->name);
			if(found == placement.end()) continue;
			for(const Json& rStep : found->value("rotation", Json::array()))
			{
				std::string axis = rStep.at("axis").get<std::string>();
				if(axis.size() != 1 || axis.find_first_of("XYZ") != 0) throw std::runtime_error(std::format("{}: '{}' is not an axis.", rPrefab.name, axis));
				rPrefab.rotation.push_back(SARotationStep{axis[0], rStep.at("field").get<uint8_t>(), rStep.at("addend").get<int32_t>(), rStep.at("negate").get<bool>()});
			}
			rPrefab.variantField = found->value("variantField", 0);
		}
	}
	catch(const std::exception& rError)
	{
		fprintf(stderr, "SADX prefab placement data not read (Assets/External/Prefabs/SADX/Placement.json): %s\n", rError.what());
	}
}

void loadChangedPrefabs()
{
	for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
	{
		if(!Globals::Game::prefabs.alive[i] || Globals::Game::prefabs.objects[i].kind == PrefabKind::External) continue;
		const std::string& rName = Globals::Game::prefabs.names[i];
		std::string path = changedPrefabFile("Assets/External/", rName);
		std::ifstream stream(path);
		if(!stream) continue;
		try
		{
			readPrefabEdits(Globals::Game::prefabs.objects[i].changed, Json::parse(stream), "Changed " + rName);
		}
		catch(const std::exception& rError)
		{
			fprintf(stderr, "Changed prefab not read (%s): %s\n", path.c_str(), rError.what());
		}
	}
}

#ifdef EDITOR
void saveChangedPrefab(PoolId prefabId_)
{
	namespace fs = std::filesystem;
	const Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	const std::string& rName = Globals::Game::prefabs.names[prefabId_.id];
	bool empty = prefabEditsEmpty(rPrefab.changed, true);
	std::string text = empty ? std::string() : writePrefabEdits(rPrefab.changed, true).dump(2);
	for(const std::string& rRoot : {std::string("Assets/External/"), std::string(SF_SOURCE_DIR "/External/")})
	{
		fs::path path = changedPrefabFile(rRoot, rName);
		if(empty)
		{
			fs::remove(path);
			continue;
		}
		fs::create_directories(path.parent_path());
		std::ofstream out(path);
		if(!out) throw std::runtime_error(std::format("can't write {}.", path.string()));
		out << text;
	}
}

/* The "edits" object of the level as it is now, from its source (the game's data plus the level file's "source"): the differences only; an empty object when there are none. */
static nlohmann::ordered_json buildLevelEdits(const Level& rLevel)
{
	auto position = [](const glm::vec3& rPosition_) { return nlohmann::ordered_json::array({rPosition_.x, rPosition_.y, rPosition_.z}); };
	auto rotation = [](const glm::mat4& rRotation_)
	{
		glm::mat3 matrix(rRotation_);
		return nlohmann::ordered_json::array({matrix[0].x, matrix[0].y, matrix[0].z, matrix[1].x, matrix[1].y, matrix[1].z, matrix[2].x, matrix[2].y, matrix[2].z});
	};
	nlohmann::ordered_json moved = nlohmann::ordered_json::array();
	nlohmann::ordered_json transforms = nlohmann::ordered_json::array();
	nlohmann::ordered_json added = nlohmann::ordered_json::array();
	nlohmann::ordered_json externals = nlohmann::ordered_json::array();
	for(uint32_t i = 0; i < rLevel.entityIds.size(); i++)
	{
		if(!entityExists(rLevel.entityIds[i])) continue;
		const Entity& rEntity = getEntity(rLevel.entityIds[i]);
		if(i < rLevel.sourceEntityCount)
		{
			if(rEntity.position != rLevel.sourcePositions[i]) moved.push_back({{"index", i}, {"position", position(rEntity.position)}, {"source", position(rLevel.sourcePositions[i])}});
			if(rEntity.rotation != rLevel.sourceRotations[i] || rEntity.scale != rLevel.sourceScales[i]) transforms.push_back({{"index", i}, {"rotation", rotation(rEntity.rotation)}, {"scale", position(rEntity.scale)}});
			nlohmann::ordered_json values = writeExternals(rEntity, rLevel);
			if(!values.empty()) externals.push_back({{"index", i}, {"values", values}});
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
			if(rEntity.meshChoice != prefabMesh(Globals::Game::prefabs[rEntity.prefabId])) added.back()["mesh"] = writeMeshChoice(rEntity.meshChoice);
			if(rEntity.textureChoice != prefabTexture(Globals::Game::prefabs[rEntity.prefabId])) added.back()["texture"] = writeTextureChoice(rEntity.textureChoice);
			nlohmann::ordered_json values = writeExternals(rEntity, rLevel);
			if(!values.empty()) added.back()["externals"] = values;
		}
	}

	/* What each of the level's own entities has more or less than the game's data plus the prefab edits give it (the same order of application as applyEdits), by name; an added entity keeps its whole list. The bound area is kept whole when it differs. */
	auto& rTypes = Globals::Game::entityBehaviorTypes;
	nlohmann::ordered_json prefabs = nlohmann::ordered_json::object();
	for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
	{
		if(!Globals::Game::prefabs.alive[i]) continue;
		const Prefab& rPrefab = Globals::Game::prefabs.objects[i];
		bool withModel = rPrefab.kind != PrefabKind::External;
		if(prefabEditsEmpty(rPrefab.level, withModel)) continue;
		nlohmann::ordered_json lists = writePrefabEdits(rPrefab.level, withModel);
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
			applyPrefabBehaviorsTo(Globals::Game::prefabs[rEntity.prefabId], expected);
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

		MeshChoice expectedMesh = rEntity.prefabId != UninitializedPoolId ? prefabMesh(Globals::Game::prefabs[rEntity.prefabId]) : MeshChoice{};
		if(rEntity.meshChoice != expectedMesh) meshes.push_back({{"index", i}, {"mesh", writeMeshChoice(rEntity.meshChoice)}});
		TextureChoice expectedTexture = rEntity.prefabId != UninitializedPoolId ? prefabTexture(Globals::Game::prefabs[rEntity.prefabId]) : TextureChoice{};
		if(rEntity.textureChoice != expectedTexture) textures.push_back({{"index", i}, {"texture", writeTextureChoice(rEntity.textureChoice)}});
	}
	nlohmann::ordered_json edits = nlohmann::ordered_json::object();
	if(!moved.empty() || !added.empty() || !prefabs.empty() || !behaviors.empty() || !shapes.empty() || !textures.empty() || !meshes.empty() || !transforms.empty() || !externals.empty())
	{
		edits = {{"moved", moved}, {"added", added}};
		if(!prefabs.empty()) edits["prefabs"] = prefabs;
		if(!behaviors.empty()) edits["behaviors"] = behaviors;
		if(!shapes.empty()) edits["shapes"] = shapes;
		if(!transforms.empty()) edits["transforms"] = transforms;
		if(!externals.empty()) edits["externals"] = externals;
		if(!meshes.empty()) edits["meshes"] = meshes;
		if(!textures.empty()) edits["textures"] = textures;
	}
	return edits;
}

/* The "cameras" list of the level as it is now: its camera entities after the source ones (the editor's Add camera and paste put them in Level::entityIds). The inverse of addLevelCameras; empty when there are none. */
static nlohmann::ordered_json buildLevelCameras(const Level& rLevel)
{
	nlohmann::ordered_json cameras = nlohmann::ordered_json::array();
	auto vector = [](const glm::vec3& rValue_) { return nlohmann::ordered_json::array({rValue_.x, rValue_.y, rValue_.z}); };
	for(uint32_t i = rLevel.sourceEntityCount; i < rLevel.entityIds.size(); i++)
	{
		if(!entityExists(rLevel.entityIds[i])) continue;
		const Entity& rEntity = getEntity(rLevel.entityIds[i]);
		if(rEntity.cameraId == UninitializedPoolId) continue;
		const Camera& rCamera = Globals::Game::cameras[rEntity.cameraId];
		nlohmann::ordered_json entry = {{"name", Globals::Game::entities.names[rLevel.entityIds[i].id]}, {"position", vector(rCamera.position)}, {"front", vector(rCamera.front)}, {"yaw", rCamera.yaw}, {"pitch", rCamera.pitch},
			{"fov", rCamera.fov}, {"near", rCamera.nearPlane}, {"far", rCamera.farPlane}, {"enabled", rCamera.enabled}};
		if(Globals::Game::mainCamera == rEntity.cameraId) entry["main"] = true;
		if(const Graphics::RenderTarget* pTarget = rCamera.renderTarget()) entry["renderTarget"] = {{"width", pTarget->width}, {"height", pTarget->height}};
		cameras.push_back(entry);
	}
	return cameras;
}

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
	nlohmann::ordered_json edits = buildLevelEdits(rLevel);
	if(!edits.empty()) file["edits"] = edits;
	file.erase("cameras");
	nlohmann::ordered_json cameras = buildLevelCameras(rLevel);
	if(!cameras.empty()) file["cameras"] = cameras;

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
/*
The file's "source" with the level's "edits" (buildLevelEdits: differences from that source) applied on top, as one "source" the next load gives the same level from. A per-entity entry of the edits replaces the source's entry of the same index, except "behaviors" (their added and
removed names are composed: removing what the source added only cancels it) and "moved" (the "source" position stays the source's); "added" entries follow the source's, which is the order of the entity indices. The edits' "prefabs" are not here (saveSourceLevel merges them into Prefab::source).
*/
nlohmann::ordered_json composeSource(const nlohmann::ordered_json& rSource_, const nlohmann::ordered_json& rEdits_)
{
	using OJson = nlohmann::ordered_json;
	OJson result = rSource_.is_object() ? rSource_ : OJson::object();
	auto entryOf = [](OJson& rList_, uint32_t index_) -> OJson*
	{
		for(OJson& rEntry : rList_)
		{
			if(rEntry.at("index").get<uint32_t>() == index_) return &rEntry;
		}
		return nullptr;
	};
	for(const char* pKey : {"moved", "transforms", "shapes", "meshes", "textures", "externals", "behaviors"})
	{
		if(!rEdits_.contains(pKey)) continue;
		if(!result.contains(pKey)) result[pKey] = OJson::array();
		OJson& rList = result[pKey];
		for(const OJson& rEntry : rEdits_.at(pKey))
		{
			OJson* pOld = entryOf(rList, rEntry.at("index").get<uint32_t>());
			if(pOld == nullptr)
			{
				rList.push_back(rEntry);
				continue;
			}
			std::string key = pKey;
			if(key == "moved") (*pOld)["position"] = rEntry.at("position");
			else if(key == "behaviors")
			{
				OJson& rAdded = (*pOld)["added"];
				OJson& rRemoved = (*pOld)["removed"];
				for(const OJson& rName : rEntry.value("removed", OJson::array()))
				{
					auto found = std::find(rAdded.begin(), rAdded.end(), rName);
					if(found != rAdded.end()) rAdded.erase(found);
					else if(std::find(rRemoved.begin(), rRemoved.end(), rName) == rRemoved.end()) rRemoved.push_back(rName);
				}
				for(const OJson& rName : rEntry.value("added", OJson::array()))
				{
					auto found = std::find(rRemoved.begin(), rRemoved.end(), rName);
					if(found != rRemoved.end()) rRemoved.erase(found);
					else if(std::find(rAdded.begin(), rAdded.end(), rName) == rAdded.end()) rAdded.push_back(rName);
				}
			}
			else *pOld = rEntry;
		}
	}
	if(rEdits_.contains("added") && !rEdits_.at("added").empty())
	{
		if(!result.contains("added")) result["added"] = OJson::array();
		for(const OJson& rEntry : rEdits_.at("added")) result["added"].push_back(rEntry);
	}
	/* An empty "behaviors" entry (everything cancelled) is dropped. */
	if(result.contains("behaviors"))
	{
		OJson kept = OJson::array();
		for(const OJson& rEntry : result.at("behaviors"))
		{
			if(!rEntry.value("added", OJson::array()).empty() || !rEntry.value("removed", OJson::array()).empty()) kept.push_back(rEntry);
		}
		result["behaviors"] = kept;
	}
	return result;
}
}

void saveSourceLevel(PoolId levelId_)
{
	namespace fs = std::filesystem;
	const Level& rLevel = getLevel(levelId_);
	std::string levelName = Globals::Game::levels.names[levelId_.id];
	std::ifstream stream(levelFilePath("Assets/Levels/", levelName));
	if(!stream) throw std::runtime_error(std::format("{} not found.", levelFilePath("Assets/Levels/", levelName)));
	nlohmann::ordered_json file = nlohmann::ordered_json::parse(stream);
	stream.close();

	/* The level file: its source becomes what the level is now, so there are no edits left. */
	nlohmann::ordered_json source = composeSource(file.value("source", nlohmann::ordered_json::object()), buildLevelEdits(rLevel));
	source.erase("prefabs");
	file.erase("edits");
	file.erase("source");
	if(!source.empty()) file["source"] = source;
	file.erase("cameras");
	nlohmann::ordered_json cameras = buildLevelCameras(rLevel);
	if(!cameras.empty()) file["cameras"] = cameras;
	std::string text = file.dump(2);

	/* The prefabs' edits go to the level's SourceGamePrefabs folder, one file per prefab (removed when the prefab has none). */
	for(const std::string& rRoot : {std::string("Assets/Levels/"), std::string(SF_SOURCE_DIR "/Levels/")})
	{
		for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
		{
			if(!Globals::Game::prefabs.alive[i]) continue;
			const Prefab& rPrefab = Globals::Game::prefabs.objects[i];
			bool withModel = rPrefab.kind != PrefabKind::External;
			PrefabEdits merged = mergePrefabEdits(rPrefab.source, rPrefab.level);
			fs::path path = sourcePrefabFile(rRoot, levelName, Globals::Game::prefabs.names[i]);
			if(prefabEditsEmpty(merged, withModel))
			{
				fs::remove(path);
				continue;
			}
			fs::create_directories(path.parent_path());
			std::ofstream out(path);
			if(!out) throw std::runtime_error(std::format("can't write {}.", path.string()));
			out << writePrefabEdits(merged, withModel).dump(2);
		}
		std::ofstream out(levelFilePath(rRoot, levelName));
		if(!out) throw std::runtime_error(std::format("can't write {}.", levelFilePath(rRoot, levelName)));
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
