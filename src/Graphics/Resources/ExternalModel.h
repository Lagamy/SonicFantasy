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

/* A model read from Assets/External (Loading::scanExternalAssets). inProject: it and its meshes are in the project pools, otherwise in the level pools. */
struct ExternalModel
{
	std::string name;
	BoundingSphere bounds;
	bool inProject = false;
	std::vector<ExternalSubmesh> submeshes;

	/*
	Draws every submesh with the shader (an entity's: the SA shader or the STH2005 one, skinned_ says which, so the skinned uniform is cleared) at the transform. A submesh with a texture binds it; with pOverrideTexture_ (Entity::setOverrideTexture) every
	textured submesh takes that one. The vertex locations are the standard layout's (BasicMesh::create): attributes a custom layout lacks read as the GL default.
	*/
	void draw(PoolId shaderId_, bool skinned_, const glm::mat4& rTransform_, Texture* pOverrideTexture_) const;
};

/*
The external assets of one scope. Two exist (GPU_Globals.h): externalProject (Assets/External/Models|Textures) and externalLevel (the opened level's folder; emptied when another level is opened). Names inside a pool are the plain asset names ("Crate", mesh "Crate#0"), so a level may hold an asset named like a project one.
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
