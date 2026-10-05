#include "Scripting.h"
#include "ScriptInternal.h"
#include "SASet.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <cstdio>
#include <string>
#include <unordered_set>

namespace Scripting
{
static lua_State* pState = nullptr;

/* Registry references of functions that raised an error: they are not called again. */
static std::unordered_set<int> failed;

lua_State* state()
{
	return pState;
}

bool ready()
{
	return pState != nullptr;
}

static int addTraceback(lua_State* pLua_)
{
	const char* pMessage = lua_tostring(pLua_, 1);
	luaL_traceback(pLua_, pLua_, pMessage != nullptr ? pMessage : "(the error is not a string)", 1);
	return 1;
}

/* Calls the function under the nArguments_ arguments on the stack, printing an error with its traceback on stderr. The results (nResults_) are left on the stack. */
static bool protectedCall(int nArguments_, int nResults_)
{
	int base = lua_gettop(pState) - nArguments_;
	lua_pushcfunction(pState, addTraceback);
	lua_insert(pState, base);
	int status = lua_pcall(pState, nArguments_, nResults_, base);
	lua_remove(pState, base);
	if(status != LUA_OK)
	{
		std::fprintf(stderr, "[lua] %s\n", lua_tostring(pState, -1));
		lua_pop(pState, 1);
		return false;
	}
	return true;
}

void init()
{
	if(pState != nullptr) return;
	pState = luaL_newstate();
	luaL_requiref(pState, "_G", luaopen_base, 1);
	luaL_requiref(pState, LUA_TABLIBNAME, luaopen_table, 1);
	luaL_requiref(pState, LUA_STRLIBNAME, luaopen_string, 1);
	luaL_requiref(pState, LUA_MATHLIBNAME, luaopen_math, 1);
	luaL_requiref(pState, LUA_UTF8LIBNAME, luaopen_utf8, 1);
	luaL_requiref(pState, LUA_COLIBNAME, luaopen_coroutine, 1);
	lua_pop(pState, 6);
	lua_pushnil(pState);
	lua_setglobal(pState, "dofile");
	lua_pushnil(pState);
	lua_setglobal(pState, "loadfile");
	registerAPI(pState);
}

void destroy()
{
	if(pState == nullptr) return;
	lua_close(pState);
	pState = nullptr;
	failed.clear();
}

bool loadFile(const char* path_)
{
	if(pState == nullptr) return false;
	std::string path = std::string("Assets/Scripts/") + path_;
	if(luaL_loadfile(pState, path.c_str()) != LUA_OK)
	{
		std::fprintf(stderr, "[lua] %s\n", lua_tostring(pState, -1));
		lua_pop(pState, 1);
		return false;
	}
	return protectedCall(0, 0);
}

void callBehavior(int scriptFunction_, PoolId entityId_)
{
	if(pState == nullptr || failed.contains(scriptFunction_)) return;
	lua_rawgeti(pState, LUA_REGISTRYINDEX, scriptFunction_);
	lua_pushinteger(pState, toHandle(entityId_));
	if(!protectedCall(1, 0))
	{
		failed.insert(scriptFunction_);
		std::fprintf(stderr, "[lua] that behaviour is switched off.\n");
	}
}

void callInteraction(int scriptFunction_, PoolId parentEntity_, PoolId contactEntity_)
{
	if(pState == nullptr || failed.contains(scriptFunction_)) return;
	lua_rawgeti(pState, LUA_REGISTRYINDEX, scriptFunction_);
	lua_pushinteger(pState, toHandle(parentEntity_));
	lua_pushinteger(pState, toHandle(contactEntity_));
	if(!protectedCall(2, 0))
	{
		failed.insert(scriptFunction_);
		std::fprintf(stderr, "[lua] that interaction is switched off.\n");
	}
}

/* A vector of three numbers as a table {x, y, z}. */
static void pushVector(float x_, float y_, float z_)
{
	lua_createtable(pState, 0, 3);
	lua_pushnumber(pState, x_);
	lua_setfield(pState, -2, "x");
	lua_pushnumber(pState, y_);
	lua_setfield(pState, -2, "y");
	lua_pushnumber(pState, z_);
	lua_setfield(pState, -2, "z");
}

void setupObject(PoolId entityId_, const char* name_, const Ninja::SAPlacement* pPlacement_)
{
	if(pState == nullptr) return;
	lua_getglobal(pState, "setupObject");
	if(!lua_isfunction(pState, -1))
	{
		lua_pop(pState, 1);
		return;
	}
	lua_pushinteger(pState, toHandle(entityId_));
	lua_pushstring(pState, name_);
	if(pPlacement_ == nullptr)
	{
		lua_pushnil(pState);
	}
	else
	{
		lua_createtable(pState, 0, 5);
		lua_pushinteger(pState, pPlacement_->type);
		lua_setfield(pState, -2, "type");
		pushVector(static_cast<float>(pPlacement_->rotation.x), static_cast<float>(pPlacement_->rotation.y), static_cast<float>(pPlacement_->rotation.z));
		lua_setfield(pState, -2, "rotation");
		pushVector(pPlacement_->position.x, pPlacement_->position.y, pPlacement_->position.z);
		lua_setfield(pState, -2, "position");
		pushVector(pPlacement_->scale.x, pPlacement_->scale.y, pPlacement_->scale.z);
		lua_setfield(pState, -2, "scale");
	}
	protectedCall(3, 0);
}

void attachPlayer(PoolId entityId_)
{
	if(pState == nullptr) return;
	lua_getglobal(pState, "attachPlayer");
	if(!lua_isfunction(pState, -1))
	{
		lua_pop(pState, 1);
		return;
	}
	lua_pushinteger(pState, toHandle(entityId_));
	protectedCall(1, 0);
}

void releaseState(int stateRef_)
{
	if(pState != nullptr && stateRef_ != 0) luaL_unref(pState, LUA_REGISTRYINDEX, stateRef_);
}
}
