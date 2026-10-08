#pragma once
#include <string>

/*
The model the editor gave an entity (or all the entities of a prefab) in place of the one it was made with. Default = none: the entity's own.
Disc: another model of the game's discs; name is, for a SADX entity, "<rel path>@0x<offset in hex>" (the name of its SAMesh: the model is drawn with the entity's own texture set, so the set must hold the texture ids it uses and its motion stops) and, for a Shadow entity, a
.DFF entry of the entity's own archive (drawn as a static model, with the entity's own texture dictionary). External: the model of that name in the external pools (Loading::findExternalModel; Assets/External/Models), drawn in place of the instance's model with its own textures.
Compared by value; saved in the level file as {"kind": "default"|"disc"|"external", "name": ...}.
*/
enum class MeshChoiceKind
{
	Default,
	Disc,
	External
};

struct MeshChoice
{
	MeshChoiceKind kind = MeshChoiceKind::Default;
	std::string name;

	bool operator==(const MeshChoice&) const = default;
};
