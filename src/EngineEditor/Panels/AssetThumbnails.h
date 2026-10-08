#pragma once
#include "PoolId.h"
#include "Constants.h"
#include "ONE.h"
#include <glad/glad.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Graphics
{
struct PrefabThumbnails;
/*
The pictures of the texture and model tiles of the AssetPanel, read from the disc images only while a tile is on screen. get(game, source) decodes the first texture of the asset (a SADX .GVM archive or a single .GVR, or a Shadow .TXD, loose or an entry of a
.ONE archive) with importGvm / importTxd into the Texture pool and returns its GL texture; a tile that is not asked for in a frame is removed again by endFrame (so scrolling away, closing a folder or leaving the tab frees it), and decoded again
when it comes back. At most PerFrame pictures are decoded per frame, because each reads a file from the disc: get returns 0 for one whose turn has not come, and for one that can't be shown (a .png, which the importers do not read, a land table, or a format they throw on; the tile keeps its placeholder, a failure is not retried while the tile stays on screen).
The .ONE archive being read is kept (one at a time) while tiles in a frame use it, so a folder of entries of one archive reads the disc once, and freed with the first frame none does. A model's picture is drawn by PrefabThumbnails::renderModel (setModelRenderer) and deleted when it is released. Drawn by AssetPanel; beginFrame / endFrame every frame.
*/
struct AssetThumbnails
{
	static constexpr int PerFrame = 2;

	void beginFrame();

	/* Where the models' pictures are drawn (PrefabThumbnails::renderModel, which owns the target and the camera): set once by PrefabPanel; without it model tiles keep the placeholder. */
	void setModelRenderer(PrefabThumbnails* pRenderer_);

	/* The texture, or 0 if it is not made yet or can't be shown. A texture's picture has its top row first (plain UVs); a model's is a render target copy, bottom row first (flip the UVs). game_ 0 = SADX, 1 = Shadow; source_ is the catalog's source column. */
	GLuint get(int game_, const std::string& source_, bool model_ = false);

	/* Removes every picture not asked for since beginFrame, and the kept archive if no tile used it. */
	void endFrame();

	/* Removes everything. */
	void destroy();

private:
	struct Entry
	{
		bool usedThisFrame = true;

		/* The GVM's texture set (textures owned by it), or the TXD's textures; texture is the one shown, 0 if the asset can't be shown. */
		PoolId setId = UninitializedPoolId;
		std::vector<PoolId> textureIds;
		GLuint texture = 0;

		/* A model's picture is a texture of its own (deleted on release), not part of the pools. */
		GLuint ownTexture = 0;
	};

	std::unordered_map<std::string, Entry> entries;
	int madeThisFrame = 0;
	PrefabThumbnails* pModelRenderer = nullptr;

	std::string archivePath;
	std::unique_ptr<STH2005::ONE> archive;
	bool archiveUsed = false;

	void make(Entry& rEntry_, int game_, const std::string& source_, bool model_);
	void release(Entry& rEntry_);
};
}
