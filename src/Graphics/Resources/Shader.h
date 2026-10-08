#pragma once 
#include <format>
#include <string>
#include <glad/glad.h>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include "Constants.h"

namespace Graphics 
{
/*
Location of the hasTexture uniform, fixed by layout(location = 1) in Shaders/SA and Shaders/STH2005 fragment.glsl.
Meshes with a textureId are drawn with a shader that declares it, texture bound to unit 0.
*/
constexpr GLint HasTextureLocation = 1;

/* Location of the skinned uniform of Shaders/STH2005/vertex.glsl (layout(location = 2)). */
constexpr GLint SkinnedLocation = 2;

/* Locations of the uvOffset uniform of Shaders/STH2005/vertex.glsl (layout(location = 3)) and the effectTint uniform of its fragment.glsl (layout(location = 4)); both default to no change. */
constexpr GLint UvOffsetLocation = 3;
constexpr GLint EffectTintLocation = 4;

struct Shader { 
	GLuint id;
	GLint modelLocation;
	bool alive = false; 
	GLuint createShaderModule(const GLenum type_, std::string filePath_);
	void linkModules(GLuint vertexModuleId_, GLuint fragmentModuleId_); 
	void destroy(); 

	Shader(const char* name_); 
}; 
}