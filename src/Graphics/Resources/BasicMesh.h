#pragma once 
#include <cstdint>
#include <glad/glad.h>
#include <glm/ext/vector_float3.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>
#include <cstring>
#include <vector>

namespace Graphics
{
/*
boneIndices / boneWeights are a skinned vertex's up to 4 skin bones and their weights (Shaders/STH2005); zero for
everything else, which ignores them.
*/
struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 color;
	glm::u8vec4 boneIndices{0};
	glm::vec4 boneWeights{0.0f};
};

struct BasicMesh
{
	GLuint VAO;
	GLuint VBO;
	GLuint EBO;

	std::vector<Vertex> verticies;
	std::vector<uint32_t> indicies; 
	bool alive = false; 
	
	void create(); 
	void draw(); // Using current shader
	void destroy();
	
	// Used for importing from any bin. (Proved to be not really usefull)
	// void addVertexBatch(void* data_, uint64_t vertexAmount_); 
	// void addIndexBatch(void* data_, uint64_t indexAmount_); 
};
} 
