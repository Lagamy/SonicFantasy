#pragma once 
#include <cstdint> 
#include <string> 
#include <glad/glad.h>
#include <vector> 
#include "GLFW/glfw3.h"
#include "Camera.h"
#include "Entity.h"
#include "Level.h"
#include "BoundArea.h"
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
	
		inline float deltaTime = 0.0f;
		inline float lastFrame = 0.0f;
	}

	namespace Game { 
		inline std::vector<Camera> cameras;
		inline PoolMap<Entity> entities = ("Entities"); 
		inline PoolMap<Level> levels = ("Levels"); 
		inline Pool<BoundArea> boundAreas = ("BoundAreas");
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
		Acceleration applied to every body, scaled by its weight, in units per second squared. Zero until set (nothing falls);
		-Y is down (the renderer is +Y up).
		*/
		inline glm::vec3 gravity = glm::vec3(0.0f);
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
