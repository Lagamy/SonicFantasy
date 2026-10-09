#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace STH2005
{
/* A .ONE archive on the Shadow disc, by its path (e.g. "character/shadow.one"). Names an STH2005::ONE resource. */
struct ArchiveKey
{
	const char* path;
};

/*
One .DFF of a Shadow the Hedgehog model and the .BON that names its bones.
Parts without a .BON (bon = nullptr, e.g. SHADOW_EYE_L.DFF) name instead the main model's bone they attach to.
The strings must outlive the model (use literals or the constants of ResourceKeys.h): meshes keep their recipe.
Also the key of a part: ONEInstance::switchMeshPart names the part to replace and the part to put in its place.
*/
struct ModelPart
{
	const char* dff;
	const char* bon;
	const char* bone;
};

/* Two parts are the same when their dff, bon and bone are (a nullptr only equals a nullptr). */
inline bool operator==(const ModelPart& rA_, const ModelPart& rB_)
{
	auto same = [](const char* pA_, const char* pB_)
	{
		return pA_ == nullptr || pB_ == nullptr ? pA_ == pB_ : std::strcmp(pA_, pB_) == 0;
	};
	return same(rA_.dff, rB_.dff) && same(rA_.bon, rB_.bon) && same(rA_.bone, rB_.bone);
}

/*
A model of an archive: its name inside the archive and the recipe (see importModel) to build it from,
used the first time an instance needs it. A name always means one recipe: a second key with the same name and archive
reuses the mesh already built (and throws if its parts differ).
*/
struct ModelKey
{
	ArchiveKey archive;
	const char* name;
	std::vector<ModelPart> parts;
};

/*
A motion of an archive: the .MTP entry it is in, its name there (e.g. "sh_idle"; an entry ending in .MTN is a single motion with no table, `motion` then only names it) and the .BON entry of the skeleton it
animates (e.g. "SH.BON": the motion stores no bone names, they come from the .BON). Decoded the first time an instance
plays it.
attachedPart is true for the motion of a model part's skeleton (e.g. "LTE.BON" of a hand): that skeleton's first node is
the main skeleton's bone the part is attached to (see importModel), which the main motion drives, so it is
not driven by this one.
*/
struct MotionKey
{
	ArchiveKey archive;
	const char* mtp;
	const char* motion;
	const char* bon;
	bool attachedPart = false;
};

/* A .CCL entry (object collision shapes, see readCCL) of an archive, read the first time an entity needs it. */
struct CollisionKey
{
	ArchiveKey archive;
	const char* entry;
};

/* A .TXD entry of an archive, decoded the first time an instance needs it. */
struct TextureKey
{
	ArchiveKey archive;
	const char* entry;
};

/*
A kind of object a stage places (the registry of main.dol, see readPlacements): everything an entity of it is made from, grouped from the keys above. All of
them are in the stage's `*_gdt.one` archive (or stg_cmn_gdt.one for the objects common to every stage), which is also where the textures are.
name is the registry's name for the type (e.g. "SPRING"), for reading; models are the object's models, in the order of its .BIN (the first is the one an entity is created
with, the others are states it can switch to, see Entity::changeActiveMesh); textures.entry is nullptr for an object without a .TXD; motions and collisions are
the motions and the .CCL shapes the .BIN names. How a placement record is made into entities (PlacementLayout, the .BIN's variants) and whether the motion loops are not here:
they are the prefab's (Prefab::layout, variants, loopMotion), read from External/Prefabs/STH2005/Placement.json. Which of the models an entity uses at a time is not decided here (no behaviour yet).
*/
struct EntityKey
{
	const char* name;
	std::vector<ModelKey> models;
	TextureKey textures;
	std::vector<MotionKey> motions = {};
	std::vector<CollisionKey> collisions = {};
};

/*
An object type id of a placement file (Placement::type) and what it is made of, in the level that places it. With parameter >= 0 it only applies to the records whose first parameter u32
equals it (several entries of one type, e.g. the characters of SET_CHARACTOR); -1 is any record of the type.
*/
struct PlacedEntityKey
{
	uint16_t type;
	EntityKey entity;
	int32_t parameter = -1;
};

/*
A stage's level: the section archives that hold its worlds (every .RG1 entry of each is imported, the .BSP collision
entries are not) and the .TXD entry its materials' textures are looked up in, which is in another archive (the stage's
texture archive). Imported the first time a level needs it.
placementFiles are the disc paths of the stage's placement files (stgNNNN_cmn.dat, ...) and entities what each object type id in them is made of: a
placement whose type is not listed is not created.
*/
struct LevelKey
{
	std::vector<ArchiveKey> sections;
	TextureKey textures;
	std::vector<const char*> placementFiles = {};
	std::vector<PlacedEntityKey> entities = {};
	const std::map<uint16_t, const char*>* typeNames = nullptr;
};
}
