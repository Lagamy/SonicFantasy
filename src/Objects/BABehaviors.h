#pragma once
#include "BAKeys.h"
#include "BABehavior.h"
#include "Constants.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Physics
{
struct Collision;
}

/*
A remap: which behaviour each raw value of a game has (BABehaviors::resolve looks them up once when a collision is loaded, so the collision system never sees raw values). A raw value that is not listed has the fallback.
Four are used, one per kind of data: a level's triangle values and an object's shape attributes (Physics::CollisionShape::attr), for each game. They are read from External/Collision/Remaps.json (BABehaviors::loadRemaps).
*/
struct BARemap
{
	std::vector<std::pair<uint32_t, PoolId>> entries;
	PoolId fallback = UninitializedPoolId;
};

/*
The bound area rules the editor makes (External/Collision/BoundAreas.json, read after the scripts by BABehaviors::loadBoundAreas), kept by name so a function that is not registered (its script is gone) keeps its entry:
- tags: the tags made by the file (a tag is only a name).
- behaviors: name; tags (what it is); collidesWith (the tags it is stopped by, BABehavior::availableCollisions); onTouch: a list of {function, tags}: the named onTouch function (Globals::Game::boundAreaFunctions) and
  the tags whose overlap calls it (BABehavior::availableInteractions: each tag gets the function in its list).
A behaviour code made is kept as well (scriptBehaviors, never saved): the built-in ones (init) and the ones a script made with sf.collisionBehavior, whose functions get registered names ("<behaviour>/<tag>") so the editor can
show them. Customising one copies it into behaviors under the same name: the file's entry then replaces the code's rules at the next start, and reverting removes the copy. Only the behaviours in behaviors are edited, renamed or deleted.
*/
struct BoundAreaRule
{
	std::string function;
	std::vector<std::string> tags;
};

struct BoundAreaBehaviorData
{
	std::string name;
	std::vector<std::string> tags;
	std::vector<std::string> collidesWith;
	std::vector<BoundAreaRule> onTouch;
};

struct BoundAreaData
{
	std::vector<std::string> tags;
	std::vector<BoundAreaBehaviorData> behaviors;

	/* What code made, rebuilt every start and never saved: the built-in behaviours and the scripts' ones, and the tags that existed before the file was read (a script's sf.tag or one in the file that a script made already): not the file's to rename or delete. */
	std::vector<BoundAreaBehaviorData> scriptBehaviors;
	std::vector<std::string> fixedTags;
};

/*
Our own convention for collision behaviours (the engine's, not either game's): this struct describes and fills Globals::Game::baTags and Globals::Game::behaviors (init: the built-in tags and behaviours of
BAKeys.h and what they collide with) and gives a collision its behaviours from a remap. The built-in ids are kept here so code can use them without looking a name up; a script or a mod adds a tag with addTag
and a behaviour with add, and finds them with findTag and find.
*/
struct BABehaviors
{
	PoolId tagSolid;
	PoolId tagPickup;
	PoolId tagHazard;
	PoolId tagDeath;
	PoolId tagBody;

	PoolId none;
	PoolId solid;
	PoolId pickup;
	PoolId hazard;
	PoolId death;
	PoolId passable;
	PoolId body;
	PoolId object;

	BARemap remapShadowLevel;
	BARemap remapSADXLevel;
	BARemap remapSADXShape;
	BARemap remapShadowShape;

	/* Adds the built-in tags and behaviours (and lists the behaviours in boundAreas.scriptBehaviors, so the editor can customise them); once, before any collision is loaded (resolve needs them). */
	void init();

	/*
	Reads the four remaps from the file (Assets/External/Collision/Remaps.json: ShadowLevel, SADXLevel, SADXShape, ShadowShape, each {"fallback": behaviour name, "values": {"<raw, hex or decimal string>": behaviour name}}; "note" is ignored).
	Once, after init, before any collision is loaded. Throws std::runtime_error if the file is missing or wrong, a behaviour
	name is unknown or a raw value is listed twice.
	*/
	void loadRemaps(const std::string& path_);

	/* The editor-made tags and behaviours (BoundAreaData); empty if there is no file. */
	BoundAreaData boundAreas;

	/*
	Reads External/Collision/BoundAreas.json ({"tags": [name], "behaviors": [{"name", "tags": [name], "collidesWith": [name], "onTouch": [{"function": name, "tags": [name]}]}]}): makes the tags and the behaviours.
	Once, after the scripts are loaded (the function names are looked up now); a missing file is nothing to read. Throws std::runtime_error if the file is wrong, a tag name is unknown or a behaviour name is taken.
	A function that is not registered is left out of the behaviour (printed on stderr), its entry stays in boundAreas.
	*/
	void loadBoundAreas(const std::string& path_);

	/* Builds boundAreas.behaviors[index_] into Globals::Game::behaviors: made if there is none of that name, else replaced in place (every shape that has it changes). Throws if a tag name is unknown. */
	PoolId applyBoundArea(size_t index_);
	PoolId applyBoundAreaData(const BoundAreaBehaviorData& rData_);

	/*
	The edits that change what exists. All throw std::runtime_error, changing nothing, when they can't be done:
	- renameBoundAreaTag / removeBoundAreaTag: only a tag of boundAreas.tags that is not fixed; a tag a behaviour (any, built-in too) still uses is not removed.
	- renameBoundAreaBehavior / removeBoundAreaBehavior: boundAreas.behaviors[index_] that is not built in or made by a script (a customised one is reverted instead); a behaviour a loaded shape has is not removed. A level file that
	  saved a shape with the old name names an unknown behaviour at its next load (printed on stderr).
	- customizeScriptBehavior: copies boundAreas.scriptBehaviors[index_] (a built-in or a script's behaviour) into behaviors (same name, same rules) and returns its index there.
	- revertBoundAreaBehavior: removes the copy boundAreas.behaviors[index_] made of a built-in or script behaviour and gives the behaviour its original rules again.
	*/
	void renameBoundAreaTag(const std::string& rName_, const std::string& rNewName_);
	void removeBoundAreaTag(const std::string& rName_);
	void renameBoundAreaBehavior(size_t index_, const std::string& rNewName_);
	void removeBoundAreaBehavior(size_t index_);
	size_t customizeScriptBehavior(size_t index_);
	void revertBoundAreaBehavior(size_t index_);

	/* The original rules of a behaviour name, nullptr if it is neither built in nor made by a script (init and sf.collisionBehavior keep them in boundAreas.scriptBehaviors). */
	const BoundAreaBehaviorData* scriptBehaviorNamed(const std::string& rName_) const;

	/* Adds a tag / a behaviour (empty rules) to boundAreas and the pools; throws if the name is empty or taken. Returns the index of the behaviour. */
	PoolId addBoundAreaTag(const std::string& rName_);
	size_t addBoundAreaBehavior(const std::string& rName_);

#ifdef EDITOR
	/* Writes boundAreas to Assets/External/Collision/BoundAreas.json and the source tree's External/Collision/BoundAreas.json. Throws if a file can't be written. */
	void saveBoundAreas() const;
#endif

	/* Adds a tag or a behaviour under its key's name; throws if that name is taken in that pool. */
	PoolId addTag(const BATagKey& rKey_);
	PoolId add(const BABehaviorKey& rKey_, BABehavior behavior_);

	/* The id of the tag / behaviour named name_, UninitializedPoolId if there is none. idOfTag and idOf are the same for a key and throw if there is none. */
	PoolId findTag(const char* name_) const;
	PoolId find(const char* name_) const;
	PoolId idOfTag(const BATagKey& rKey_) const;
	PoolId idOf(const BABehaviorKey& rKey_) const;

	/*
	Gives every distinct raw triangle value of the collision's mesh (its own attribute, or its piece's flags, see CollisionMesh::triangleValue) its behaviour in Collision::behaviorByValue, sets
	Collision::fallbackBehavior and the behaviour of every shape from its attr, all from rRemap_. Called when a collision is loaded.
	*/
	void resolve(Physics::Collision& rCollision_, const BARemap& rRemap_) const;
};
