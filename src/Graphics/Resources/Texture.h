#pragma once 
#include <cstdint>
#include <glad/glad.h>
#include <stb_image.h>
#include <stdexcept>
#include <cstring> 
#include <vector>

namespace Graphics 
{
struct Texture 
{ 
	std::vector<unsigned char> pixels;
	uint32_t width, height, channels;
	GLuint textureId;
	GLint filter = GL_NEAREST;
	GLint wrap = GL_CLAMP_TO_EDGE;
	bool alive = false; 

	Texture(uint32_t width_, uint32_t height_, uint32_t channels_); 
	void setPixelBatch(void* data_, uint32_t dstOffset_, uint32_t size_);
	void create();
	void destroy(); 
	void use(); 
}; 
}