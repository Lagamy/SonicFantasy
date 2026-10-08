#pragma once
#include <string>
#include <glm/glm.hpp>
#include "PoolId.h"

/* The kinds of value a script behaviour can declare as a field (sf.behavior's third argument): the inspector shows a widget for each; entity and prefab are set by drag and drop. */
enum class ExternalType
{
	Number,
	Bool,
	String,
	Vec3,
	Entity,
	Prefab
};

/*
One value of a field. Only the member the field's type names is used. An entity is a PoolId in Globals::Game::entities and a prefab one in Globals::Game::prefabs (UninitializedPoolId = none). entityIndex is the entity's place in
its level while the value is not on a live entity (kept over Stop, Editor::KeptEntity): an entity's PoolId does not survive the level being made again.
*/
struct ExternalValue
{
	double number = 0.0;
	bool flag = false;
	std::string text;
	glm::vec3 vector = glm::vec3(0.0f);
	PoolId entity = UninitializedPoolId;
	PoolId prefab = UninitializedPoolId;
	int32_t entityIndex = -1;

	bool operator==(const ExternalValue&) const = default;
};

/* A field a behaviour declares: its name in sf.state(entity), its type and the value it has when nothing was set. external = shown in the inspector (set per entity); otherwise it is only a declared default. */
struct ExternalField
{
	std::string name;
	ExternalType type = ExternalType::Number;
	ExternalValue defaultValue;
	bool external = false;
};
