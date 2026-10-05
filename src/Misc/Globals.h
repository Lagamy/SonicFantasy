#pragma once 
#include <cstdint> 
#include <string> 
#include <glad/glad.h>
#include <vector> 
#include "GLFW/glfw3.h"
#include "Camera.h"
#include "Entity.h"
#include "Level.h"
#include "BABehavior.h"
#include "BABehaviors.h"
#include "EntityBehavior.h"
#include "GameplaySystem.h"
#include "SpatialIndex.h"
#include "EntityState.h"
#include "GameEvents.h"
#include "GCDisc.h"
#include "Pool.h"
#include "PoolMap.h"
#include "Collision.h"
#include "QuadTree.h"
#include "PhysicsBody.h"
#include "PhysicsSystem.h"
#include "CollisionSystem.h"

namespace Globals {
	namespace App { 
		inline uint32_t windowWidth = 1920; 
		inline uint32_t windowHeight = 1080; 
		inline GLFWwindow* window;

		/* Debug mode (Tab, see main.cpp): shows the debug UI and the entity labels (Graphics::Text); Esc frees the cursor only while it is on. */
		inline bool debugMode = false;
	
		inline float deltaTime = 0.0f;
		inline float lastFrame = 0.0f;

		/* Frames per second, averaged over the last half second (main.cpp), and whether the buffer swap waits for the display (vsync; off by default, so the frame rate is the machine's; changed with glfwSwapInterval, see the Game tab). */
		inline float fps = 0.0f;
		inline bool vsync = false;
	}

	namespace Game { 
		inline std::vector<Camera> cameras;
		inline PoolMap<Entity> entities = ("Entities"); 
		inline PoolMap<Level> levels = ("Levels"); 

		/* Where the entities are, for culling (SpatialIndex.h); rebuilt by main.cpp once the level's entities exist. */
		inline SpatialIndex spatialIndex;
		inline PoolMap<BATag> baTags = ("BATags");
		inline PoolMap<BABehavior> behaviors = ("Behaviors");
		inline BABehaviors baBehaviors;

		/*
		Entity behaviours (EntityBehavior.h): the kinds, by name, the stack each entity that has any has (one record per such entity) and the system that runs them every gameplay tick.
		*/
		inline PoolMap<EntityBehavior> entityBehaviorTypes = ("EntityBehaviorTypes");
		inline Pool<EntityBehaviorSet> entityBehaviors = ("EntityBehaviors");
		inline GameplaySystem gameplaySystem;

		/* What behaviours keep per entity (EntityState.h), and the ring count of the game being played (a placeholder for the player's state until there is one). */
		inline Pool<EntityState> entityStates = ("EntityStates");
		inline int rings = 0;

		/* The entity the follow camera (main.cpp, outside the debug mode) sits behind: the player a script chose with sf.followCamera. */
		inline PoolId cameraTarget = UninitializedPoolId;

		/* The one-shot presentation events the gameplay posts and the frame drains (GameEvents.h). */
		inline GameEvents gameEvents;

		/* The entities asked to be removed (Entity::markForDestroy); the GameplaySystem removes them at the start of every gameplay tick. */
		inline Pool<PoolId> markedForDestroy = ("MarkedForDestroy");
	}	

	/*
	Collision resources and physics bodies; Levels and Entities hold their PoolIds (collisionId, physicsId) and remove them in destroy.
	quadTree indexes the triangles of the collisions it was last built from (QuadTree::build); it is not updated by itself.
	*/
	namespace Physics {
		inline Pool<::Physics::Collision> collisions = ("Collisions");
		inline ::Physics::QuadTree quadTree;
		inline Pool<::Physics::PhysicsBody> bodies = ("PhysicsBodies");
		inline ::Physics::PhysicsSystem physicsSystem;
		inline ::Physics::CollisionSystem collisionSystem;

		/*
		Acceleration applied to every body, scaled by its weight, in units per second squared; -Y is down (the renderer is +Y up).
		External (the per-character record of _Main.rel, Sonic's +0x78 = 0.08 per frame squared): 0.08 * 60 * 60, assuming 1 frame = 1/60 s (hypothesis).
		*/
		inline glm::vec3 gravity = glm::vec3(0.0f, -288.0f, 0.0f);
	}

	/* The opened disc images, kept open so resources can be loaded on demand from resource keys. */
	namespace Discs {
		inline GameCube::GCDisc sadx;
		inline GameCube::GCDisc shadow;
	}

	namespace Mouse { 
		inline float mouseSensitivity = 0.1f;
		inline float lastX = Globals::App::windowWidth / 2.0f;
		inline float lastY = Globals::App::windowHeight / 2.0f;
		inline bool firstMouse = true;
		/* True while the cursor is free (visible, for the debug UI) and does not turn the camera; see main.cpp. */
		inline bool cursorFree = false;
	} 
}
