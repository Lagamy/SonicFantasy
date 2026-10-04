#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "RenderWareMesh.h"
#include "STH2005Motion.h"
#include "STH2005Keys.h"
#include "TextureBinding.h"
#include "Txd.h"

namespace STH2005
{
struct ONEEntry
{
	std::string name;
	uint32_t size;
	uint32_t offset;
	uint32_t compressedSize;
};

/* An index that refers to nothing (no texture dictionary, no binding). */
constexpr uint32_t NoIndex = 0xFFFFFFFF;

/*
A Shadow the Hedgehog .ONE archive ("One Ver 0.60") held in memory, and the resources decoded from it.
Entries are PRS-compressed; readFile decompresses one on request.
It is a pooled resource (Graphics::oneArchives, named by its disc path): instances hold its PoolId and indices into
it. users counts them; at 0 the archive removes its meshes and textures.
meshes are models built from an ModelKey recipe the first time they are used (useMesh; name -> index in
meshByName, each with its name in meshNames and its recipe in meshParts). switchPart makes the variant of a mesh with
one part replaced, or finds the mesh that already has that recipe. textureDictionaries are .TXD entries,
decoded the first time they are used (useTextureDictionary) and kept by name. motions are .MTP entries decoded the first
time they are played (useMotion), by "<mtp>|<motion>" in motionByName. bindings are per-submesh texture tables
for a (mesh, texture dictionary) pair, built once (useBinding) and shared by every instance using that pair.
*/
struct ONE
{
	std::vector<uint8_t> bytes;
	std::vector<ONEEntry> entries;

	uint32_t users = 0;
	std::vector<Graphics::RenderWareMesh> meshes;
	std::unordered_map<std::string, uint32_t> meshByName;
	std::vector<std::string> meshNames;
	std::vector<std::vector<ModelPart>> meshParts;
	std::vector<Renderware::TxdTextures> textureDictionaries;
	std::vector<std::string> textureDictionaryNames;
	std::unordered_map<std::string, uint32_t> textureDictionaryByName;
	std::vector<Motion> motions;
	std::unordered_map<std::string, uint32_t> motionByName;
	std::vector<Graphics::TextureBinding> bindings;
	std::map<std::pair<uint32_t, uint32_t>, uint32_t> bindingByPair;

	std::vector<uint8_t> readFile(const std::string& name_) const;
	uint32_t addMesh(const char* name_, Graphics::RenderWareMesh&& mesh_, const std::vector<ModelPart>& rParts_);
	uint32_t useMesh(const ModelKey& rKey_);
	uint32_t switchPart(uint32_t meshIndex_, const ModelPart& rPrevious_, const ModelPart& rDesired_);
	uint32_t useTextureDictionary(const char* name_);
	uint32_t addTextureDictionary(const std::string& name_, const std::vector<uint8_t>& rBytes_);
	uint32_t useBinding(uint32_t meshIndex_, uint32_t textureDictionaryIndex_);
	uint32_t useMotion(const MotionKey& rKey_);
	void destroy();
};

ONE openONE(std::vector<uint8_t> bytes_);
}
