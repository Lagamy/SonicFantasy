#include "AssetThumbnails.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include "Gvm.h"
#include "PrefabThumbnails.h"
#include "Txd.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>

namespace Graphics
{
namespace
{
std::string lower(std::string text_)
{
	std::transform(text_.begin(), text_.end(), text_.begin(), [](unsigned char c_) { return static_cast<char>(std::tolower(c_)); });
	return text_;
}
}

void AssetThumbnails::beginFrame()
{
	this->madeThisFrame = 0;
	this->archiveUsed = false;
	for(auto& rEntry : this->entries) rEntry.second.usedThisFrame = false;
}

void AssetThumbnails::setModelRenderer(PrefabThumbnails* pRenderer_)
{
	this->pModelRenderer = pRenderer_;
}

GLuint AssetThumbnails::get(int game_, const std::string& source_, bool model_)
{
	std::string key = std::string(model_ ? "M" : "T") + (game_ == 0 ? "S|" : "H|") + source_;
	auto found = this->entries.find(key);
	if(found != this->entries.end())
	{
		found->second.usedThisFrame = true;
		return found->second.texture;
	}
	if(this->madeThisFrame >= PerFrame) return 0;
	this->madeThisFrame++;
	Entry& rEntry = this->entries[key];
	this->make(rEntry, game_, source_, model_);
	return rEntry.texture;
}

/*
Decodes the asset behind a source: SADX "<file>.GVR" is a single texture (importGvr), "<file>.GVM" is read from the SADX disc and imported as a texture set named "(thumbnail)/<file>" (a name of its own, the set the game loads for an instance is not touched); Shadow "<archive> > <entry>.TXD" is an entry
of an archive of the Shadow disc, a plain path a loose file. The first texture is the picture. Anything else, or an importer failing, leaves texture 0 (reported on stderr once, when it is decoded).
*/
void AssetThumbnails::make(Entry& rEntry_, int game_, const std::string& source_, bool model_)
{
	if(model_)
	{
		if(this->pModelRenderer != nullptr)
		{
			/* Whether the archive has the .TXD named like the model, so that the renderer does not have to try a missing entry (the archive throws for one). */
			bool textured = false;
			size_t split = source_.find(" > ");
			if(game_ == 1 && split != std::string::npos)
			{
				try
				{
					std::string path = source_.substr(0, split);
					std::string entry = source_.substr(split + 3);
					std::string textureEntry = entry.substr(0, entry.rfind('.')) + ".TXD";
					if(this->archive == nullptr || this->archivePath != path)
					{
						this->archive = std::make_unique<STH2005::ONE>(STH2005::openONE(Globals::Discs::shadow.readFile(path)));
						this->archivePath = path;
					}
					this->archiveUsed = true;
					for(const STH2005::ONEEntry& rEntry : this->archive->entries) textured = textured || rEntry.name == textureEntry;
				}
				catch(const std::exception& rException_)
				{
					std::fprintf(stderr, "AssetThumbnails: %s: %s\n", source_.c_str(), rException_.what());
				}
			}
			rEntry_.ownTexture = this->pModelRenderer->renderModel(game_, source_, textured);
		}
		rEntry_.texture = rEntry_.ownTexture;
		return;
	}
	try
	{
		if(game_ == 0)
		{
			if(lower(source_).ends_with(".gvm"))
			{
				std::string setName = "(thumbnail)/" + source_;
				Ninja::importGvm(setName.c_str(), Globals::Discs::sadx.readFile(source_));
				rEntry_.setId = textureSets.nameToId.find(setName)->second;
				if(!textureSets[rEntry_.setId].ordered.empty()) rEntry_.texture = textures[textureSets[rEntry_.setId].ordered[0]].textureId;
			}
			return;
		}

		std::string entry = source_;
		std::vector<uint8_t> bytes;
		size_t split = source_.find(" > ");
		if(split == std::string::npos)
		{
			if(!lower(source_).ends_with(".txd")) return;
			bytes = Globals::Discs::shadow.readFile(source_);
		}
		else
		{
			entry = source_.substr(split + 3);
			if(!lower(entry).ends_with(".txd")) return;
			std::string path = source_.substr(0, split);
			if(this->archive == nullptr || this->archivePath != path)
			{
				this->archive = std::make_unique<STH2005::ONE>(STH2005::openONE(Globals::Discs::shadow.readFile(path)));
				this->archivePath = path;
			}
			this->archiveUsed = true;
			bytes = this->archive->readFile(entry);
		}
		Renderware::TxdTextures dictionary = Renderware::importTxd(bytes);
		rEntry_.textureIds = dictionary.ids;
		if(!dictionary.ids.empty()) rEntry_.texture = textures[dictionary.ids[0]].textureId;
	}
	catch(const std::exception& rException_)
	{
		std::fprintf(stderr, "AssetThumbnails: %s: %s\n", source_.c_str(), rException_.what());
		if(game_ == 0 && rEntry_.setId == UninitializedPoolId)
		{
			auto found = textureSets.nameToId.find("(thumbnail)/" + source_);
			if(found != textureSets.nameToId.end()) rEntry_.setId = found->second;
		}
		this->release(rEntry_);
	}
}

void AssetThumbnails::release(Entry& rEntry_)
{
	if(rEntry_.setId != UninitializedPoolId) textureSets.remove(rEntry_.setId);
	for(PoolId textureId : rEntry_.textureIds) textures.remove(textureId);
	if(rEntry_.ownTexture != 0) glDeleteTextures(1, &rEntry_.ownTexture);
	rEntry_.ownTexture = 0;
	rEntry_.setId = UninitializedPoolId;
	rEntry_.textureIds.clear();
	rEntry_.texture = 0;
}

void AssetThumbnails::endFrame()
{
	for(auto it = this->entries.begin(); it != this->entries.end();)
	{
		if(it->second.usedThisFrame)
		{
			++it;
			continue;
		}
		this->release(it->second);
		it = this->entries.erase(it);
	}
	if(!this->archiveUsed) this->archive.reset();
}

void AssetThumbnails::destroy()
{
	for(auto& rEntry : this->entries) this->release(rEntry.second);
	this->entries.clear();
	this->archive.reset();
}
}
