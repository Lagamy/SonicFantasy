#pragma once 
#include <cstdint> 
#include <string> 
#include <glad/glad.h>
#include <vector> 
#include "GLFW/glfw3.h"
#include "Camera.h"
#include "Entity.h"
#include "GCDisc.h"
#include "PoolMap.h"

namespace Globals {
	namespace App { 
		inline uint32_t windowWidth = 1920; 
		inline uint32_t windowHeight = 1080; 
		inline GLFWwindow* window;
	
		inline float deltaTime = 0.0f;
		inline float lastFrame = 0.0f;
	}

	namespace Game { 
		inline float cameraSpeed = 5.0;
		inline std::vector<Camera> cameras;
		inline PoolMap<Entity> entities = ("Entities"); 
	}	

	/* The opened disc images, kept open so resources can be loaded on demand from resource keys. */
	namespace Discs {
		inline Importers::GCDisc sadx;
		inline Importers::GCDisc shadow;
	}

	namespace Mouse { 
		inline float mouseSensitivity = 0.1f;
		inline float lastX = Globals::App::windowWidth / 2.0f;
		inline float lastY = Globals::App::windowHeight / 2.0f;
		inline bool firstMouse = true;
	} 
}
