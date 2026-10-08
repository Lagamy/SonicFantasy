#include "GLTFImport.h"
#include "CollisionFile.h"
#include "GLTFFile.h"
#include "ModelFile.h"
#include "TextureFile.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <unordered_map>

namespace Loading
{
namespace
{
namespace fs = std::filesystem;

const fs::path GameRoot = "Assets/Levels";
const fs::path SourceRoot = fs::path(SF_SOURCE_DIR) / "Levels";

fs::path levelFolder(const fs::path& rRoot_, const std::string& rLevelName_)
{
	return rRoot_ / rLevelName_;
}

/* Copies a file written under GameRoot to the same place under SourceRoot. */
void mirror(const fs::path& rGamePath_)
{
	fs::path target = SourceRoot / fs::relative(rGamePath_, GameRoot);
	fs::create_directories(target.parent_path());
	fs::copy_file(rGamePath_, target, fs::copy_options::overwrite_existing);
}

/* Deletes the files of the folder whose name starts with prefix_ and ends with one of the suffixes, under both roots. */
void removeFiles(const std::string& rLevelName_, const std::string& rFolderName_, const std::string& rPrefix_, const std::vector<std::string>& rSuffixes_)
{
	for(const fs::path& rRoot : {GameRoot, SourceRoot})
	{
		fs::path folder = levelFolder(rRoot, rLevelName_) / rFolderName_;
		if(!fs::is_directory(folder)) continue;
		for(const fs::directory_entry& rEntry : fs::directory_iterator(folder))
		{
			std::string name = rEntry.path().filename().string();
			if(!rEntry.is_regular_file() || !name.starts_with(rPrefix_)) continue;
			for(const std::string& rSuffix : rSuffixes_)
			{
				if(name.ends_with(rSuffix))
				{
					fs::remove(rEntry.path());
					break;
				}
			}
		}
	}
}

struct Triangle
{
	uint32_t submesh;
	uint32_t index[3];
	glm::vec3 centroid;
};

/* Splits triangles [first, last) of rTriangles_ in halves at the median centroid along the longer side (X or Z) until each part has at most MaxChunkTriangles; the parts are appended to rParts_ as ranges. */
void splitRange(std::vector<Triangle>& rTriangles_, size_t first_, size_t last_, std::vector<std::pair<size_t, size_t>>& rParts_)
{
	if(last_ - first_ <= MaxChunkTriangles)
	{
		rParts_.push_back({first_, last_});
		return;
	}
	glm::vec2 minimum(rTriangles_[first_].centroid.x, rTriangles_[first_].centroid.z), maximum = minimum;
	for(size_t i = first_; i < last_; i++)
	{
		glm::vec2 point(rTriangles_[i].centroid.x, rTriangles_[i].centroid.z);
		minimum = glm::min(minimum, point);
		maximum = glm::max(maximum, point);
	}
	int axis = (maximum.x - minimum.x) >= (maximum.y - minimum.y) ? 0 : 2;
	size_t middle = first_ + (last_ - first_) / 2;
	std::nth_element(rTriangles_.begin() + first_, rTriangles_.begin() + middle, rTriangles_.begin() + last_, [axis](const Triangle& rA_, const Triangle& rB_) { return rA_.centroid[axis] < rB_.centroid[axis]; });
	splitRange(rTriangles_, first_, middle, rParts_);
	splitRange(rTriangles_, middle, last_, rParts_);
}

/* One submesh of a chunk being built: the used vertices of the source submeshes, copied once each. */
struct SubmeshBuilder
{
	External::ModelSubmesh submesh;
	std::unordered_map<uint64_t, uint32_t> remap;
};

External::ModelData buildChunk(const External::ModelData& rSource_, const std::vector<Triangle>& rTriangles_, size_t first_, size_t last_, const std::string& rName_)
{
	size_t stride = rSource_.layout.stride;
	std::map<std::string, SubmeshBuilder> builders;
	for(size_t t = first_; t < last_; t++)
	{
		const Triangle& rTriangle = rTriangles_[t];
		const External::ModelSubmesh& rFrom = rSource_.submeshes[rTriangle.submesh];
		SubmeshBuilder& rBuilder = builders[rFrom.texture];
		rBuilder.submesh.texture = rFrom.texture;
		for(uint32_t sourceIndex : rTriangle.index)
		{
			uint64_t key = (static_cast<uint64_t>(rTriangle.submesh) << 32) | sourceIndex;
			auto found = rBuilder.remap.find(key);
			if(found == rBuilder.remap.end())
			{
				uint32_t added = rBuilder.submesh.vertexCount++;
				rBuilder.submesh.vertices.insert(rBuilder.submesh.vertices.end(), rFrom.vertices.begin() + sourceIndex * stride, rFrom.vertices.begin() + (sourceIndex + 1) * stride);
				found = rBuilder.remap.emplace(key, added).first;
			}
			rBuilder.submesh.indices.push_back(found->second);
		}
	}

	External::ModelData chunk;
	chunk.name = rName_;
	chunk.layout = rSource_.layout;
	glm::vec3 minimum(0.0f), maximum(0.0f);
	bool any = false;
	std::vector<glm::vec3> points;
	for(auto& [texture, rBuilder] : builders)
	{
		for(uint32_t v = 0; v < rBuilder.submesh.vertexCount; v++)
		{
			glm::vec3 position;
			std::memcpy(&position, rBuilder.submesh.vertices.data() + v * stride, sizeof(glm::vec3));
			minimum = any ? glm::min(minimum, position) : position;
			maximum = any ? glm::max(maximum, position) : position;
			any = true;
			points.push_back(position);
		}
		chunk.submeshes.push_back(std::move(rBuilder.submesh));
	}
	chunk.bounds.center = (minimum + maximum) * 0.5f;
	for(const glm::vec3& rPoint : points) chunk.bounds.radius = std::max(chunk.bounds.radius, glm::length(rPoint - chunk.bounds.center));
	chunk.bounds.valid = true;
	return chunk;
}

std::string safeName(const std::string& rText_)
{
	std::string name = rText_;
	for(char& rChar : name)
	{
		if(!std::isalnum(static_cast<unsigned char>(rChar)) && rChar != '_' && rChar != '-') rChar = '_';
	}
	return name.empty() ? "collision" : name;
}
}

size_t importGLTFLevelModel(const std::string& levelName_, const std::string& gltfPath_)
{
	std::vector<Graphics::Texture> textures;
	std::vector<std::string> textureNames;
	External::ModelData model = External::readGLTFModel(gltfPath_, levelName_, textures, textureNames);

	removeFiles(levelName_, "Models", levelName_ + "_", {".chunk.model.json", ".chunk.model.bin", ".png", ".png.json"});
	fs::path models = levelFolder(GameRoot, levelName_) / "Models";
	fs::create_directories(models);

	/* The glTF's image names become the level's own file names, so two levels (or two imports) never share a name. */
	std::map<std::string, std::string> fileNameOf;
	for(size_t i = 0; i < textures.size(); i++)
	{
		std::string stem = std::format("{}_tex{}", levelName_, i);
		fs::path png = models / (stem + ".png");
		External::writeTexture(png.string(), textures[i]);
		mirror(png);
		mirror(png.string() + ".json");
		fileNameOf[textureNames[i]] = stem;
	}
	for(External::ModelSubmesh& rSubmesh : model.submeshes)
	{
		if(!rSubmesh.texture.empty()) rSubmesh.texture = fileNameOf.at(rSubmesh.texture);
	}

	std::map<std::pair<int, int>, std::vector<Triangle>> cells;
	size_t stride = model.layout.stride;
	for(uint32_t s = 0; s < model.submeshes.size(); s++)
	{
		const External::ModelSubmesh& rSubmesh = model.submeshes[s];
		for(size_t i = 0; i + 2 < rSubmesh.indices.size(); i += 3)
		{
			Triangle triangle;
			triangle.submesh = s;
			triangle.centroid = glm::vec3(0.0f);
			for(int corner = 0; corner < 3; corner++)
			{
				triangle.index[corner] = rSubmesh.indices[i + corner];
				glm::vec3 position;
				std::memcpy(&position, rSubmesh.vertices.data() + triangle.index[corner] * stride, sizeof(glm::vec3));
				triangle.centroid += position / 3.0f;
			}
			cells[{static_cast<int>(std::floor(triangle.centroid.x / ChunkCellSize)), static_cast<int>(std::floor(triangle.centroid.z / ChunkCellSize))}].push_back(triangle);
		}
	}

	size_t chunkCount = 0;
	for(auto& [cell, rTriangles] : cells)
	{
		std::vector<std::pair<size_t, size_t>> parts;
		splitRange(rTriangles, 0, rTriangles.size(), parts);
		for(size_t k = 0; k < parts.size(); k++)
		{
			External::ModelData chunk = buildChunk(model, rTriangles, parts[k].first, parts[k].second, std::format("{}_{}_{}_{}.chunk", levelName_, cell.first, cell.second, k));
			fs::path json = models / (chunk.name + ".model.json");
			External::writeModel(json.string(), chunk);
			mirror(json);
			mirror(External::modelBinPath(json.string()));
			chunkCount++;
		}
	}
	return chunkCount;
}

std::string importGLTFLevelCollision(const std::string& levelName_, const std::string& gltfPath_)
{
	Physics::CollisionMesh mesh = External::readGLTFCollision(gltfPath_);
	fs::path folder = levelFolder(GameRoot, levelName_) / "Collision";
	fs::create_directories(folder);
	std::string base = safeName(fs::path(gltfPath_).stem().string());
	std::string name = base;
	for(int number = 2; fs::exists(folder / (name + ".collision.json")); number++) name = std::format("{}_{}", base, number);
	fs::path json = folder / (name + ".collision.json");
	External::writeCollision(json.string(), name, mesh);
	mirror(json);
	mirror(External::collisionBinPath(json.string()));
	return name;
}

void replaceGLTFLevelCollision(const std::string& levelName_, const std::string& name_, const std::string& gltfPath_)
{
	fs::path json = levelFolder(GameRoot, levelName_) / "Collision" / (name_ + ".collision.json");
	if(!fs::exists(json)) throw std::runtime_error(std::format("no collision file {}.", json.string()));
	Physics::CollisionMesh mesh = External::readGLTFCollision(gltfPath_);
	External::writeCollision(json.string(), name_, mesh);
	mirror(json);
	mirror(External::collisionBinPath(json.string()));
}

std::vector<std::string> listGLTFLevelCollisions(const std::string& levelName_)
{
	std::vector<std::string> names;
	fs::path folder = levelFolder(GameRoot, levelName_) / "Collision";
	if(!fs::is_directory(folder)) return names;
	const std::string suffix = ".collision.json";
	for(const fs::directory_entry& rEntry : fs::directory_iterator(folder))
	{
		std::string file = rEntry.path().filename().string();
		if(rEntry.is_regular_file() && file.ends_with(suffix)) names.push_back(file.substr(0, file.size() - suffix.size()));
	}
	std::sort(names.begin(), names.end());
	return names;
}
}
