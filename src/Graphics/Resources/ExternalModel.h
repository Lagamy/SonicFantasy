#pragma once
#include <string>
#include <vector>
#include "BasicMesh.h"
#include "BoundingSphere.h"
#include "PoolId.h"
#include "PoolMap.h"
#include "Texture.h"
#include <glm/glm.hpp>

namespace Graphics
{
struct ExternalPools;

/*
One submesh of an external model: its BasicMesh (always in the pools of the model's own scope, see ExternalPools) and its Texture (UninitializedPoolId = untextured). A level model may use a project
texture, so the texture says which scope's pool it is in.
*/
struct ExternalSubmesh
{
	PoolId meshId;
	PoolId textureId;
	bool textureInProject = false;
};

/* A model read from Assets/External (Loading::scanExternalAssets). inProject: it and its meshes are in the project pools, otherwise in the pools of its level. pPools: the pools its meshes and own-scope textures are in (set at registration; a level's pools are dropped with the level, so an entity that holds the model never outlives them). */
struct ExternalModel
{
	std::string name;
	BoundingSphere bounds;
	bool inProject = false;
	ExternalPools* pPools = nullptr;
	std::vector<ExternalSubmesh> submeshes;

	/*
	Draws every submesh with the shader (an entity's: the SA shader or the STH2005 one, skinned_ says which, so the skinned uniform is cleared) at the transform. A submesh with a texture binds it; with pOverrideTexture_ (Entity::setOverrideTexture) every
	textured submesh takes that one. The vertex locations are the standard layout's (BasicMesh::create): attributes a custom layout lacks read as the GL default.
	*/
	void draw(PoolId shaderId_, bool skinned_, const glm::mat4& rTransform_, Texture* pOverrideTexture_) const;
};

/*
The external assets of one scope. externalProject (Assets/External/Models|Textures) is one; the level scope has one set per loaded level (GPU_Globals.h: externalLevels, kept until the level is destroyed), and externalLevel() is the set of the level the editor works on (the last scanned). Names inside a pool are the plain asset names ("Crate", mesh "Crate#0"), so a level may hold an asset named like a project one.
*/
struct ExternalPools
{
	PoolMap<BasicMesh> meshes;
	PoolMap<Texture> textures;
	PoolMap<ExternalModel> models;

	ExternalPools(const char* meshesName_, const char* texturesName_, const char* modelsName_) : meshes(meshesName_), textures(texturesName_), models(modelsName_) {}

	/* Frees everything of the scope (models first). */
	void clear()
	{
		this->models.clear();
		this->meshes.clear();
		this->textures.clear();
	}
};
}
