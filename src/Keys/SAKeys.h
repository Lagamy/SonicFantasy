#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

/*
A table of collision shapes (SADX's 0x30-byte collision entries, see Physics::CollisionShape) in a GameCube .rel module: the module's
path, the file offset of the first entry in the decompressed module (for a Ghidra address of sadxDecomp: the section's file_offset +
address - the section's address, in section-layout.json) and the number of entries.
*/
struct SACollisionShapesKey
{
	const char* rel;
	uint32_t offset;
	uint32_t count;
};

/*
A Ninja Basic model of a GameCube .rel module on the SADX disc: the module's path and the file offset of the model's
root NJS_OBJECT (see importSAModel). Always big-endian. Imported the first time an instance needs it.
*/
struct SAModelKey
{
	const char* rel;
	uint32_t rootOffset;
};

/* A .gvm texture archive on the SADX disc, by its path. Imported the first time an instance needs it. */
struct SATextureSetKey
{
	const char* path;
};

/*
A Ninja motion (NJS_MOTION) of a GameCube .rel module on the SADX disc: the module's path, the motion's file offset and
the number of model nodes it animates (the file doesn't store it; SA Tools lists it). Imported the first time an
instance plays it; nodeCount must match the instance's model.
*/
struct SAMotionKey
{
	const char* rel;
	uint32_t offset;
	uint32_t nodeCount;
};

/*
One rotation of an object's display code (see SAEntityKey::rotation): about axis ('X', 'Y' or 'Z') by the task's angle `field` (0 x, 1 y, 2 z, in BAMS), negated if negate, plus addend. A step with
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
A kind of object an SADX stage places (SASet.h): the model it is drawn with, in a .rel, and its textures (the .gvm the texlist of the model names; path nullptr = untextured).
name is the object list's name for the type (e.g. "SPRING"), for reading; motions and collisions are the ones the generator found. rotation is the order and angles the object's display code turns by when it is not the usual z, x, y of the three angles (empty = the usual; found by the generator from the object's code). variantModels, if there are any, replace model: a component of the record's scale (variantField: 0 x, 1 y, 2 z) as a byte, modulo their count, picks one, and one with a null rel is not drawn (O PARASOL by x, O PIER by y; see tools/genSAEntityKeys.py). variantCollisions, if there are any, replace collisions the same way, with their own count (the object's code picks a collision table by the same value).
Which model an object uses is not stored as data: tools/genSAEntityKeys.py finds it from the relocations of the object's code (hypothesis).
*/
struct SAEntityKey
{
	const char* name;
	SAModelKey model;
	SATextureSetKey textures;
	std::vector<SAMotionKey> motions = {};
	std::vector<SACollisionShapesKey> collisions = {};
	std::vector<SARotationStep> rotation = {};
	std::vector<SAModelKey> variantModels = {};
	std::vector<std::vector<SACollisionShapesKey>> variantCollisions = {};
	uint8_t variantField = 0;
};

/* An object type (SAPlacement::type) and what it is made of, in the stage that places it. */
struct SAPlacedEntityKey
{
	uint16_t type;
	SAEntityKey entity;
};

/*
An object the stage's own code creates instead of a SET record (e.g. Emerald Coast's floating bridge): what it is made of and where, in the land table's space. The orientation is none.
*/
struct SAStageEntityKey
{
	const SAEntityKey* entity;
	float position[3];
};

/*
A land table (the level's geometry pieces and texlist) of a GameCube .rel module on the SADX disc: the module's path and
the file offset of the land table (see readSALandTable). Always big-endian. Imported the first time a level needs it.
skippedTextures are texlist names: a piece whose submeshes all use one of them is not imported (e.g. baked shadows).
placementFiles are the disc paths of the stage's SET files (SetNNNNx.bin) and entities what each object type in them is made of: a placement whose type is not listed gets a marker
(CollisionView::markers) labeled with its name from typeNames (type -> name, may be nullptr). stageEntities are objects the stage's code creates, placed as they are (Level::setSAEntities).
*/
struct SALandTableKey
{
	const char* rel;
	uint32_t offset;
	std::vector<std::string> skippedTextures = {};
	std::vector<const char*> placementFiles = {};
	std::vector<SAPlacedEntityKey> entities = {};
	const std::map<uint16_t, const char*>* typeNames = nullptr;
	std::vector<SAStageEntityKey> stageEntities = {};
};
