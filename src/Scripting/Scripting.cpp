#include "Scripting.h"
#include "ScriptInternal.h"
#include "ScriptLog.h"
#include "SASet.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Scripting
{
static lua_State* pState = nullptr;

/* Registry references of functions that raised an error: they are not called again. */
static std::unordered_set<int> failed;

/* The Lua registry reference of the function each prefab name has (sf.prefabBehavior). */
static std::unordered_map<std::string, int> prefabBehaviors;

lua_State* state()
{
	return pState;
}

bool ready()
{
	return pState != nullptr;
}

/* An error report: on stderr as "[lua] <text>" and in the editor's Output tab (ScriptLog.h). */
static void reportError(const char* pText_)
{
	std::fprintf(stderr, "[lua] %s\n", pText_ != nullptr ? pText_ : "(the error is not a string)");
	addLog(true, pText_ != nullptr ? pText_ : "(the error is not a string)");
}

/* Lua's print: the arguments as text separated by tabs, written to stdout like the original print (tools read it) and kept for the Output tab. */
static int scriptPrint(lua_State* pLua_)
{
	int count = lua_gettop(pLua_);
	std::string line;
	for(int i = 1; i <= count; i++)
	{
		size_t length = 0;
		const char* pText = luaL_tolstring(pLua_, i, &length);
		if(i > 1) line += '\t';
		line.append(pText, length);
		lua_pop(pLua_, 1);
	}
	std::fprintf(stdout, "%s\n", line.c_str());
	addLog(false, std::move(line));
	return 0;
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
		reportError(lua_tostring(pState, -1));
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
	lua_pushcfunction(pState, scriptPrint);
	lua_setglobal(pState, "print");
	registerAPI(pState);
}

void destroy()
{
	if(pState == nullptr) return;
	lua_close(pState);
	pState = nullptr;
	failed.clear();
	prefabBehaviors.clear();
}

bool loadFile(const char* path_)
{
	if(pState == nullptr) return false;
	std::string path = std::string("Assets/Scripts/") + path_;
	if(luaL_loadfile(pState, path.c_str()) != LUA_OK)
	{
		reportError(lua_tostring(pState, -1));
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
		reportError("that behaviour is switched off.");
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
		reportError("that interaction is switched off.");
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

void registerPrefabBehavior(const char* prefabName_, int reference_)
{
	auto found = prefabBehaviors.find(prefabName_);
	if(found == prefabBehaviors.end())
	{
		prefabBehaviors.emplace(prefabName_, reference_);
		return;
	}
	if(pState != nullptr) luaL_unref(pState, LUA_REGISTRYINDEX, found->second);
	failed.erase(found->second);
	found->second = reference_;
}

bool hasPrefabBehavior(const char* prefabName_)
{
	return prefabBehaviors.contains(prefabName_);
}

void setupPrefab(PoolId entityId_, const char* prefabName_, const Ninja::SAPlacement* pPlacement_)
{
	if(pState == nullptr) return;
	auto found = prefabBehaviors.find(prefabName_);
	if(found == prefabBehaviors.end() || failed.contains(found->second)) return;
	lua_rawgeti(pState, LUA_REGISTRYINDEX, found->second);
	lua_pushinteger(pState, toHandle(entityId_));
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
	if(!protectedCall(2, 0))
	{
		failed.insert(found->second);
		reportError(std::format("the behaviour of {} is switched off.", prefabName_).c_str());
	}
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

/* One call of the script's resetGame(unloaded_, levelName_), if it defines one (levelName_ nil = the same level). */
static void callResetHook(bool unloaded_, const char* pLevelName_)
{
	lua_getglobal(pState, "resetGame");
	if(!lua_isfunction(pState, -1))
	{
		lua_pop(pState, 1);
		return;
	}
	lua_pushboolean(pState, unloaded_);
	if(pLevelName_ != nullptr) lua_pushstring(pState, pLevelName_);
	else lua_pushnil(pState);
	protectedCall(2, 0);
}

void resetGame(const char* pLevelName_)
{
	if(pState == nullptr) return;
	callResetHook(false, pLevelName_);
	processPendingUnloads();
	callResetHook(true, pLevelName_);
}

void releaseState(int stateRef_)
{
	if(pState != nullptr && stateRef_ != 0) luaL_unref(pState, LUA_REGISTRYINDEX, stateRef_);
}
}
