#include "ScriptInternal.h"
#include "Scripting.h"
#include "API.h"
#include "Globals.h"
#include "BABehaviors.h"
#include "ResourceKeys.h"
#include "LevelLoader.h"
#include "PlayerLoader.h"
#include "EntityBehavior.h"
#include "GPU_Globals.h"
#include "GPU_API.h"
#include "ONEInstance.h"
#include "DebugSetup.h"
#include "Editor.h"
#include "lua.h"
#include "lauxlib.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <exception>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/*
The global table `sf`: what a script can ask of the engine. Every function is a thin wrapper over the engine's own API (API.h and the pools), written to stay simple; each one that takes an entity fails with a Lua
error if the handle is of an entity that is gone. Vectors are three numbers (x, y, z) both ways. Angles are in BAMS (65536 = a turn) where the games use them. The list, grouped:
- time and game: tickRate(), tickTime(), rings(), setRings(n), debugMode(), key(name), cameraForward(), followCamera(e)
- animation: animate(e, name [, framesPerGameFrame]) (Sonic's motions by name: stand, walk, speedwalk, jog, run, sprint, brake, turnAround, springFall, fall, landing, damage, jump (curled model), ball (jumpball model); for a Shadow entity: stand, walk, run, jump, jumpB (play once), jumpC, fall, brake, damage, skate, highSkate; it loops), pose(e, {{name, frame, weight}, ...}), setAnimationScaleMode(n), setAnimationEulerOrder(n), motionRange(e, name) and bone(e, name) (a Shadow entity's mixed pose, see below), lowestPoint(e) (the lowest y of a Shadow entity's vertices, posed and at rest), blink(e, name | false) (draws that animation in place of the animate one while it is on; the animate one keeps its own clock)
- entities: exists(e), name(e), position(e), setPosition(e, x, y, z), moveTo(e, x, y, z), turnY(e, radians), orient(e, yaw, nx, ny, nz), destroy(e), state(e), axis(e, x, y, z), setUncullable(e, bool), postEvent(name, e [, x, y, z]), setDashTrail(e, bool) (the jump dash ribbon, Graphics::DashTrail), setEffectBall(e, kind, bool) (Shadow's effect balls, Graphics::EffectBalls; kind "jumpA", "dashB" or "fallB")
- bodies (an entity with a physics): hasBody(e), velocity(e), setVelocity(e, x, y, z), setGroundSnap(e, distance), setGroundSnapMode(e, {ray, radius, minNormalY, blockedByWall}), setMeanVelocityMove(e, on), setWeight(e, w), grounded(e), groundNormal(e), groundSurface(e), groundGap(e, maxGap), touchingWall(e), wallNormal(e)
- behaviours: behavior(name, fn [, fields]) (fields = {name = {type = "number" | "bool" | "string" | "vec3" | "entity" | "prefab", default = v, external = bool}}: written into sf.state(e) before the first call; the external ones are set per entity in the editor's Inspector; a prefab field holds the prefab's name, an entity field a handle or sf.none, a vec3 {x, y, z}), give(e, name), prefabBehavior(prefabName, fn) (fn(entity, placement) is called for every entity made from that prefab, prefabName like "SADX/SPRING"; placement is the SET record {type, rotation, position, scale} or nil)
- collision: onTouch(name, fn), tag(name), collisionBehavior(name, {tags, collidesWith, interactions}), setShapeBehavior(e, behavior)
- setup: spawn(prefabName, x, y, z) (makes an entity from a prefab by its name, "SADX/SPRING", "Global/Bullet"; returns it), loadLevel(name) (the name of a Levels/<name>/ folder: "EmeraldCoast", "Stg0100", "Stg0000"; returns the level), unloadLevel(level) (done at the start of the next tick), levelExists(level), spawnPlayer(character, x, y, z) ("sonic", "shadow"; returns the entity, which the script then attaches), setCamera(x, y, z)
- scripts: include(path), rotateBamsZXY(x, y, z, az, ax, ay), none
*/
namespace Scripting
{
static PoolId checkEntity(lua_State* pLua_, int arg_)
{
	lua_Integer handle = luaL_checkinteger(pLua_, arg_);
	PoolId id = fromHandle(handle);
	if(!entityExists(id)) luaL_error(pLua_, "entity %I does not exist (any more)", handle);
	return id;
}

static Physics::PhysicsBody& checkBody(lua_State* pLua_, int arg_)
{
	PoolId id = checkEntity(pLua_, arg_);
	const Entity& rEntity = Globals::Game::entities[id];
	if(rEntity.physicsId == UninitializedPoolId) luaL_error(pLua_, "entity %I has no physics body", toHandle(id));
	return Globals::Physics::bodies[rEntity.physicsId];
}

static int pushVector(lua_State* pLua_, const glm::vec3& rVector_)
{
	lua_pushnumber(pLua_, rVector_.x);
	lua_pushnumber(pLua_, rVector_.y);
	lua_pushnumber(pLua_, rVector_.z);
	return 3;
}

static glm::vec3 checkVector(lua_State* pLua_, int firstArg_)
{
	return glm::vec3(static_cast<float>(luaL_checknumber(pLua_, firstArg_)), static_cast<float>(luaL_checknumber(pLua_, firstArg_ + 1)), static_cast<float>(luaL_checknumber(pLua_, firstArg_ + 2)));
}

/* A C++ exception of the engine (a std::runtime_error from a pool or a loader) becomes a Lua error with its message instead of a crash. */
template<int (*Function)(lua_State*)>
static int guarded(lua_State* pLua_)
{
	try
	{
		return Function(pLua_);
	}
	catch(const std::exception& rError)
	{
		return luaL_error(pLua_, "%s", rError.what());
	}
}

/* ---- time and game ---- */

static int tickRate(lua_State* pLua_)
{
	lua_pushnumber(pLua_, Globals::Game::gameplaySystem.tickRate);
	return 1;
}

static int tickTime(lua_State* pLua_)
{
	lua_pushnumber(pLua_, Globals::Game::gameplaySystem.tickTime());
	return 1;
}

static int rings(lua_State* pLua_)
{
	lua_pushinteger(pLua_, Globals::Game::rings);
	return 1;
}

static int setRings(lua_State* pLua_)
{
	Globals::Game::rings = static_cast<int>(luaL_checkinteger(pLua_, 1));
	return 0;
}

static int debugMode(lua_State* pLua_)
{
	lua_pushboolean(pLua_, editorActive());
	return 1;
}

/* Whether a key is held: "A" to "Z", "SPACE", "SHIFT", "CTRL". */
static int key(lua_State* pLua_)
{
	std::string name = luaL_checkstring(pLua_, 1);
	int code;
	if(name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') code = GLFW_KEY_A + (name[0] - 'A');
	else if(name == "SPACE") code = GLFW_KEY_SPACE;
	else if(name == "SHIFT") code = GLFW_KEY_LEFT_SHIFT;
	else if(name == "CTRL") code = GLFW_KEY_LEFT_CONTROL;
	else return luaL_error(pLua_, "unknown key '%s'", name.c_str());
	lua_pushboolean(pLua_, glfwGetKey(Globals::App::window, code) == GLFW_PRESS);
	return 1;
}

/*
The motions an entity can be made to play by name: Sonic's (an SA entity: its motion and the model it is made for) and Shadow's (an STH2005 entity: its motion and whether it loops). Both tables are read from
Assets/External/Players/Sonic.json and Shadow.json (Loading::loadPlayerKeys). animate picks the Shadow table for an entity of the STH2005_ONE engine.
Harmless to call every tick: the entity plays a motion when it differs from the one playing.
*/
using Animation = Loading::SonicAnimation;
using ShadowAnimation = Loading::ShadowAnimation;

static const std::unordered_map<std::string, Animation>& animationTable()
{
	return Loading::sonicKeys().animations;
}

static const std::unordered_map<std::string, ShadowAnimation>& shadowAnimationTable()
{
	return Loading::shadowKeys().animations;
}

static int animate(lua_State* pLua_)
{
	const auto& animations = animationTable();
	PoolId id = checkEntity(pLua_, 1);
	std::string name = luaL_checkstring(pLua_, 2);
	if(Globals::Game::entities[id].sourceEngine == STH2005_ONE)
	{
		const auto& shadowAnimations = shadowAnimationTable();
		auto shadowFound = shadowAnimations.find(name);
		if(shadowFound == shadowAnimations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
		changeAnimation(id, *shadowFound->second.pMotion, shadowFound->second.loop);
		/* The optional third argument is the playback speed as a multiple of the 60 frames a second a motion plays at (0 or none = 1). */
		Globals::Game::entities[id].animationRate = static_cast<float>(luaL_optnumber(pLua_, 3, 0.0));
		return 0;
	}
	auto found = animations.find(name);
	if(found == animations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
	changeAnimation(id, *found->second.pMotion, *found->second.pMesh);
	/* The optional third argument is the playback speed in motion frames per game frame (the game's own unit: external, FUN_804cf4f4 adds speed times a table value each game frame); 0 or none = the default rate. */
	Globals::Game::entities[id].animationRate = static_cast<float>(luaL_optnumber(pLua_, 3, 0.0)) * Globals::Game::gameplaySystem.tickRate;
	return 0;
}

/*
pose(e, {{name, frame, weight}, ...}): a Shadow entity's main skeleton shows these motions (the names of the table above) sampled at those frames, mixed by weight (ONEInstance::setPose); an empty table ends it. The timing is the script's own.
motionRange(e, name): the first and last frame the game plays that motion in (STH2005::Motion::start, end; most are 0 to the motion's length).
*/
static int pose(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	luaL_checktype(pLua_, 2, LUA_TTABLE);
	const auto& shadowAnimations = shadowAnimationTable();
	std::vector<Graphics::PoseRequest> layers;
	for(lua_Integer i = 1; lua_geti(pLua_, 2, i) != LUA_TNIL; i++)
	{
		lua_geti(pLua_, -1, 1);
		std::string name = luaL_checkstring(pLua_, -1);
		lua_geti(pLua_, -2, 2);
		float frame = static_cast<float>(luaL_checknumber(pLua_, -1));
		lua_geti(pLua_, -3, 3);
		float weight = static_cast<float>(luaL_checknumber(pLua_, -1));
		lua_pop(pLua_, 4);
		auto found = shadowAnimations.find(name);
		if(found == shadowAnimations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
		layers.push_back(Graphics::PoseRequest{found->second.pMotion, frame, weight});
	}
	lua_pop(pLua_, 1);
	Globals::Game::entities[id].setONEPose(layers);
	return 0;
}

/* setAnimationScaleMode(n): Graphics::ONEScaleMode, a debug switch for what the motions' scale channels do (0 scale and compensation, 1 neither, 2 scale only). */
static int setAnimationScaleMode(lua_State* pLua_)
{
	Graphics::ONEScaleMode = static_cast<int>(luaL_checkinteger(pLua_, 1));
	return 0;
}

/* setAnimationEulerOrder(n): Graphics::ONEEulerOrder, a debug switch for the order the Euler angles of a motion are applied in (0 to 5). */
static int setAnimationEulerOrder(lua_State* pLua_)
{
	Graphics::ONEEulerOrder = static_cast<int>(luaL_checkinteger(pLua_, 1));
	return 0;
}

/*
bone(e, name): for the debug log, a frame of a Shadow entity's skeleton as it was posed last: local translation x y z, local rotation as Euler angles in radians (Rz * Ry * Rx) x y z, scale x y z, and the position in the model's own space x y z (12 numbers);
nothing if the skeleton has no frame of that name.
*/
static int bone(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	std::string name = luaL_checkstring(pLua_, 2);
	const Entity& rEntity = Globals::Game::entities[id];
	if(rEntity.sourceEngine != STH2005_ONE || rEntity.instanceId == UninitializedPoolId) return luaL_error(pLua_, "entity %I has no ONE instance", toHandle(id));
	float values[12];
	if(!Graphics::getONEInstance(rEntity.instanceId).boneInfo(name, values)) return 0;
	for(float value : values) lua_pushnumber(pLua_, value);
	return 12;
}

/*
lowestPoint(e): for the debug log, the lowest y of a Shadow entity's skinned vertices in the model's own space: as posed last, then in the rest pose (2 numbers; the sole against the model's origin).
*/
static int lowestPoint(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	const Entity& rEntity = Globals::Game::entities[id];
	if(rEntity.sourceEngine != STH2005_ONE || rEntity.instanceId == UninitializedPoolId) return luaL_error(pLua_, "entity %I has no ONE instance", toHandle(id));
	float values[2];
	Graphics::getONEInstance(rEntity.instanceId).lowestPoint(values);
	lua_pushnumber(pLua_, values[0]);
	lua_pushnumber(pLua_, values[1]);
	return 2;
}

static int motionRange(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	std::string name = luaL_checkstring(pLua_, 2);
	const auto& shadowAnimations = shadowAnimationTable();
	auto found = shadowAnimations.find(name);
	if(found == shadowAnimations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
	const Entity& rEntity = Globals::Game::entities[id];
	if(rEntity.sourceEngine != STH2005_ONE || rEntity.instanceId == UninitializedPoolId) return luaL_error(pLua_, "entity %I has no ONE instance", toHandle(id));
	const Graphics::ONEInstance& rInstance = Graphics::getONEInstance(rEntity.instanceId);
	const STH2005::Motion& rMotion = Graphics::oneArchives[rInstance.archiveId].motions[Graphics::oneArchives[rInstance.archiveId].useMotion(*found->second.pMotion)];
	lua_pushnumber(pLua_, rMotion.start);
	lua_pushnumber(pLua_, rMotion.end != 0 ? rMotion.end : rMotion.lastFrame);
	return 2;
}

/* blink(e, name): draws that animation in place of the one animate set while it is on, which keeps its own clock; blink(e, false) or nil ends it. */
static int blink(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	if(!lua_isstring(pLua_, 2))
	{
		blinkAnimation(id, nullptr, nullptr);
		return 0;
	}
	const auto& animations = animationTable();
	std::string name = lua_tostring(pLua_, 2);
	auto found = animations.find(name);
	if(found == animations.end()) return luaL_error(pLua_, "unknown animation '%s'", name.c_str());
	blinkAnimation(id, found->second.pMotion, found->second.pMesh);
	return 0;
}

static int cameraForward(lua_State* pLua_)
{
	return pushVector(pLua_, getMainCamera().front);
}

/* followCamera(e): the entity the camera follows outside the debug mode (the active player). */
static int followCamera(lua_State* pLua_)
{
	Globals::Game::cameraTarget = checkEntity(pLua_, 1);
	return 0;
}

/* ---- entities ---- */

static int exists(lua_State* pLua_)
{
	lua_pushboolean(pLua_, entityExists(fromHandle(luaL_checkinteger(pLua_, 1))));
	return 1;
}

static int name(lua_State* pLua_)
{
	lua_pushstring(pLua_, Globals::Game::entities.getName(checkEntity(pLua_, 1)).c_str());
	return 1;
}

static int position(lua_State* pLua_)
{
	return pushVector(pLua_, Globals::Game::entities[checkEntity(pLua_, 1)].position);
}

static int setPosition(lua_State* pLua_)
{
	Globals::Game::entities[checkEntity(pLua_, 1)].setPosition(checkVector(pLua_, 2));
	return 0;
}

/* Turns an entity about +Y by an angle in radians (right-handed, like glm::rotate), replacing its rotation; kept as the previous one for the tick's interpolation like moveTo does for the position. */
static int turnY(lua_State* pLua_)
{
	float angle = static_cast<float>(luaL_checknumber(pLua_, 2));
	Globals::Game::entities[checkEntity(pLua_, 1)].turnTo(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f)));
	return 0;
}

/*
orient(e, yaw, nx, ny, nz): the entity's rotation is the turn that takes +Y to the unit-length direction (nx, ny, nz) (about the axis they span, the smallest turn) times the turn about +Y by yaw (right-handed, radians): a model standing
on a slope, turned about its own up axis. This is the game's orientation matrix (decompile: the ground-normal basis of FUN_800934fc times the yaw, FUN_80079628).
*/
static int orient(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	float yaw = static_cast<float>(luaL_checknumber(pLua_, 2));
	glm::vec3 up = glm::normalize(checkVector(pLua_, 3));
	glm::mat4 align(1.0f);
	float d = glm::clamp(up.y, -1.0f, 1.0f);
	glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), up);
	if(glm::length(axis) > 1e-6f) align = glm::rotate(glm::mat4(1.0f), std::acos(d), glm::normalize(axis));
	Globals::Game::entities[id].turnTo(align * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
	return 0;
}

static int moveTo(lua_State* pLua_)
{
	Globals::Game::entities[checkEntity(pLua_, 1)].moveTo(checkVector(pLua_, 2));
	return 0;
}

static int destroy(lua_State* pLua_)
{
	Globals::Game::entities[checkEntity(pLua_, 1)].markForDestroy();
	return 0;
}

/* The entity's table for what its behaviours remember, made on the first call and kept until the entity is removed. */
static int state(lua_State* pLua_)
{
	Entity& rEntity = Globals::Game::entities[checkEntity(pLua_, 1)];
	if(rEntity.scriptStateRef == 0)
	{
		lua_newtable(pLua_);
		lua_pushvalue(pLua_, -1);
		rEntity.scriptStateRef = luaL_ref(pLua_, LUA_REGISTRYINDEX);
	}
	else
	{
		lua_rawgeti(pLua_, LUA_REGISTRYINDEX, rEntity.scriptStateRef);
	}
	return 1;
}

/* A vector of the entity's own space turned into the world by the entity's rotation (the spring's up axis, for one). */
static int axis(lua_State* pLua_)
{
	const Entity& rEntity = Globals::Game::entities[checkEntity(pLua_, 1)];
	return pushVector(pLua_, glm::vec3(rEntity.rotation * glm::vec4(checkVector(pLua_, 2), 0.0f)));
}

static int setUncullable(lua_State* pLua_)
{
	Globals::Game::entities[checkEntity(pLua_, 1)].behaviorNotCulled = lua_toboolean(pLua_, 2) != 0;
	return 0;
}

/* A presentation event (GameEvents.h) at the entity's position, or at the given one. */
static int postEvent(lua_State* pLua_)
{
	const char* pName = luaL_checkstring(pLua_, 1);
	PoolId id = checkEntity(pLua_, 2);
	glm::vec3 where = lua_gettop(pLua_) >= 5 ? checkVector(pLua_, 3) : Globals::Game::entities[id].position;
	postGameEvent(pName, id, where);
	return 0;
}

/* Starts (on) or lets go of (off, it then fades out) the entity's dash ribbon (Graphics::DashTrail); on again while it has one attached does nothing. */
static int setDashTrail(lua_State* pLua_)
{
	Graphics::dashTrail.setActive(checkEntity(pLua_, 1), lua_toboolean(pLua_, 2) != 0);
	return 0;
}

/* Starts (on) or ends (off) one of the entity's effect balls (Graphics::EffectBalls): kind is "jumpA", "dashB" or "fallB"; on again while it has that one does nothing. */
static int setEffectBall(lua_State* pLua_)
{
	std::string kind = luaL_checkstring(pLua_, 2);
	Graphics::EffectBalls::Kind ball;
	if(kind == "jumpA") ball = Graphics::EffectBalls::JumpBallA;
	else if(kind == "dashB") ball = Graphics::EffectBalls::DashBallB;
	else if(kind == "fallB") ball = Graphics::EffectBalls::FallBallB;
	else return luaL_error(pLua_, "sf.setEffectBall: unknown kind '%s' (jumpA, dashB or fallB).", kind.c_str());
	Graphics::effectBalls.setActive(checkEntity(pLua_, 1), ball, lua_toboolean(pLua_, 3) != 0);
	return 0;
}

/* ---- bodies ---- */

static int hasBody(lua_State* pLua_)
{
	lua_pushboolean(pLua_, Globals::Game::entities[checkEntity(pLua_, 1)].physicsId != UninitializedPoolId);
	return 1;
}

static int velocity(lua_State* pLua_)
{
	return pushVector(pLua_, checkBody(pLua_, 1).velocity);
}

static int setVelocity(lua_State* pLua_)
{
	checkBody(pLua_, 1).velocity = checkVector(pLua_, 2);
	return 0;
}

static int grounded(lua_State* pLua_)
{
	lua_pushboolean(pLua_, checkBody(pLua_, 1).grounded);
	return 1;
}

/* setGroundSnapMode(e, {ray = bool, radius = number, minNormalY = number, blockedByWall = bool}): how the snap finds the ground (PhysicsBody::snapByRay, snapRadius, snapMinNormalY, snapBlockedByWall); a field left out keeps its value. */
static int setGroundSnapMode(lua_State* pLua_)
{
	Physics::PhysicsBody& rBody = checkBody(pLua_, 1);
	luaL_checktype(pLua_, 2, LUA_TTABLE);
	if(lua_getfield(pLua_, 2, "ray") != LUA_TNIL) rBody.snapByRay = lua_toboolean(pLua_, -1) != 0;
	lua_pop(pLua_, 1);
	if(lua_getfield(pLua_, 2, "radius") != LUA_TNIL) rBody.snapRadius = static_cast<float>(luaL_checknumber(pLua_, -1));
	lua_pop(pLua_, 1);
	if(lua_getfield(pLua_, 2, "minNormalY") != LUA_TNIL) rBody.snapMinNormalY = static_cast<float>(luaL_checknumber(pLua_, -1));
	lua_pop(pLua_, 1);
	if(lua_getfield(pLua_, 2, "blockedByWall") != LUA_TNIL) rBody.snapBlockedByWall = lua_toboolean(pLua_, -1) != 0;
	lua_pop(pLua_, 1);
	return 0;
}

/* setWeight(e, w): the share of the global gravity the body gets (0 = none: a script that applies its own). */
static int setWeight(lua_State* pLua_)
{
	checkBody(pLua_, 1).weight = static_cast<float>(luaL_checknumber(pLua_, 2));
	return 0;
}

/* setMeanVelocityMove(e, on): whether the body's move takes the mean of the tick's start and end velocities (PhysicsBody::moveByMeanVelocity). */
static int setMeanVelocityMove(lua_State* pLua_)
{
	checkBody(pLua_, 1).moveByMeanVelocity = lua_toboolean(pLua_, 2) != 0;
	return 0;
}

/* setGroundSnap(e, distance): see PhysicsBody::snapDistance; 0 turns it off. */
static int setGroundSnap(lua_State* pLua_)
{
	checkBody(pLua_, 1).snapDistance = static_cast<float>(luaL_checknumber(pLua_, 2));
	return 0;
}

static int groundNormal(lua_State* pLua_)
{
	return pushVector(pLua_, checkBody(pLua_, 1).groundNormal);
}

/* The name of the collision behaviour of the surface the body stands on (nil in the air). */
static int groundSurface(lua_State* pLua_)
{
	const Physics::PhysicsBody& rBody = checkBody(pLua_, 1);
	if(!rBody.grounded || rBody.groundSurface == UninitializedPoolId) lua_pushnil(pLua_);
	else lua_pushstring(pLua_, Globals::Game::behaviors.getName(rBody.groundSurface).c_str());
	return 1;
}

/* groundGap(e, maxGap): how far the bottom of the entity's first sphere or capsule is above the level straight below it (CollisionSystem::groundGap), nil if nothing is within maxGap. */
static int groundGap(lua_State* pLua_)
{
	PoolId id = checkEntity(pLua_, 1);
	float gap;
	if(Globals::Physics::collisionSystem.groundGap(id, static_cast<float>(luaL_checknumber(pLua_, 2)), gap)) lua_pushnumber(pLua_, gap);
	else lua_pushnil(pLua_);
	return 1;
}

static int touchingWall(lua_State* pLua_)
{
	lua_pushboolean(pLua_, checkBody(pLua_, 1).touchingWall);
	return 1;
}

static int wallNormal(lua_State* pLua_)
{
	return pushVector(pLua_, checkBody(pLua_, 1).wallNormal);
}

/* ---- behaviours ---- */

/*
Reads the fields table of sf.behavior (the table at index tableIndex_): {name = {type = "number" | "bool" | "string" | "vec3" | "entity" | "prefab", default = value, external = bool}, ...}. The default of a vec3 is {x, y, z} (or x =, y =, z =);
an entity or prefab has none. The fields are sorted by name (a Lua table has no order), which is the order the inspector lists them in.
*/
static std::vector<ExternalField> readFields(lua_State* pLua_, int tableIndex_)
{
	static const std::unordered_map<std::string, ExternalType> types = {{"number", ExternalType::Number}, {"bool", ExternalType::Bool}, {"string", ExternalType::String}, {"vec3", ExternalType::Vec3}, {"entity", ExternalType::Entity}, {"prefab", ExternalType::Prefab}};
	std::vector<ExternalField> fields;
	tableIndex_ = lua_absindex(pLua_, tableIndex_);
	lua_pushnil(pLua_);
	while(lua_next(pLua_, tableIndex_) != 0)
	{
		if(lua_type(pLua_, -2) != LUA_TSTRING || !lua_istable(pLua_, -1)) luaL_error(pLua_, "sf.behavior: the fields are {name = {type = ..., default = ..., external = ...}}.");
		ExternalField field;
		field.name = lua_tostring(pLua_, -2);
		if(lua_getfield(pLua_, -1, "type") != LUA_TSTRING) luaL_error(pLua_, "sf.behavior: the field '%s' needs a type.", field.name.c_str());
		auto type = types.find(lua_tostring(pLua_, -1));
		if(type == types.end()) luaL_error(pLua_, "sf.behavior: the field '%s' has the unknown type '%s' (number, bool, string, vec3, entity or prefab).", field.name.c_str(), lua_tostring(pLua_, -1));
		field.type = type->second;
		lua_pop(pLua_, 1);
		if(lua_getfield(pLua_, -1, "external") != LUA_TNIL) field.external = lua_toboolean(pLua_, -1) != 0;
		lua_pop(pLua_, 1);

		if(lua_getfield(pLua_, -1, "default") != LUA_TNIL)
		{
			switch(field.type)
			{
			case ExternalType::Number:
				field.defaultValue.number = luaL_checknumber(pLua_, -1);
				break;
			case ExternalType::Bool:
				field.defaultValue.flag = lua_toboolean(pLua_, -1) != 0;
				break;
			case ExternalType::String:
				field.defaultValue.text = luaL_checkstring(pLua_, -1);
				break;
			case ExternalType::Vec3:
			{
				luaL_checktype(pLua_, -1, LUA_TTABLE);
				const char* pAxes[3] = {"x", "y", "z"};
				float components[3];
				for(int i = 0; i < 3; i++)
				{
					if(lua_getfield(pLua_, -1, pAxes[i]) == LUA_TNIL)
					{
						lua_pop(pLua_, 1);
						lua_geti(pLua_, -1, i + 1);
					}
					components[i] = static_cast<float>(luaL_checknumber(pLua_, -1));
					lua_pop(pLua_, 1);
				}
				field.defaultValue.vector = glm::vec3(components[0], components[1], components[2]);
				break;
			}
			default:
				luaL_error(pLua_, "sf.behavior: the field '%s' is an entity or prefab and has no default.", field.name.c_str());
			}
		}
		lua_pop(pLua_, 2);
		fields.push_back(field);
	}
	std::sort(fields.begin(), fields.end(), [](const ExternalField& a_, const ExternalField& b_) { return a_.name < b_.name; });
	return fields;
}

/* Registers a behaviour under a name: fn(entity) is called once a tick for every entity that was given it. The same name again replaces the function. The optional third argument declares its fields (readFields): each is written into the entity's sf.state before the first call. */
static int behavior(lua_State* pLua_)
{
	const char* pName = luaL_checkstring(pLua_, 1);
	luaL_checktype(pLua_, 2, LUA_TFUNCTION);
	std::vector<ExternalField> fields;
	if(!lua_isnoneornil(pLua_, 3))
	{
		luaL_checktype(pLua_, 3, LUA_TTABLE);
		fields = readFields(pLua_, 3);
	}
	lua_pushvalue(pLua_, 2);
	int reference = luaL_ref(pLua_, LUA_REGISTRYINDEX);

	/* The file the calling chunk was loaded from (luaL_loadfile names a chunk "@<path>"), kept for the editor; empty if the caller is not a file. */
	std::string sourceFile;
	lua_Debug caller;
	if(lua_getstack(pLua_, 1, &caller) && lua_getinfo(pLua_, "S", &caller) && caller.source != nullptr && caller.source[0] == '@') sourceFile = caller.source + 1;

	auto found = Globals::Game::entityBehaviorTypes.nameToId.find(pName);
	if(found == Globals::Game::entityBehaviorTypes.nameToId.end())
	{
		PoolId id = addEntityBehavior(pName, nullptr);
		Globals::Game::entityBehaviorTypes[id].scriptFunction = reference;
		Globals::Game::entityBehaviorTypes[id].sourceFile = sourceFile;
		Globals::Game::entityBehaviorTypes[id].fields = fields;
	}
	else
	{
		EntityBehavior& rBehavior = Globals::Game::entityBehaviorTypes[found->second];
		if(rBehavior.scriptFunction != 0) luaL_unref(pLua_, LUA_REGISTRYINDEX, rBehavior.scriptFunction);
		rBehavior.scriptFunction = reference;
		rBehavior.sourceFile = sourceFile;
		rBehavior.fields = fields;
	}
	return 0;
}

/* Registers fn(entity, placement) as the setup of a prefab (Scripting::setupPrefab), by the prefab's name ("SADX/SPRING"); the same name again replaces the function. */
static int prefabBehavior(lua_State* pLua_)
{
	const char* pName = luaL_checkstring(pLua_, 1);
	luaL_checktype(pLua_, 2, LUA_TFUNCTION);
	lua_pushvalue(pLua_, 2);
	registerPrefabBehavior(pName, luaL_ref(pLua_, LUA_REGISTRYINDEX));
	return 0;
}

static int give(lua_State* pLua_)
{
	PoolId entityId = checkEntity(pLua_, 1);
	const char* pName = luaL_checkstring(pLua_, 2);
	auto found = Globals::Game::entityBehaviorTypes.nameToId.find(pName);
	if(found == Globals::Game::entityBehaviorTypes.nameToId.end()) return luaL_error(pLua_, "there is no behaviour named '%s'", pName);
	giveEntityBehavior(entityId, found->second);
	return 0;
}

/* ---- collision ---- */

/* The file the calling chunk was loaded from, as in behavior; empty if it was not loaded from one. */
static std::string callerSourceFile(lua_State* pLua_)
{
	lua_Debug caller;
	if(lua_getstack(pLua_, 1, &caller) && lua_getinfo(pLua_, "S", &caller) && caller.source != nullptr && caller.source[0] == '@') return caller.source + 1;
	return "";
}

/* Registers the function at index_ under the name (Globals::Game::boundAreaFunctions); a name that is registered keeps its registry reference and gets the new function behind it. Returns the reference. */
static int registerBoundAreaFunction(lua_State* pLua_, const std::string& rName_, int index_, const std::string& rSourceFile_)
{
	auto& rFunctions = Globals::Game::boundAreaFunctions;
	auto found = rFunctions.nameToId.find(rName_);
	lua_pushvalue(pLua_, index_);
	if(found == rFunctions.nameToId.end())
	{
		BoundAreaFunction function;
		function.scriptFunction = luaL_ref(pLua_, LUA_REGISTRYINDEX);
		function.sourceFile = rSourceFile_;
		rFunctions.add(rName_.c_str(), function);
		return function.scriptFunction;
	}
	BoundAreaFunction& rFunction = rFunctions[found->second];
	lua_rawseti(pLua_, LUA_REGISTRYINDEX, rFunction.scriptFunction);
	rFunction.sourceFile = rSourceFile_;
	return rFunction.scriptFunction;
}

/*
Registers fn(parent, contact) under a name as an onTouch function of a bound area (BoundAreaFunction): the editor lists it and a bound area's rules name it. The same name again replaces the function behind the
same registry reference. Returns the function, so it can also be used directly in the interactions of sf.collisionBehavior (which then names it by this name).
*/
static int onTouch(lua_State* pLua_)
{
	const char* pName = luaL_checkstring(pLua_, 1);
	luaL_checktype(pLua_, 2, LUA_TFUNCTION);
	registerBoundAreaFunction(pLua_, pName, 2, callerSourceFile(pLua_));
	lua_pushvalue(pLua_, 2);
	return 1;
}

/* The tag of that name; made if there is none (the built-in ones, Solid, Pickup, Hazard, Death and Body, are there already). */
static int tag(lua_State* pLua_)
{
	const char* pName = luaL_checkstring(pLua_, 1);
	PoolId id = Globals::Game::baBehaviors.findTag(pName);
	if(id == UninitializedPoolId)
	{
		id = Globals::Game::baBehaviors.addTag(BATagKey{pName});
		Globals::Game::baBehaviors.boundAreas.fixedTags.push_back(pName);
	}
	lua_pushinteger(pLua_, toHandle(id));
	return 1;
}

/*
Makes a collision behaviour (BABehavior.h) from {tags = {tag, ...}, collidesWith = {tag, ...}, interactions = {[tag] = fn(parent, contact) or {fn, fn, ...}, ...}}; each part may be left out. Returns its handle. An interaction is
called with the entity that owns the collision (parent) and the one it touched (contact), once per tick and pair.
The behaviour's rules are also kept by name (BoundAreaData::scriptBehaviors) so the editor can show and customise it: a function that sf.onTouch registered is named by that name, any other is registered as
"<behaviour>/<tag>" (with "#2", "#3" ... for a second one of the tag).
*/
static int collisionBehavior(lua_State* pLua_)
{
	const char* pName = luaL_checkstring(pLua_, 1);
	luaL_checktype(pLua_, 2, LUA_TTABLE);
	BABehavior behavior;
	BoundAreaBehaviorData data;
	data.name = pName;
	const auto& rTagNames = Globals::Game::baTags.names;

	if(lua_getfield(pLua_, 2, "tags") == LUA_TTABLE)
	{
		for(lua_Integer i = 1; lua_rawgeti(pLua_, -1, i) != LUA_TNIL; i++)
		{
			behavior.tags.push_back(fromHandle(luaL_checkinteger(pLua_, -1)));
			data.tags.push_back(rTagNames[behavior.tags.back().id]);
			lua_pop(pLua_, 1);
		}
		lua_pop(pLua_, 1);
	}
	lua_pop(pLua_, 1);

	if(lua_getfield(pLua_, 2, "collidesWith") == LUA_TTABLE)
	{
		for(lua_Integer i = 1; lua_rawgeti(pLua_, -1, i) != LUA_TNIL; i++)
		{
			PoolId tagId = fromHandle(luaL_checkinteger(pLua_, -1));
			behavior.availableCollisions.insert(tagId.id);
			data.collidesWith.push_back(rTagNames[tagId.id]);
			lua_pop(pLua_, 1);
		}
		lua_pop(pLua_, 1);
	}
	lua_pop(pLua_, 1);

	/* The function at index_, named and registered if it was not, added to the behaviour for the tag. */
	std::string sourceFile = callerSourceFile(pLua_);
	auto interact = [&](uint32_t tagId_, int index_)
	{
		auto& rFunctions = Globals::Game::boundAreaFunctions;
		index_ = lua_absindex(pLua_, index_);
		std::string name;
		for(uint32_t f = 0; f < rFunctions.objects.size() && name.empty(); f++)
		{
			if(!rFunctions.alive[f]) continue;
			lua_rawgeti(pLua_, LUA_REGISTRYINDEX, rFunctions.objects[f].scriptFunction);
			if(lua_rawequal(pLua_, -1, index_)) name = rFunctions.names[f];
			lua_pop(pLua_, 1);
		}
		if(name.empty())
		{
			name = std::format("{}/{}", pName, rTagNames[tagId_]);
			for(int n = 2; rFunctions.nameToId.contains(name); n++) name = std::format("{}/{}#{}", pName, rTagNames[tagId_], n);
			registerBoundAreaFunction(pLua_, name, index_, sourceFile);
		}
		behavior.availableInteractions[tagId_].push_back(BAInteraction::script(rFunctions[rFunctions.nameToId.at(name)].scriptFunction));

		auto rule = std::find_if(data.onTouch.begin(), data.onTouch.end(), [&](const BoundAreaRule& rRule_) { return rRule_.function == name; });
		if(rule == data.onTouch.end()) rule = data.onTouch.insert(data.onTouch.end(), BoundAreaRule{name, {}});
		if(std::find(rule->tags.begin(), rule->tags.end(), rTagNames[tagId_]) == rule->tags.end()) rule->tags.push_back(rTagNames[tagId_]);
	};

	if(lua_getfield(pLua_, 2, "interactions") == LUA_TTABLE)
	{
		lua_pushnil(pLua_);
		while(lua_next(pLua_, -2) != 0)
		{
			uint32_t tagId = fromHandle(luaL_checkinteger(pLua_, -2)).id;
			/* A function, or a list of functions when a tag has several. */
			if(lua_type(pLua_, -1) == LUA_TFUNCTION)
			{
				interact(tagId, -1);
			}
			else
			{
				luaL_checktype(pLua_, -1, LUA_TTABLE);
				for(lua_Integer i = 1; lua_rawgeti(pLua_, -1, i) != LUA_TNIL; i++)
				{
					luaL_checktype(pLua_, -1, LUA_TFUNCTION);
					interact(tagId, -1);
					lua_pop(pLua_, 1);
				}
				lua_pop(pLua_, 1);
			}
			lua_pop(pLua_, 1);
		}
	}
	lua_pop(pLua_, 1);

	PoolId id = Globals::Game::baBehaviors.add(BABehaviorKey{pName}, behavior);
	Globals::Game::baBehaviors.boundAreas.scriptBehaviors.push_back(std::move(data));
	lua_pushinteger(pLua_, toHandle(id));
	return 1;
}

/* Gives every shape of the entity's collision the collision behaviour. */
static int setShapeBehavior(lua_State* pLua_)
{
	const Entity& rEntity = Globals::Game::entities[checkEntity(pLua_, 1)];
	PoolId behaviorId = fromHandle(luaL_checkinteger(pLua_, 2));
	if(rEntity.collisionId == UninitializedPoolId) return 0;
	for(Physics::CollisionShape& rShape : Globals::Physics::collisions[rEntity.collisionId].shapes)
	{
		rShape.behavior = behaviorId;
	}
	return 0;
}

/* ---- setup ---- */

static PoolId shaderByName(const char* name_)
{
	return Graphics::shaders.nameToId.at(name_);
}

/*
Makes a level from Assets/Levels/<name>/<name>.json (Loading::loadLevel, see LevelLoader.h) and returns it. Load one level at a time: Emerald Coast and Westopolis would overlap at similar coordinates. The player's start
position is the script's to choose (the real start of a stage is not known).
*/
static int loadLevel(lua_State* pLua_)
{
	lua_pushinteger(pLua_, toHandle(Loading::loadLevel(luaL_checkstring(pLua_, 1))));
	return 1;
}

/* The levels unloadLevel was asked to remove; processPendingUnloads (Scripting.h) removes them. */
static std::vector<PoolId> pendingUnloads;

/*
Asks for a level to be removed, with its collision and the entities it placed. It happens at the start of the next gameplay tick, not now: the script is usually running inside a tick, and the physics
and collision systems have already read which entities are active. A script that loads another level right after waits one tick (sf.levelExists says when the old one is gone).
*/
static int unloadLevel(lua_State* pLua_)
{
	PoolId id = fromHandle(luaL_checkinteger(pLua_, 1));
	Globals::Game::levels.isPoolIdValid(id);
	pendingUnloads.push_back(id);
	return 0;
}

static int levelExists(lua_State* pLua_)
{
	PoolId id = fromHandle(luaL_checkinteger(pLua_, 1));
	lua_pushboolean(pLua_, id.id < Globals::Game::levels.objects.size() && Globals::Game::levels.alive[id.id] && Globals::Game::levels.generation[id.id] == id.generation);
	return 1;
}

/* Every entity spawnPlayer and spawn made that still exists (Scripting::spawnedEntities). */
static std::vector<PoolId> spawnedList;

/* Makes a player entity from the character's prefab (spawnPrefab: model, idle motions, hand-made collision shape and physics of weight 1) and returns it; the script attaches it (attachPlayer). */
static int spawnPlayer(lua_State* pLua_)
{
	std::string character = luaL_checkstring(pLua_, 1);
	SpawnParams params;
	params.position = checkVector(pLua_, 2);
	PoolId id;
	if(character == "sonic")
	{
		params.name = "SASonic";
		params.what = "(player)";
		id = spawnPrefab(findPrefab(SA_NINJA, "Sonic"), shaderByName("SA"), params);
	}
	else if(character == "shadow")
	{
		params.name = "Shadow";
		params.what = "(player)";
		id = spawnPrefab(findPrefab(STH2005_ONE, "Shadow"), shaderByName("STH2005"), params);
	}
	else return luaL_error(pLua_, "unknown character '%s'", character.c_str());
	if(id == UninitializedPoolId) return luaL_error(pLua_, "could not spawn '%s'", character.c_str());
	std::erase_if(spawnedList, [](PoolId old_) { return !entityExists(old_); });
	spawnedList.push_back(id);
	lua_pushinteger(pLua_, toHandle(id));
	return 1;
}

/* spawn(prefabName, x, y, z): an entity made from the prefab of that name (Globals::Game::prefabs, e.g. a prefab field of a behaviour), with the shader its game uses as the editor's Prefabs tab does; returns it. */
static int spawn(lua_State* pLua_)
{
	const char* pName = luaL_checkstring(pLua_, 1);
	auto found = Globals::Game::prefabs.nameToId.find(pName);
	if(found == Globals::Game::prefabs.nameToId.end()) return luaL_error(pLua_, "there is no prefab named '%s'", pName);
	SpawnParams params;
	params.position = checkVector(pLua_, 2);
	params.what = "(script)";
	PoolId id = spawnPrefab(found->second, shaderByName(Globals::Game::prefabs[found->second].game == SA_NINJA ? "SA" : "STH2005"), params);
	if(id == UninitializedPoolId) return luaL_error(pLua_, "could not spawn '%s'", pName);
	std::erase_if(spawnedList, [](PoolId old_) { return !entityExists(old_); });
	spawnedList.push_back(id);
	lua_pushinteger(pLua_, toHandle(id));
	return 1;
}

static int setCamera(lua_State* pLua_)
{
	setCameraPosition(checkVector(pLua_, 1));
	return 0;
}

/* ---- scripts ---- */

static int include(lua_State* pLua_)
{
	const char* pPath = luaL_checkstring(pLua_, 1);

	/* A script of Assets/External/Scripts includes relative to that folder (its chunk is named "@<path>"), any other relative to Assets/Scripts. */
	const char* pRoot = ScriptsRoot;
	lua_Debug caller;
	if(lua_getstack(pLua_, 1, &caller) && lua_getinfo(pLua_, "S", &caller) && caller.source != nullptr && std::string(caller.source).starts_with(std::string("@") + ExternalScriptsRoot)) pRoot = ExternalScriptsRoot;
	if(!loadFile(pPath, pRoot)) return luaL_error(pLua_, "could not run script '%s'", pPath);
	return 0;
}

/* A vector turned about z, then x, then y by angles in BAMS (65536 = a turn), the order the games' objects use (the matrix is Rz * Rx * Ry, as for the dash panel). */
static int rotateBamsZXY(lua_State* pLua_)
{
	constexpr float BamsToRadians = glm::two_pi<float>() / 65536.0f;
	glm::vec3 vector = checkVector(pLua_, 1);
	float z = static_cast<float>(luaL_checknumber(pLua_, 4)) * BamsToRadians;
	float x = static_cast<float>(luaL_checknumber(pLua_, 5)) * BamsToRadians;
	float y = static_cast<float>(luaL_checknumber(pLua_, 6)) * BamsToRadians;
	glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), z, glm::vec3(0.0f, 0.0f, 1.0f));
	rotation = glm::rotate(rotation, x, glm::vec3(1.0f, 0.0f, 0.0f));
	rotation = glm::rotate(rotation, y, glm::vec3(0.0f, 1.0f, 0.0f));
	return pushVector(pLua_, glm::vec3(rotation * glm::vec4(vector, 0.0f)));
}

const std::vector<PoolId>& spawnedEntities()
{
	return spawnedList;
}

void processPendingUnloads()
{
	if(pendingUnloads.empty()) return;
	for(PoolId id : pendingUnloads)
	{
		if(Globals::Game::levels.alive[id.id] && Globals::Game::levels.generation[id.id] == id.generation) Globals::Game::levels.remove(id);
	}
	pendingUnloads.clear();
	Globals::Game::spatialIndex.rebuild();
}

void registerAPI(lua_State* pLua_)
{
	const luaL_Reg functions[] = {
		{"tickRate", guarded<tickRate>}, {"tickTime", guarded<tickTime>}, {"rings", guarded<rings>}, {"setRings", guarded<setRings>},
		{"debugMode", guarded<debugMode>}, {"key", guarded<key>}, {"cameraForward", guarded<cameraForward>}, {"followCamera", guarded<followCamera>}, {"animate", guarded<animate>}, {"blink", guarded<blink>}, {"pose", guarded<pose>}, {"setAnimationScaleMode", guarded<setAnimationScaleMode>}, {"setAnimationEulerOrder", guarded<setAnimationEulerOrder>}, {"motionRange", guarded<motionRange>}, {"bone", guarded<bone>}, {"lowestPoint", guarded<lowestPoint>},
		{"exists", guarded<exists>}, {"name", guarded<name>}, {"position", guarded<position>}, {"setPosition", guarded<setPosition>},
		{"moveTo", guarded<moveTo>}, {"turnY", guarded<turnY>}, {"orient", guarded<orient>}, {"destroy", guarded<destroy>}, {"state", guarded<state>}, {"axis", guarded<axis>},
		{"setUncullable", guarded<setUncullable>}, {"postEvent", guarded<postEvent>}, {"setDashTrail", guarded<setDashTrail>}, {"setEffectBall", guarded<setEffectBall>},
		{"hasBody", guarded<hasBody>}, {"velocity", guarded<velocity>}, {"setVelocity", guarded<setVelocity>}, {"grounded", guarded<grounded>}, {"setGroundSnap", guarded<setGroundSnap>}, {"setWeight", guarded<setWeight>}, {"setGroundSnapMode", guarded<setGroundSnapMode>}, {"setMeanVelocityMove", guarded<setMeanVelocityMove>},
		{"groundNormal", guarded<groundNormal>}, {"groundSurface", guarded<groundSurface>}, {"groundGap", guarded<groundGap>}, {"touchingWall", guarded<touchingWall>}, {"wallNormal", guarded<wallNormal>},
		{"behavior", guarded<behavior>}, {"give", guarded<give>}, {"prefabBehavior", guarded<prefabBehavior>},
		{"onTouch", guarded<onTouch>}, {"tag", guarded<tag>}, {"collisionBehavior", guarded<collisionBehavior>}, {"setShapeBehavior", guarded<setShapeBehavior>},
		{"loadLevel", guarded<loadLevel>}, {"unloadLevel", guarded<unloadLevel>}, {"levelExists", guarded<levelExists>}, {"spawnPlayer", guarded<spawnPlayer>}, {"spawn", guarded<spawn>},{"setCamera", guarded<setCamera>},
		{"include", guarded<include>}, {"rotateBamsZXY", guarded<rotateBamsZXY>},
		{nullptr, nullptr}
	};
	luaL_newlib(pLua_, functions);
	lua_pushinteger(pLua_, toHandle(UninitializedPoolId));
	lua_setfield(pLua_, -2, "none");
	lua_setglobal(pLua_, "sf");
}
}
