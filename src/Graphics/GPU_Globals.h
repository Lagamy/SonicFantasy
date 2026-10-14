#pragma once 
#include <string>
#include <unordered_map>
#include <memory>
#include "SAInstance.h"
#include "ONEInstance.h"
#include "SALevelInstance.h"
#include "ONELevelInstance.h"
#include "GLTFLevelInstance.h"
#include "ONE.h"
#include "Renderer.h"
#ifdef EDITOR
#include "Editor.h"
#endif
#include "DashTrail.h"
#include "EffectBalls.h"
#include "DashSpheres.h"
#include "SonicTrail.h"
#include "Pool.h"
#include "Shader.h"
#include "BasicMesh.h"
#include "ExternalModel.h"
#include "Texture.h"
#include "SAMesh.h"
#include "SAAnimation.h"
#include "TextureSet.h"
#include "TextureBinding.h"
#include "PoolMap.h"

namespace Graphics 
{
	inline Renderer renderer;
	inline DashTrail dashTrail;
	inline EffectBalls effectBalls;
	inline DashSpheres dashSpheres;
	inline SonicTrail sonicTrail;
	inline PoolMap<BasicMesh> basicMeshes = ("Meshes"); 
	inline PoolMap<Texture> textures = ("Textures");
	inline PoolMap<VertexLayout> vertexLayouts = ("VertexLayouts");
	inline ExternalPools externalProject("ProjectMeshes", "ProjectTextures", "ProjectModels");
	/*
	The level scope of the External assets: the pools of every loaded level by its name (made by Loading::scanExternalAssets, dropped by Loading::forgetExternalAssets when the level is destroyed, so an entity's pointer into
	its level's pools stays valid while the entity exists). externalNoLevel is the empty set used while no level was scanned; externalLevel() is the set of the active scope (the level scanned last).
	*/
	inline std::unordered_map<std::string, std::unique_ptr<ExternalPools>> externalLevels;
	inline ExternalPools externalNoLevel("LevelMeshes", "LevelTextures", "LevelModels");
	inline ExternalPools* pExternalLevel = &externalNoLevel;
	inline ExternalPools& externalLevel() { return *pExternalLevel; }
	inline PoolMap<TextureSet> textureSets = ("TextureSets");
	inline Pool<TextureBinding> textureBindings = ("TextureBindings");
	inline PoolMap<Shader> shaders = ("Shaders");
	inline PoolMap<SAMesh> saMeshes = ("SAMeshes");
	inline std::unordered_map<std::string, RelModule> relModules;
	inline PoolMap<SAMotion> saMotions = ("SAMotions");
	inline Pool<SAInstance> saInstances = ("SAInstances"); 
	inline PoolMap<STH2005::ONE> oneArchives = ("ONEArchives");
	inline Pool<ONEInstance> oneInstances = ("ONEInstances");
	inline Pool<SALevelInstance> saLevelInstances = ("SALevelInstances");
	inline Pool<ONELevelInstance> oneLevelInstances = ("ONELevelInstances");
	inline Pool<GLTFLevelInstance> gltfLevelInstances = ("GLTFLevelInstances");
}
