#include "ExternalAssets.h"
#include "GPU_Globals.h"
#include "ModelFile.h"
#include "TextureFile.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>
#include <vector>

namespace Loading
{
namespace
{
namespace fs = std::filesystem;

const fs::path ExternalRoot = "Assets/External";

/* The files in dir_ whose name ends with suffix_, sorted so registration order does not depend on the file system. */
std::vector<fs::path> filesWithSuffix(const fs::path& rDir_, const std::string& rSuffix_)
{
	std::vector<fs::path> result;
	if(!fs::is_directory(rDir_)) return result;
	for(const fs::directory_entry& rEntry : fs::directory_iterator(rDir_))
	{
		if(rEntry.is_regular_file() && rEntry.path().filename().string().ends_with(rSuffix_)) result.push_back(rEntry.path());
	}
	std::sort(result.begin(), result.end());
	return result;
}

/* The standard layout stays the one static object; any other is stored once per distinct key. */
const Graphics::VertexLayout* findOrAddLayout(const Graphics::VertexLayout& rLayout_)
{
	std::string key = rLayout_.key();
	if(key == Graphics::VertexLayout::standard().key()) return &Graphics::VertexLayout::standard();
	auto found = Graphics::vertexLayouts.nameToId.find(key);
	if(found != Graphics::vertexLayouts.nameToId.end()) return &Graphics::vertexLayouts[found->second];
	return &Graphics::vertexLayouts[Graphics::vertexLayouts.add(key.c_str(), rLayout_)];
}

bool projectScanned = false;
std::string levelScopeName;
uint32_t revision = 0;

void removeModel(Graphics::ExternalPools& rPools_, const std::string& rName_)
{
	auto found = rPools_.models.nameToId.find(rName_);
	if(found == rPools_.models.nameToId.end()) return;
	PoolId id = found->second;
	for(const Graphics::ExternalSubmesh& rSubmesh : rPools_.models[id].submeshes) rPools_.meshes.remove(rSubmesh.meshId);
	rPools_.models.remove(id);
}

/*
Makes the GPU meshes of the model and registers it (a model of that name is replaced). A submesh's texture is looked up in rPools_ first and, for a level model (not inProject_), then in the project pools; a
missing one is printed and the submesh stays untextured. Throws if a mesh can't be made, after freeing the meshes made so far.
*/
void registerModel(Graphics::ExternalPools& rPools_, bool inProject_, External::ModelData& rData_)
{
	const Graphics::VertexLayout* pLayout = findOrAddLayout(rData_.layout);
	Graphics::ExternalModel model;
	model.name = rData_.name;
	model.bounds = rData_.bounds;
	model.inProject = inProject_;
	try
	{
		for(size_t i = 0; i < rData_.submeshes.size(); i++)
		{
			External::ModelSubmesh& rSubmesh = rData_.submeshes[i];
			PoolId meshId = rPools_.meshes.add(std::format("{}#{}", rData_.name, i).c_str());
			model.submeshes.push_back({meshId, UninitializedPoolId, inProject_});
			Graphics::BasicMesh& rMesh = rPools_.meshes[meshId];
			rMesh.verticies.setLayout(pLayout);
			rMesh.verticies.bytes = std::move(rSubmesh.vertices);
			rMesh.indicies = std::move(rSubmesh.indices);
			rMesh.create();

			if(rSubmesh.texture.empty()) continue;
			Graphics::ExternalSubmesh& rResult = model.submeshes.back();
			auto found = rPools_.textures.nameToId.find(rSubmesh.texture);
			if(found != rPools_.textures.nameToId.end())
			{
				rResult.textureId = found->second;
			}
			else if(!inProject_)
			{
				auto projectFound = Graphics::externalProject.textures.nameToId.find(rSubmesh.texture);
				if(projectFound != Graphics::externalProject.textures.nameToId.end())
				{
					rResult.textureId = projectFound->second;
					rResult.textureInProject = true;
				}
			}
			if(rResult.textureId == UninitializedPoolId) std::cerr << "External model " << rData_.name << ": no texture named " << rSubmesh.texture << ", submesh " << i << " stays untextured." << std::endl;
		}
	}
	catch(...)
	{
		for(const Graphics::ExternalSubmesh& rSubmesh : model.submeshes) rPools_.meshes.remove(rSubmesh.meshId);
		throw;
	}
	removeModel(rPools_, rData_.name);
	rPools_.models.add(rData_.name.c_str(), std::move(model));
}

/* Reads the folder (rBase_/Textures, rBase_/Models) into the pools: images (.png, .jpg, .jpeg: stb_image decodes both) first, then models. A file that fails (or repeats the name of an earlier one) is printed to stderr and skipped (one bad file must not stop a level from opening). */
void scanFolder(Graphics::ExternalPools& rPools_, bool inProject_, const fs::path& rBase_)
{
	for(const char* pSuffix : {".png", ".jpg", ".jpeg"})
	{
		for(const fs::path& rFile : filesWithSuffix(rBase_ / "Textures", pSuffix))
		{
			try
			{
				PoolId id = rPools_.textures.addUnique(rFile.stem().string().c_str(), External::readTexture(rFile.string()));
				rPools_.textures[id].create();
			}
			catch(const std::exception& rError)
			{
				std::cerr << "External texture skipped: " << rError.what() << std::endl;
			}
		}
	}
	for(const fs::path& rFile : filesWithSuffix(rBase_ / "Models", ".model.json"))
	{
		/* The chunks of a glTF level's visual (GLTFImport.h) are read by the level itself, streamed; they are not assets to pick. */
		if(rFile.filename().string().ends_with(".chunk.model.json")) continue;
		try
		{
			External::ModelData data = External::readModel(rFile.string());
			if(data.name + ".model.json" != rFile.filename().string()) throw std::runtime_error(std::format("Model '{}': the name '{}' in the header is not the file name", rFile.string(), data.name));
			registerModel(rPools_, inProject_, data);
		}
		catch(const std::exception& rError)
		{
			std::cerr << "External model skipped: " << rError.what() << std::endl;
		}
	}
}

/* The pools and the folder (under Assets/External) of a scope. Throws for the level scope while no level is open. */
Graphics::ExternalPools& poolsOf(ExternalScope scope_)
{
	if(scope_ == ExternalScope::Project) return Graphics::externalProject;
	if(levelScopeName.empty()) throw std::runtime_error("External assets: no level is open for the level scope");
	return Graphics::externalLevel;
}

fs::path folderOf(ExternalScope scope_)
{
	return scope_ == ExternalScope::Project ? ExternalRoot : fs::path("Assets/Levels") / levelScopeName;
}

#ifdef EDITOR
/*
Copies a file the editor wrote under the scope's folder in Assets to the same place in the source tree (External/ for the project, Levels/ for a level's folder: what the next build copies back), so an import is not lost with build/.
Throws std::runtime_error if the copy fails.
*/
void mirrorToSource(ExternalScope scope_, const fs::path& rGamePath_)
{
	fs::path gameBase = scope_ == ExternalScope::Project ? ExternalRoot : fs::path("Assets/Levels");
	fs::path target = fs::path(SF_SOURCE_DIR) / (scope_ == ExternalScope::Project ? "External" : "Levels") / fs::relative(rGamePath_, gameBase);
	std::error_code error;
	fs::create_directories(target.parent_path(), error);
	if(!error) fs::copy_file(rGamePath_, target, fs::copy_options::overwrite_existing, error);
	if(error) throw std::runtime_error(std::format("External assets: can't copy {} to the source tree: {}", rGamePath_.string(), error.message()));
}
#endif

/* A name that is a plain file name: it becomes the file's name. */
void requireFileName(const std::string& rName_)
{
	if(rName_.empty() || rName_.find_first_of("/\\") != std::string::npos || rName_ == "." || rName_ == "..") throw std::runtime_error(std::format("External assets: '{}' is not a usable asset name", rName_));
}
}

void scanExternalAssets(const std::string& levelName_)
{
	if(!projectScanned)
	{
		projectScanned = true;
		scanFolder(Graphics::externalProject, true, ExternalRoot);
		revision++;
	}
	if(levelScopeName != levelName_)
	{
		Graphics::externalLevel.clear();
		levelScopeName = levelName_;
		scanFolder(Graphics::externalLevel, false, folderOf(ExternalScope::Level));
		revision++;
	}
}

void importExternalTexture(const std::string& name_, Graphics::Texture texture_, ExternalScope scope_)
{
	requireFileName(name_);
	Graphics::ExternalPools& rPools = poolsOf(scope_);
	if(rPools.textures.nameToId.contains(name_)) throw std::runtime_error(std::format("External assets: a texture named '{}' exists already in that scope", name_));
	fs::path folder = folderOf(scope_) / "Textures";
	fs::create_directories(folder);
	External::writeTexture((folder / (name_ + ".png")).string(), texture_);
#ifdef EDITOR
	mirrorToSource(scope_, folder / (name_ + ".png"));
	mirrorToSource(scope_, folder / (name_ + ".png.json"));
#endif
	PoolId id = rPools.textures.add(name_.c_str(), std::move(texture_));
	rPools.textures[id].create();
	revision++;
}

void importExternalModel(External::ModelData model_, ExternalScope scope_)
{
	requireFileName(model_.name);
	Graphics::ExternalPools& rPools = poolsOf(scope_);
	fs::path folder = folderOf(scope_) / "Models";
	fs::create_directories(folder);
	External::writeModel((folder / (model_.name + ".model.json")).string(), model_);
#ifdef EDITOR
	mirrorToSource(scope_, folder / (model_.name + ".model.json"));
	mirrorToSource(scope_, External::modelBinPath((folder / (model_.name + ".model.json")).string()));
#endif
	registerModel(rPools, scope_ == ExternalScope::Project, model_);
	revision++;
}

uint32_t externalRevision()
{
	return revision;
}

const Graphics::ExternalModel* findExternalModel(const std::string& name_)
{
	for(Graphics::ExternalPools* pPools : {&Graphics::externalLevel, &Graphics::externalProject})
	{
		auto found = pPools->models.nameToId.find(name_);
		if(found != pPools->models.nameToId.end()) return &pPools->models[found->second];
	}
	return nullptr;
}

Graphics::Texture* findExternalTexture(const std::string& name_)
{
	for(Graphics::ExternalPools* pPools : {&Graphics::externalLevel, &Graphics::externalProject})
	{
		auto found = pPools->textures.nameToId.find(name_);
		if(found != pPools->textures.nameToId.end()) return &pPools->textures[found->second];
	}
	return nullptr;
}
}
