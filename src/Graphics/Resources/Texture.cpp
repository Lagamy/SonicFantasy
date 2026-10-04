#include "Texture.h"
#include <algorithm>
#include <array>
#include <utility>

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

/*
Gives every fully transparent texel the color of its opaque neighbors, spreading outwards one texel per pass. The importers
store such texels as transparent black (CMPR), which mip generation and linear filtering would blend into the edge of the
visible part as a dark fringe. Alpha stays 0, so the texel is still transparent. Expects 4 channels.
*/
static void bleedTransparentColor(std::vector<unsigned char>& rPixels_, uint32_t width_, uint32_t height_)
{
	size_t count = static_cast<size_t>(width_) * height_;
	std::vector<bool> known(count);
	size_t unknown = 0;
	for(size_t i = 0; i < count; i++)
	{
		known[i] = rPixels_[i * 4 + 3] != 0;
		if(!known[i]) unknown++;
	}
	if(unknown == 0 || unknown == count) return;

	while(unknown > 0)
	{
		std::vector<std::pair<size_t, std::array<unsigned char, 3>>> resolved;
		for(uint32_t y = 0; y < height_; y++)
		{
			for(uint32_t x = 0; x < width_; x++)
			{
				if(known[y * width_ + x]) continue;
				unsigned sum[3] = {0, 0, 0};
				unsigned neighbors = 0;
				for(int dy = -1; dy <= 1; dy++)
				{
					for(int dx = -1; dx <= 1; dx++)
					{
						int nx = static_cast<int>(x) + dx;
						int ny = static_cast<int>(y) + dy;
						if(nx < 0 || ny < 0 || nx >= static_cast<int>(width_) || ny >= static_cast<int>(height_)) continue;
						size_t n = static_cast<size_t>(ny) * width_ + nx;
						if(!known[n]) continue;
						for(int c = 0; c < 3; c++) sum[c] += rPixels_[n * 4 + c];
						neighbors++;
					}
				}
				if(neighbors == 0) continue;
				resolved.push_back({static_cast<size_t>(y) * width_ + x, {static_cast<unsigned char>(sum[0] / neighbors), static_cast<unsigned char>(sum[1] / neighbors), static_cast<unsigned char>(sum[2] / neighbors)}});
			}
		}
		for(const auto& rPixel : resolved)
		{
			std::copy(rPixel.second.begin(), rPixel.second.end(), rPixels_.begin() + rPixel.first * 4);
			known[rPixel.first] = true;
			unknown--;
		}
	}
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

	if(this->mipmaps && this->channels == 4)
	{
		bleedTransparentColor(this->pixels, this->width, this->height);
	}

	// Create and Bind texture in memory
	glGenTextures(1, &textureId);
	glBindTexture(GL_TEXTURE_2D, textureId);

	// Now set parameters:
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); // Stretch texture to fit a quad 
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, this->wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, this->wrap);
	/*
	With mipmaps the minification filter also picks the level, keeping the texture's own filter between texels:
	linear blends the two nearest levels, nearest takes the nearest level. Magnification is unchanged.
	*/
	GLint minFilter = this->filter;
	if(this->mipmaps)
	{
		minFilter = this->filter == GL_NEAREST ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
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
);

	/* The whole chain is generated from level 0 (the sRGB texture is filtered in linear space by the driver). */
	if(this->mipmaps) glGenerateMipmap(GL_TEXTURE_2D);

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