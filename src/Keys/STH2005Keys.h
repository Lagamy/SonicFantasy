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
How many objects of an EntityKey one placement record stands for, and where they are (see RingLine.h): Single is one at the record's position, RingLine
is the layout of the ring object (a record is a count of rings along a line, or the other shapes of the game's ring code), Variant is for objects whose record
names one of the BIN's variants: the u32 at the start of its parameters is an index into EntityKey::variants (observed on the stage art objects; hypothesis that it is
the index of the .BIN's info), and the record makes one entity per model of that variant.
*/
enum class PlacementLayout
{
	Single,
	RingLine,
	Variant
};

/*
A kind of object a stage places (the registry of main.dol, see readPlacements): everything an entity of it is made from, grouped from the keys above. All of
them are in the stage's `*_gdt.one` archive (or stg_cmn_gdt.one for the objects common to every stage), which is also where the textures are.
name is the registry's name for the type (e.g. "SPRING"), for reading; models are the object's models, in the order of its .BIN (the first is the one an entity is created
with, the others are states it can switch to, see Entity::changeActiveMesh); textures.entry is nullptr for an object without a .TXD; motions and collisions are
the motions and the .CCL shapes the .BIN names; layout says how many entities one placement record makes; variants (for the Variant layout) lists, per variant, the indices into models of the models it is made of, and the collision with the variant's index (if any) goes to the first of them; loopMotion makes the motion Level starts on the entity loop (set for living things, enemies and characters, whose idle goes on) instead of playing once. Which of them an entity uses at a time is not decided here (no behaviour yet).
*/
struct EntityKey
{
	const char* name;
	std::vector<ModelKey> models;
	TextureKey textures;
	std::vector<MotionKey> motions = {};
	std::vector<CollisionKey> collisions = {};
	PlacementLayout layout = PlacementLayout::Single;
	std::vector<std::vector<uint32_t>> variants = {};
	bool loopMotion = false;
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
skippedEntries are parts of entry names: an .RG1 whose name contains one is not imported (e.g. "_D_" for the worlds of a
kind that is not wanted).
placementFiles are the disc paths of the stage's placement files (stgNNNN_cmn.dat, ...) and entities what each object type id in them is made of: a
placement whose type is not listed is not created.
*/
struct LevelKey
{
	std::vector<ArchiveKey> sections;
	TextureKey textures;
	std::vector<std::string> skippedEntries = {};
	std::vector<const char*> placementFiles = {};
	std::vector<PlacedEntityKey> entities = {};
	const std::map<uint16_t, const char*>* typeNames = nullptr;
};
}
