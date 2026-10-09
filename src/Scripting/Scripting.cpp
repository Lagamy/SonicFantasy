#include "Scripting.h"
#include "ScriptInternal.h"
#include "ScriptLog.h"
#include "SASet.h"
#include "Globals.h"
#include "API.h"
#include "EntityBehavior.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <cstring>
#include <filesystem>
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

bool loadFile(const char* path_, const char* root_)
{
	if(pState == nullptr) return false;
	std::string path = std::string(root_) + path_;
	if(luaL_loadfile(pState, path.c_str()) != LUA_OK)
	{
		reportError(lua_tostring(pState, -1));
		lua_pop(pState, 1);
		return false;
	}
	return protectedCall(0, 0);
}

void loadExternalScripts()
{
	if(std::filesystem::exists(std::string(ExternalScriptsRoot) + "main.lua")) loadFile("main.lua", ExternalScriptsRoot);

	/* Then every .lua of the two kind folders, Entity (sf.behavior) and BoundArea (sf.onTouch), in name order so the load order is the same every run. */
	for(const char* pFolder : {"Entity", "BoundArea"})
	{
		std::filesystem::path folder = std::filesystem::path(ExternalScriptsRoot) / pFolder;
		if(!std::filesystem::is_directory(folder)) continue;
		std::vector<std::string> files;
		for(const auto& rEntry : std::filesystem::recursive_directory_iterator(folder))
		{
			if(rEntry.is_regular_file() && rEntry.path().extension() == ".lua") files.push_back(std::filesystem::relative(rEntry.path(), ExternalScriptsRoot).generic_string());
		}
		std::sort(files.begin(), files.end());
		for(const std::string& rFile : files) loadFile(rFile.c_str(), ExternalScriptsRoot);
	}
}

/* A vector of three numbers as a table {x, y, z}. */
static void pushVector(float x_, float y_, float z_);

/*
Writes the declared fields (EntityBehavior::fields) of a behaviour into the entity's sf.state table, once per entity and behaviour: the value the editor set (Entity::externals) or the declared default. An entity that is gone (a reference
to it was kept) or a prefab that is gone reads as sf.none (-1) / nil. An entity's state table is made here if it has none yet, as sf.state does.
*/
static void applyFields(PoolId entityId_, PoolId behaviorId_)
{
	Entity& rEntity = Globals::Game::entities[entityId_];
	const EntityBehavior& rBehavior = Globals::Game::entityBehaviorTypes[behaviorId_];
	if(rBehavior.fields.empty() || std::find(rEntity.externalsApplied.begin(), rEntity.externalsApplied.end(), behaviorId_) != rEntity.externalsApplied.end()) return;
	rEntity.externalsApplied.push_back(behaviorId_);

	if(rEntity.scriptStateRef == 0)
	{
		lua_newtable(pState);
		lua_pushvalue(pState, -1);
		rEntity.scriptStateRef = luaL_ref(pState, LUA_REGISTRYINDEX);
	}
	else
	{
		lua_rawgeti(pState, LUA_REGISTRYINDEX, rEntity.scriptStateRef);
	}
	const std::map<std::string, ExternalValue>* pSet = nullptr;
	auto set = rEntity.externals.find(Globals::Game::entityBehaviorTypes.names[behaviorId_.id]);
	if(set != rEntity.externals.end()) pSet = &set->second;

	for(const ExternalField& rField : rBehavior.fields)
	{
		const ExternalValue* pValue = &rField.defaultValue;
		if(pSet != nullptr)
		{
			auto found = pSet->find(rField.name);
			if(found != pSet->end()) pValue = &found->second;
		}
		switch(rField.type)
		{
		case ExternalType::Number:
			lua_pushnumber(pState, pValue->number);
			break;
		case ExternalType::Bool:
			lua_pushboolean(pState, pValue->flag);
			break;
		case ExternalType::String:
			lua_pushstring(pState, pValue->text.c_str());
			break;
		case ExternalType::Vec3:
			pushVector(pValue->vector.x, pValue->vector.y, pValue->vector.z);
			break;
		case ExternalType::Entity:
			lua_pushinteger(pState, entityExists(pValue->entity) ? toHandle(pValue->entity) : toHandle(UninitializedPoolId));
			break;
		case ExternalType::Prefab:
		{
			PoolId prefabId = pValue->prefab;
			if(prefabId.id < Globals::Game::prefabs.objects.size() && Globals::Game::prefabs.alive[prefabId.id] && Globals::Game::prefabs.generation[prefabId.id] == prefabId.generation) lua_pushstring(pState, Globals::Game::prefabs.names[prefabId.id].c_str());
			else lua_pushnil(pState);
			break;
		}
		}
		lua_setfield(pState, -2, rField.name.c_str());
	}
	lua_pop(pState, 1);
}

void callBehavior(int scriptFunction_, PoolId entityId_, PoolId behaviorId_)
{
	if(pState == nullptr || failed.contains(scriptFunction_)) return;
	applyFields(entityId_, behaviorId_);
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
