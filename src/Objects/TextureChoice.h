#pragma once
#include <string>

/*
The texture the editor gave an entity (or all the entities of a prefab) in place of the one its model was made with. Default = none: the model's own.
Disc: another texture of the game's discs; name is a SADX .gvm path (the whole texture set replaces the entity's, so it has to hold every texture the model's materials index) or, for a Shadow entity, a .TXD entry of the
entity's own archive (a name the entry does not have leaves that submesh untextured). External: the texture of that name in the external pools (Loading::findExternalTexture: the png/jpeg files of Assets/External), drawn on every submesh that has a texture.
Compared by value; saved in the level file as {"kind": "disc"|"external", "name": ...}.
*/
enum class TextureChoiceKind
{
	Default,
	Disc,
	External
};

struct TextureChoice
{
	TextureChoiceKind kind = TextureChoiceKind::Default;
	std::string name;

	bool operator==(const TextureChoice&) const = default;
};
