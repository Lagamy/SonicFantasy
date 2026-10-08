#pragma once
#include "PoolId.h"

namespace Ninja
{
struct SAPlacement;
}

/*
The scripting layer: gameplay written in Lua 5.4 (third-party/lua, compiled as C++ so a Lua error is a C++ exception and nothing is longjmp'ed over a C++ frame) over the engine, which stays C++: one physics and
collision system for both games, rendering, animation. A script sees the engine through the global table `sf` (ScriptAPI.cpp) and keeps what its objects remember in plain Lua tables (sf.state(entity)), so a new
field needs no recompile. Scripts are read from Assets/Scripts (the repo's Scripts folder, copied next to the executable by the build like the shaders); init() runs nothing, main.cpp loads the entry script.
An entity, a tag or a behaviour reaches a script as an integer handle (its PoolId packed in 64 bits: generation << 32 | id), -1 (sf.none) for none; a handle of an entity that is gone makes the call that gets it fail,
sf.exists says. Only base, table, string, math, utf8 and coroutine libraries are open (no io, os, package or debug, and no dofile / loadfile): a script reaches the files and the machine only through sf.
A function a script registers (a behaviour, a collision interaction) that raises an error prints it with a traceback on stderr once and is not called again. Errors and what print writes are also kept in ScriptLog.h for the editor's Output tab.
*/
namespace Scripting
{
/* Creates the Lua state and the `sf` table; false if it already exists. Call before anything that needs a script (setupPrefab, attachPlayer), after BABehaviors::init (scripts find the built-in tags). */
void init();

/* Closes the state. Registry references held elsewhere (behaviour types) die with it; call at the end, once the game stops running. */
void destroy();

/* True between init and destroy. */
bool ready();

/* Runs a script of Assets/Scripts (path_ relative to it); an error is printed on stderr and gives false. */
bool loadFile(const char* path_);

/* Called by the gameplay system and the collision system for a function a script registered (a Lua registry reference). */
void callBehavior(int scriptFunction_, PoolId entityId_);
void callInteraction(int scriptFunction_, PoolId parentEntity_, PoolId contactEntity_);

/*
A prefab's behaviour (sf.prefabBehavior(prefabName, fn), prefabName as Globals::Game::prefabs names it, e.g. "SADX/SPRING"): setupPrefab calls fn(entity, placement) for every entity spawnPrefab makes from that prefab, placement the SET record as a table {type, rotation = {x, y, z}
in BAMS, position = {x, y, z}, scale = {x, y, z}}, nil if the object has none; a prefab with none is skipped. hasPrefabBehavior says whether one is registered.
The other hook into the scripts' own functions is the global `attachPlayer(entity)`; a script that does not define it is skipped.
*/
void setupPrefab(PoolId entityId_, const char* prefabName_, const Ninja::SAPlacement* pPlacement_);
bool hasPrefabBehavior(const char* prefabName_);
void attachPlayer(PoolId entityId_);

/* Removes the levels a script asked to unload (sf.unloadLevel); the gameplay system calls it at the start of every tick. */
void processPendingUnloads();

/*
Puts the game back to its start (the editor's Stop): the script's global resetGame(unloaded) is called with false, where it asks for its level to go (sf.unloadLevel); the removal is done here, now, since no tick
is running; then resetGame(true) is called, where it loads the level again and puts the players at its start. Both calls get the level's name as a second argument (nil without a name): a name makes the script load that level instead (the editor's File > Load level). A script that does not define resetGame is skipped.
*/
void resetGame(const char* pLevelName_ = nullptr);

/* Gives back an entity's state table (Entity::scriptStateRef); Entity::destroy calls it. Safe after destroy(). */
void releaseState(int stateRef_);
}
