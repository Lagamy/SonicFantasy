#pragma once
#include <cstdint>
#include "PoolId.h"

struct lua_State;

/* What the files of the scripting layer share (Scripting.cpp, ScriptAPI.cpp); not for the rest of the engine. */
namespace Scripting
{
lua_State* state();

/* An entity, tag or behaviour as the integer a script holds: generation << 32 | id (-1 for UninitializedPoolId, whose two halves are all ones). */
inline int64_t toHandle(PoolId id_)
{
	return static_cast<int64_t>((static_cast<uint64_t>(id_.generation) << 32) | id_.id);
}

inline PoolId fromHandle(int64_t handle_)
{
	uint64_t bits = static_cast<uint64_t>(handle_);
	return PoolId{static_cast<uint32_t>(bits & 0xFFFFFFFFu), static_cast<uint32_t>(bits >> 32)};
}

/* Registers the function in the Lua registry (reference_, a luaL_ref) as the behaviour of a prefab (sf.prefabBehavior); a second one for the same name replaces the first, whose reference is released. */
void registerPrefabBehavior(const char* prefabName_, int reference_);

/* Registers the global table `sf`. */
void registerAPI(lua_State* pState_);
}
