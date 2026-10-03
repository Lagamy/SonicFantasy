#pragma once 
#include "BasicMesh.h"
#include "Globals.h"
#include "Shader.h"
#include "BasicMesh.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <glad/glad.h>

namespace Graphics 
{
/* Most skin matrices one draw can use: the size of the shared bone buffer (SSBO, binding 1, Shaders/STH2005). */
constexpr uint32_t MaxSkinBones = 256;

struct Renderer { 
	GLuint boneBuffer;

	// Main Graphics Objects init
	Renderer() = default; 
	~Renderer() = default; 
	void init(); 
	void run(Camera& rCamera_); 
	void destroy();
}; 
}