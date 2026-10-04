#include "SADXGameplay.h"
#include "API.h"
#include "Globals.h"
#include "SASet.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>

namespace SADXGameplay
{
/*
Placeholders: numbers that are not decoded from the game, only made up so that the behaviours below can be tried. Speeds are in units per second, times in gameplay ticks.
*/
constexpr float PlayerMoveSpeed = 150.0f;
constexpr float PlayerJumpSpeed = 60.0f;
constexpr float SpringSpeed = 800.0f;
constexpr float SpringCooldownTicks = 30.0f;
constexpr float SpringControlLockTicks = 20.0f;
constexpr float EnemyBounceSpeed = 40.0f;
constexpr float EnemyKnockbackSpeed = 90.0f;
constexpr float InvulnerableTicks = 90.0f;

/*
Decoded (external, the pseudocode of the dash panel, _Main.rel 0x8049B8CC, and of the function it calls to launch the player, 0x804C9DF8; the debug text of the object names the fields): the record's scale x
is the speed and its scale y the time the player's control is disabled for (in frames), each replaced by a default (14 and 60: the constants at 0x8065BC8C / 0x8065BC90) when it is 0 or lower. The launch
sets the player's speed to the vector (speed, 0, 0) turned about z by the record's z angle, about x by its x angle and about y by its y angle minus 0x4000, in that order (the Ninja rotate calls). The
panel does not launch again until its countdown (the disable time, counted down each frame) is over. Hypotheses: a speed is units per frame at 60 frames a second, a frame is a tick, and our rotation
convention is the game's.
*/
constexpr float DashPanelDefaultSpeed = 14.0f;
constexpr float DashPanelDefaultDisableTicks = 60.0f;

static PoolId tagRing;
static PoolId tagSpring;
static PoolId tagDashPanel;
static PoolId tagEnemy;
static PoolId behaviorRing;
static PoolId behaviorSpring;
static PoolId behaviorDashPanel;
static PoolId behaviorEnemy;
static PoolId entityBehaviorCooldown;
static PoolId entityBehaviorPlayer;

static void setShapeBehavior(PoolId entityId_, PoolId behaviorId_)
{
	Entity& rEntity = Globals::Game::entities[entityId_];
	if(rEntity.collisionId == UninitializedPoolId) return;
	for(Physics::CollisionShape& rShape : Globals::Physics::collisions[rEntity.collisionId].shapes)
	{
		rShape.behavior = behaviorId_;
	}
}

/* A body the behaviours below act on: an entity with a physics (the player). */
static bool isMover(PoolId entityId_)
{
	return entityId_ != UninitializedPoolId && entityExists(entityId_) && Globals::Game::entities[entityId_].physicsId != UninitializedPoolId;
}

/*
The ring (placeholder: not decoded, only what the game is known for): touched by the player it is counted and removed. EntityState::phase is 1 once collected, since contacts are tested every frame and the
removal happens at the start of the next tick.
*/
static void ringTouched(PoolId ringId_, PoolId playerId_)
{
	if(!entityExists(ringId_) || !isMover(playerId_)) return;
	EntityState& rState = entityStateOf(ringId_);
	if(rState.phase != 0) return;
	rState.phase = 1;
	Globals::Game::rings++;
	postGameEvent("RingCollected", ringId_, Globals::Game::entities[ringId_].position);
	Globals::Game::entities[ringId_].markForDestroy();
}

/* The panel's EntityState: value = speed (units per frame), vector = the direction (unit), phase = the disable time (ticks), timer = the countdown to its next launch (ticks). The player's timer gets the disable time. */
static void dashPanelTouched(PoolId panelId_, PoolId playerId_)
{
	if(!entityExists(panelId_) || !isMover(playerId_)) return;
	EntityState& rPanel = entityStateOf(panelId_);
	if(rPanel.timer > 0.0f) return;
	Globals::Physics::bodies[Globals::Game::entities[playerId_].physicsId].velocity = rPanel.vector * rPanel.value * Globals::Game::gameplaySystem.tickRate;
	entityStateOf(playerId_).timer = rPanel.phase;
	rPanel.timer = static_cast<float>(rPanel.phase);
	postGameEvent("DashPanelLaunch", panelId_, Globals::Game::entities[panelId_].position);
}

/* The spring (placeholder: not decoded): launches the player along its up axis (EntityState::vector) at SpringSpeed and locks the control for a moment. */
static void springTouched(PoolId springId_, PoolId playerId_)
{
	if(!entityExists(springId_) || !isMover(playerId_)) return;
	EntityState& rSpring = entityStateOf(springId_);
	if(rSpring.timer > 0.0f) return;
	Globals::Physics::bodies[Globals::Game::entities[playerId_].physicsId].velocity = rSpring.vector * SpringSpeed;
	entityStateOf(playerId_).timer = SpringControlLockTicks;
	rSpring.timer = SpringCooldownTicks;
	postGameEvent("SpringLaunch", springId_, Globals::Game::entities[springId_].position);
}

/*
Enemies (placeholder: not decoded, a simplified idea of the game's rules): touched by a player that is in the air they are destroyed and the player bounces; touched on the ground the player loses its rings
and is knocked back, and cannot be hurt again for InvulnerableTicks. The player's EntityState: value = the invulnerable ticks left.
*/
static void enemyTouched(PoolId enemyId_, PoolId playerId_)
{
	if(!entityExists(enemyId_) || !isMover(playerId_)) return;
	EntityState& rEnemy = entityStateOf(enemyId_);
	if(rEnemy.phase != 0) return;
	Entity& rPlayer = Globals::Game::entities[playerId_];
	Physics::PhysicsBody& rBody = Globals::Physics::bodies[rPlayer.physicsId];
	if(std::abs(rBody.velocity.y) > 0.5f)
	{
		rEnemy.phase = 1;
		postGameEvent("EnemyDefeated", enemyId_, Globals::Game::entities[enemyId_].position);
		Globals::Game::entities[enemyId_].markForDestroy();
		rBody.velocity.y = EnemyBounceSpeed;
		return;
	}
	EntityState& rPlayerState = entityStateOf(playerId_);
	if(rPlayerState.value > 0.0f) return;
	Globals::Game::rings = 0;
	postGameEvent("PlayerHurt", playerId_, rPlayer.position);
	rPlayerState.value = InvulnerableTicks;
	glm::vec3 away = rPlayer.position - Globals::Game::entities[enemyId_].position;
	away.y = 0.0f;
	away = glm::length(away) > 1e-4f ? glm::normalize(away) : glm::vec3(0.0f, 0.0f, 1.0f);
	rBody.velocity = away * EnemyKnockbackSpeed + glm::vec3(0.0f, 30.0f, 0.0f);
}

/* Counts the entity's timer down to 0 every tick (the dash panel's and the spring's launch cooldowns). */
static void cooldownBehave(PoolId entityId_)
{
	EntityState& rState = entityStateOf(entityId_);
	if(rState.timer > 0.0f) rState.timer -= 1.0f;
}

/*
The player (placeholder controller, not Sonic's physics): outside the debug mode (Globals::App::debugMode, Tab) the keyboard, W A S D, moves the entity over the ground relative to where the camera looks
(the speed is set outright, and zeroed on the ground when no key is held), Space jumps from the ground (the vertical speed is about zero then; gravity and the collisions are the physics system's). EntityState: timer = the ticks the control is locked for (a launch), value = the
invulnerable ticks left (enemies); both count down here.
*/
static void playerBehave(PoolId entityId_)
{
	Entity& rEntity = Globals::Game::entities[entityId_];
	if(rEntity.physicsId == UninitializedPoolId) return;
	EntityState& rState = entityStateOf(entityId_);
	if(rState.value > 0.0f) rState.value -= 1.0f;
	if(rState.timer > 0.0f)
	{
		rState.timer -= 1.0f;
		return;
	}

	GLFWwindow* pWindow = Globals::App::window;
	const Camera& rCamera = Globals::Game::cameras[0];
	glm::vec3 forward(rCamera.front.x, 0.0f, rCamera.front.z);
	forward = glm::length(forward) > 1e-4f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::vec3 wish(0.0f);
	bool controlled = !Globals::App::debugMode;
	if(controlled && glfwGetKey(pWindow, GLFW_KEY_W) == GLFW_PRESS) wish += forward;
	if(controlled && glfwGetKey(pWindow, GLFW_KEY_S) == GLFW_PRESS) wish -= forward;
	if(controlled && glfwGetKey(pWindow, GLFW_KEY_D) == GLFW_PRESS) wish += right;
	if(controlled && glfwGetKey(pWindow, GLFW_KEY_A) == GLFW_PRESS) wish -= right;

	Physics::PhysicsBody& rBody = Globals::Physics::bodies[rEntity.physicsId];
	bool grounded = std::abs(rBody.velocity.y) < 0.5f;
	if(glm::length(wish) > 1e-4f)
	{
		wish = glm::normalize(wish) * PlayerMoveSpeed;
		rBody.velocity.x = wish.x;
		rBody.velocity.z = wish.z;
	}
	else if(grounded)
	{
		rBody.velocity.x = 0.0f;
		rBody.velocity.z = 0.0f;
	}
	if(controlled && grounded && glfwGetKey(pWindow, GLFW_KEY_SPACE) == GLFW_PRESS) rBody.velocity.y = PlayerJumpSpeed;
}

void registerAll()
{
	BABehaviors& rBehaviors = Globals::Game::baBehaviors;
	tagRing = addBATag(BATagKey{"Ring"});
	tagSpring = addBATag(BATagKey{"Spring"});
	tagDashPanel = addBATag(BATagKey{"DashPanel"});
	tagEnemy = addBATag(BATagKey{"Enemy"});

	/* Each object's collision behaviour has its own tags and acts on a Body that touches it. */
	behaviorRing = addBehavior(BABehaviorKey{"Ring"}, BABehavior{{rBehaviors.tagPickup, tagRing}, {}, {{rBehaviors.tagBody.id, &ringTouched}}});
	behaviorSpring = addBehavior(BABehaviorKey{"Spring"}, BABehavior{{tagSpring}, {}, {{rBehaviors.tagBody.id, &springTouched}}});
	behaviorDashPanel = addBehavior(BABehaviorKey{"DashPanel"}, BABehavior{{tagDashPanel}, {}, {{rBehaviors.tagBody.id, &dashPanelTouched}}});
	behaviorEnemy = addBehavior(BABehaviorKey{"Enemy"}, BABehavior{{rBehaviors.tagHazard, tagEnemy}, {}, {{rBehaviors.tagBody.id, &enemyTouched}}});

	entityBehaviorCooldown = addEntityBehavior("Cooldown", &cooldownBehave);
	entityBehaviorPlayer = addEntityBehavior("Player", &playerBehave);
}

/* Angles in BAMS (65536 = a full turn) as radians. */
static float bamsToRadians(int bams_)
{
	return static_cast<float>(bams_) * glm::two_pi<float>() / 65536.0f;
}

void setupObject(PoolId entityId_, const char* name_, const Ninja::SAPlacement* pPlacement_)
{
	if(behaviorRing == UninitializedPoolId || tagRing == UninitializedPoolId) return;
	if(std::strcmp(name_, "RING") == 0)
	{
		setShapeBehavior(entityId_, behaviorRing);
	}
	else if(std::strcmp(name_, "SPRING") == 0 || std::strcmp(name_, "SPRINGB") == 0)
	{
		setShapeBehavior(entityId_, behaviorSpring);
		EntityState& rState = entityStateOf(entityId_);
		rState.vector = glm::normalize(glm::vec3(Globals::Game::entities[entityId_].rotation * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
		giveEntityBehavior(entityId_, entityBehaviorCooldown);
	}
	else if(std::strcmp(name_, "O AXPNL") == 0 && pPlacement_ != nullptr)
	{
		setShapeBehavior(entityId_, behaviorDashPanel);
		EntityState& rState = entityStateOf(entityId_);
		rState.value = pPlacement_->scale.x > 0.0f ? pPlacement_->scale.x : DashPanelDefaultSpeed;
		rState.phase = static_cast<int>(pPlacement_->scale.y > 0.0f ? pPlacement_->scale.y : DashPanelDefaultDisableTicks);
		glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), bamsToRadians(pPlacement_->rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		rotation = glm::rotate(rotation, bamsToRadians(pPlacement_->rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		rotation = glm::rotate(rotation, bamsToRadians(pPlacement_->rotation.y - 0x4000), glm::vec3(0.0f, 1.0f, 0.0f));
		rState.vector = glm::normalize(glm::vec3(rotation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
		giveEntityBehavior(entityId_, entityBehaviorCooldown);
	}
	else if(std::strcmp(name_, "E SARU") == 0 || std::strcmp(name_, "E SAITO") == 0 || std::strcmp(name_, "E AMEMB") == 0)
	{
		setShapeBehavior(entityId_, behaviorEnemy);
	}
}

void attachPlayer(PoolId entityId_)
{
	entityStateOf(entityId_);
	giveEntityBehavior(entityId_, entityBehaviorPlayer);
	getEntity(entityId_).behaviorNotCulled = true;
}
}
