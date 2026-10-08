#include "ONE.h"
#include "Prs.h"
#include "BinaryRead.h"
#include "GPU_Globals.h"
#include "Model.h"
#include "Mtp.h"
#include "Bon.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string_view>

namespace STH2005
{
/*
"One Ver 0.60" layout (external, HeroesONE-Reloaded ONEShadowArchive; confirmed on character/shadow.one), little-endian:
0x00 u32 0, 0x04 u32 archive size - 0xC, 0x08 u32 RenderWare version stamp, 0x0C version string (16 bytes),
0x1C u32 file count, 0x20 padding (32 bytes). From 0x40: fileCount + 2 entries of 56 bytes:
name (44 bytes, null-terminated), u32 uncompressed size, u32 data offset (relative to 0xC), u32 always 1.
Entries with that last field != 1 are dummies (the first two in shadow.one).
Compressed size is the distance to the next entry's offset, or to the end of the archive for the last one.
"One Ver 0.50" (disc file common/CommonEffect.one, decoded 2026-10-07: 83 entries, all PRS sizes match) has the same header, entry count and 56-byte entries,
but the name field is 32 bytes, so the size, offset and always-1 fields sit at +32, +36 and +40 (the rest of the entry is zeros).
*/
constexpr uint32_t ONEHeaderSize = 0xC;
constexpr uint32_t ONEEntriesOffset = 0x40;
constexpr uint32_t ONEEntrySize = 56;
constexpr uint32_t ONEName060 = 44;
constexpr uint32_t ONEName050 = 32;

ONE openONE(std::vector<uint8_t> bytes_)
{
	ONE archive;
	archive.bytes = std::move(bytes_);
	const std::vector<uint8_t>& rBytes = archive.bytes;

	bool version050 = rBytes.size() >= ONEEntriesOffset && std::memcmp(rBytes.data() + 0x0C, "One Ver 0.50", 13) == 0;
	if(rBytes.size() < ONEEntriesOffset || (!version050 && std::memcmp(rBytes.data() + 0x0C, "One Ver 0.60", 13) != 0))
	{
		throw std::runtime_error("STH2005::ONE: not a \"One Ver 0.60\" or \"One Ver 0.50\" archive.");
	}
	uint32_t nameSize = version050 ? ONEName050 : ONEName060;

	uint32_t fileCount = readLE<uint32_t>(rBytes, 0x1C);
	for(uint32_t i = 0; i < fileCount + 2; i++)
	{
		uint32_t entry = ONEEntriesOffset + i * ONEEntrySize;
		if(readLE<uint32_t>(rBytes, entry + nameSize + 8) != 1)
		{
			continue;
		}
		const char* pName = reinterpret_cast<const char*>(rBytes.data() + entry);
		archive.entries.push_back(ONEEntry{
			std::string(pName, strnlen(pName, nameSize)),
			readLE<uint32_t>(rBytes, entry + nameSize),
			readLE<uint32_t>(rBytes, entry + nameSize + 4),
			0
		});
	}

	uint32_t dataEnd = static_cast<uint32_t>(rBytes.size()) - ONEHeaderSize;
	for(size_t i = 0; i < archive.entries.size(); i++)
	{
		uint32_t next = (i + 1 < archive.entries.size()) ? archive.entries[i + 1].offset : dataEnd;
		if(next < archive.entries[i].offset || next > dataEnd)
		{
			throw std::runtime_error(std::format("STH2005::ONE: entry {} has an invalid data range.", archive.entries[i].name));
		}
		archive.entries[i].compressedSize = next - archive.entries[i].offset;
	}

	return archive;
}

std::vector<uint8_t> ONE::readFile(const std::string& name_) const
{
	for(const ONEEntry& rEntry : this->entries)
	{
		if(rEntry.name != name_)
		{
			continue;
		}
		auto start = this->bytes.begin() + ONEHeaderSize + rEntry.offset;
		std::vector<uint8_t> compressed(start, start + rEntry.compressedSize);
		std::vector<uint8_t> data = decompressPrs(compressed);
		if(data.size() != rEntry.size)
		{
			throw std::runtime_error(std::format("STH2005::ONE: {} decompressed to {} bytes, expected {}.", name_, data.size(), rEntry.size));
		}
		return data;
	}
	throw std::runtime_error(std::format("STH2005::ONE: no entry named {}.", name_));
}

/* name_ must not already be a mesh of this archive. rParts_ is the recipe it was built from. Returns the mesh's index. */
uint32_t ONE::addMesh(const char* name_, Graphics::RenderWareMesh&& mesh_, const std::vector<ModelPart>& rParts_)
{
	if(this->meshByName.contains(name_))
	{
		throw std::runtime_error(std::format("STH2005::ONE: mesh {} already exists.", name_));
	}
	uint32_t index = static_cast<uint32_t>(this->meshes.size());
	this->meshes.push_back(std::move(mesh_));
	this->meshNames.push_back(name_);
	this->meshParts.push_back(rParts_);
	this->meshByName.emplace(name_, index);
	return index;
}

/*
Index of the mesh the key names: the one already built, or built now from the key's recipe.
A key whose name exists with different parts is a different recipe under the same name and throws.
*/
uint32_t ONE::useMesh(const ModelKey& rKey_)
{
	auto found = this->meshByName.find(rKey_.name);
	if(found == this->meshByName.end())
	{
		return importModel(rKey_.name, *this, rKey_.parts);
	}
	if(this->meshParts[found->second] != rKey_.parts)
	{
		throw std::runtime_error(std::format("STH2005::ONE: mesh {} was built from different parts than the key's.", rKey_.name));
	}
	return found->second;
}

/*
Index of the mesh that is meshIndex_ with the part equal to rPrevious_ replaced by rDesired_. If a mesh with those parts
exists (e.g. when switching back) it is reused, otherwise it is built now, named "<mesh>|<previous dff>><desired dff>".
Throws if the mesh has no such part.
*/
uint32_t ONE::switchPart(uint32_t meshIndex_, const ModelPart& rPrevious_, const ModelPart& rDesired_)
{
	std::vector<ModelPart> parts = this->meshParts[meshIndex_];
	auto part = std::find(parts.begin(), parts.end(), rPrevious_);
	if(part == parts.end())
	{
		throw std::runtime_error(std::format("STH2005::ONE: mesh {} has no part {}.", this->meshNames[meshIndex_], rPrevious_.dff));
	}
	*part = rDesired_;

	for(uint32_t i = 0; i < this->meshParts.size(); i++)
	{
		if(this->meshParts[i] == parts) return i;
	}
	std::string name = std::format("{}|{}>{}", this->meshNames[meshIndex_], rPrevious_.dff, rDesired_.dff);
	return importModel(name.c_str(), *this, parts);
}

/*
Index of the texture dictionary stored in the .TXD entry name_. The first use decodes the entry and uploads its
textures to the GPU; later uses find it again.
*/
uint32_t ONE::useTextureDictionary(const char* name_)
{
	auto found = this->textureDictionaryByName.find(name_);
	if(found != this->textureDictionaryByName.end())
	{
		return found->second;
	}
	return this->addTextureDictionary(name_, this->readFile(name_));
}

/* Decodes a .TXD that is not an entry of this archive (see addONEInstance) and keeps it under name_, so the same one is not decoded again. */
uint32_t ONE::addTextureDictionary(const std::string& name_, const std::vector<uint8_t>& rBytes_)
{
	uint32_t index = static_cast<uint32_t>(this->textureDictionaries.size());
	this->textureDictionaries.push_back(Renderware::importTxd(rBytes_));
	this->textureDictionaryNames.push_back(name_);
	this->textureDictionaryByName.emplace(name_, index);
	return index;
}

/*
Index of the binding of a mesh against a texture dictionary: every submesh's textureName is looked up in the
dictionary's byName, once, so draw only indexes the result. A name missing from the dictionary is printed and the submesh is left
untextured (UninitializedPoolId), like one without a texture name: stage objects can name textures their archive's .TXD does not have
(observed: FRUIT, whose texture s0404_nn022 is in the .TXDs of stages 0301, 0404 and 0603 but not in the common or stg0100 ones; its key names stage 0404's .TXD, see
EntityKey / genEntityKeys.py).
*/
uint32_t ONE::useBinding(uint32_t meshIndex_, uint32_t textureDictionaryIndex_)
{
	std::pair<uint32_t, uint32_t> pair(meshIndex_, textureDictionaryIndex_);
	auto found = this->bindingByPair.find(pair);
	if(found != this->bindingByPair.end())
	{
		return found->second;
	}

	const Renderware::TxdTextures& rDictionary = this->textureDictionaries[textureDictionaryIndex_];
	Graphics::TextureBinding binding;
	for(const Graphics::RenderWareAtomic& rAtomic : this->meshes[meshIndex_].atomics)
	{
		std::vector<PoolId>& rIds = binding.submeshTextureIds.emplace_back();
		for(const Graphics::RenderWareSubmesh& rSubmesh : rAtomic.submeshes)
		{
			if(rSubmesh.textureName.empty())
			{
				rIds.push_back(UninitializedPoolId);
				continue;
			}
			auto texture = rDictionary.byName.find(rSubmesh.textureName);
			if(texture == rDictionary.byName.end())
			{
				std::printf("STH2005::ONE: texture %s of %s is not in %s, left untextured.\n", rSubmesh.textureName.c_str(), this->meshNames[meshIndex_].c_str(), this->textureDictionaryNames[textureDictionaryIndex_].c_str());
				rIds.push_back(UninitializedPoolId);
				continue;
			}
			rIds.push_back(texture->second);
		}
	}
	uint32_t index = static_cast<uint32_t>(this->bindings.size());
	this->bindings.push_back(std::move(binding));
	this->bindingByPair.emplace(pair, index);
	return index;
}

/*
Index of the motion the key names: the one already decoded, or decoded now from its .MTP entry (read and decompressed
for each new motion) with the node names of its .BON entry. Throws for a motion readMtpMotion doesn't support.
*/
uint32_t ONE::useMotion(const MotionKey& rKey_)
{
	std::string name = std::format("{}|{}", rKey_.mtp, rKey_.motion);
	auto found = this->motionByName.find(name);
	if(found != this->motionByName.end())
	{
		return found->second;
	}
	uint32_t index = static_cast<uint32_t>(this->motions.size());
	std::string_view file = rKey_.mtp;
	bool isMtn = file.size() >= 4 && std::equal(file.end() - 4, file.end(), ".MTN", [](char a_, char b_) { return std::toupper(static_cast<unsigned char>(a_)) == b_; });
	std::vector<uint8_t> bytes = this->readFile(rKey_.mtp);
	Bon bon = readBon(this->readFile(rKey_.bon));
	this->motions.push_back(isMtn ? readMtnMotion(bytes, rKey_.motion, bon) : readMtpMotion(bytes, rKey_.motion, bon));
	this->motionByName.emplace(name, index);
	return index;
}

/* Frees the GPU data of the meshes and removes the textures of every decoded dictionary. Called by PoolMap::remove. */
void ONE::destroy()
{
	for(Graphics::RenderWareMesh& rMesh : this->meshes)
	{
		rMesh.destroy();
	}
	for(const Renderware::TxdTextures& rDictionary : this->textureDictionaries)
	{
		for(PoolId textureId : rDictionary.ids)
		{
			Graphics::textures.remove(textureId);
		}
	}
}
}
