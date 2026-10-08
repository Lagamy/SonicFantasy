#include "TextureFile.h"
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <stb_image_write.h>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>

namespace External
{
namespace
{
using Json = nlohmann::ordered_json;

GLint filterFromName(const std::string& rName_)
{
	if(rName_ == "nearest") return GL_NEAREST;
	if(rName_ == "linear") return GL_LINEAR;
	throw std::runtime_error(std::format("filter '{}' is not nearest or linear", rName_));
}

GLint wrapFromName(const std::string& rName_)
{
	if(rName_ == "clamp") return GL_CLAMP_TO_EDGE;
	if(rName_ == "repeat") return GL_REPEAT;
	if(rName_ == "mirror") return GL_MIRRORED_REPEAT;
	throw std::runtime_error(std::format("wrap '{}' is not clamp, repeat or mirror", rName_));
}

const char* filterName(GLint filter_) { return filter_ == GL_LINEAR ? "linear" : "nearest"; }

const char* wrapName(GLint wrap_)
{
	if(wrap_ == GL_REPEAT) return "repeat";
	if(wrap_ == GL_MIRRORED_REPEAT) return "mirror";
	return "clamp";
}
}

Graphics::Texture readTexture(const std::string& rPngPath_)
{
	int width = 0, height = 0, channels = 0;
	unsigned char* pPixels = stbi_load(rPngPath_.c_str(), &width, &height, &channels, 4);
	if(pPixels == nullptr) throw std::runtime_error(std::format("Texture '{}': {}", rPngPath_, stbi_failure_reason()));
	Graphics::Texture texture(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 4);
	std::memcpy(texture.pixels.data(), pPixels, texture.pixels.size());
	stbi_image_free(pPixels);

	std::string sidecar = rPngPath_ + ".json";
	if(std::filesystem::exists(sidecar))
	{
		try
		{
			std::ifstream file(sidecar);
			Json settings = Json::parse(file);
			if(settings.contains("filter")) texture.filter = filterFromName(settings.at("filter").get<std::string>());
			if(settings.contains("wrap")) texture.wrap = wrapFromName(settings.at("wrap").get<std::string>());
			if(settings.contains("mipmaps")) texture.mipmaps = settings.at("mipmaps").get<bool>();
		}
		catch(const std::exception& rError)
		{
			throw std::runtime_error(std::format("Texture '{}': {}", sidecar, rError.what()));
		}
	}
	return texture;
}

void writeTexture(const std::string& rPngPath_, const Graphics::Texture& rTexture_)
{
	if(rTexture_.channels != 4 || rTexture_.pixels.size() != static_cast<size_t>(rTexture_.width) * rTexture_.height * 4)
	{
		throw std::runtime_error(std::format("Texture '{}': the texture has no 4 channel pixels to write (already uploaded?)", rPngPath_));
	}
	if(stbi_write_png(rPngPath_.c_str(), static_cast<int>(rTexture_.width), static_cast<int>(rTexture_.height), 4, rTexture_.pixels.data(), static_cast<int>(rTexture_.width) * 4) == 0)
	{
		throw std::runtime_error(std::format("Texture '{}': can't write the PNG", rPngPath_));
	}
	Json settings = {{"filter", filterName(rTexture_.filter)}, {"wrap", wrapName(rTexture_.wrap)}, {"mipmaps", rTexture_.mipmaps}};
	std::ofstream file(rPngPath_ + ".json");
	if(!file) throw std::runtime_error(std::format("Texture '{}': can't write the sidecar", rPngPath_));
	file << settings.dump(2) << "\n";
}
}
