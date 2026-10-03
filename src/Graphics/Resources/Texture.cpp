#include "Texture.h"

namespace Graphics 
{	
Texture::Texture(uint32_t width_, uint32_t height_, uint32_t channels_) : width(width_), height(height_), channels(channels_)
{
	pixels.resize(static_cast<size_t>(width_) * height_ * channels_);
}

void Texture::setPixelBatch(void* data_, uint32_t dstOffset_, uint32_t size_)
{
	std::memcpy(this->pixels.data() + dstOffset_, data_, size_); 	
}

void Texture::create()
{
	if (pixels.empty()) {
    	throw std::runtime_error("Failed to load texture!");
	}
	//std::cout << width;

	/*
	OpenGL's 4.3 feature to create read/write(in shaders) textures, instead of read only: 
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	
	In shader: 
	layout(rgba8, binding = 0) uniform image2D uMaskImage;

	void main() {
		ivec2 coord = ivec2(gl_FragCoord.xy);
		imageStore(uMaskImage, coord, vec4(1.0, 0.0, 0.0, 1.0)); // Write a red pixel
	}

	*/

	// Create and Bind texture in memory
	glGenTextures(1, &textureId);
	glBindTexture(GL_TEXTURE_2D, textureId);

	// Now set parameters:
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); // Stretch texture to fit a quad 
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, this->wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, this->wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, this->filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, this->filter);

	// Texture upload:
glTexImage2D(
    GL_TEXTURE_2D,
    0,
    GL_SRGB8_ALPHA8,   
    this->width,
    this->height,
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    this->pixels.data()
);	//glGenerateMipmap(GL_TEXTURE_2D);

	this->pixels.clear(); 

	this->alive = true; 
}

void Texture::destroy()
{
	glDeleteTextures(1, &textureId);
}

void Texture::use()
{
	glBindTexture(GL_TEXTURE_2D, textureId);
}
}