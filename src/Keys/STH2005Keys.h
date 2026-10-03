#pragma once
#include <cstring>
#include <vector>

/* A .ONE archive on the Shadow disc, by its path (e.g. "character/shadow.one"). Names an STH2005ONE resource. */
struct STH2005ArchiveKey
{
	const char* path;
};

/*
One .DFF of a Shadow the Hedgehog model and the .BON that names its bones.
Parts without a .BON (bon = nullptr, e.g. SHADOW_EYE_L.DFF) name instead the main model's bone they attach to.
The strings must outlive the model (use literals or the constants of ResourceKeys.h): meshes keep their recipe.
Also the key of a part: ONEInstance::switchMeshPart names the part to replace and the part to put in its place.
*/
struct STH2005ModelPart
{
	const char* dff;
	const char* bon;
	const char* bone;
};

/* Two parts are the same when their dff, bon and bone are (a nullptr only equals a nullptr). */
inline bool operator==(const STH2005ModelPart& rA_, const STH2005ModelPart& rB_)
{
	auto same = [](const char* pA_, const char* pB_)
	{
		return pA_ == nullptr || pB_ == nullptr ? pA_ == pB_ : std::strcmp(pA_, pB_) == 0;
	};
	return same(rA_.dff, rB_.dff) && same(rA_.bon, rB_.bon) && same(rA_.bone, rB_.bone);
}

/*
A model of an archive: its name inside the archive and the recipe (see importSTH2005Model) to build it from,
used the first time an instance needs it. A name always means one recipe: a second key with the same name and archive
reuses the mesh already built (and throws if its parts differ).
*/
struct STH2005ModelKey
{
	STH2005ArchiveKey archive;
	const char* name;
	std::vector<STH2005ModelPart> parts;
};

/*
A motion of an archive: the .MTP entry it is in, its name there (e.g. "sh_idle") and the .BON entry of the skeleton it
animates (e.g. "SH.BON": the motion stores no bone names, they come from the .BON). Decoded the first time an instance
plays it.
attachedPart is true for the motion of a model part's skeleton (e.g. "LTE.BON" of a hand): that skeleton's first node is
the main skeleton's bone the part is attached to (see importSTH2005Model), which the main motion drives, so it is
not driven by this one.
*/
struct STH2005MotionKey
{
	STH2005ArchiveKey archive;
	const char* mtp;
	const char* motion;
	const char* bon;
	bool attachedPart = false;
};

/* A .TXD entry of an archive, decoded the first time an instance needs it. */
struct STH2005TextureKey
{
	STH2005ArchiveKey archive;
	const char* entry;
};
