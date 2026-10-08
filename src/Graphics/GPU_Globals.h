#pragma once 
#include <string>
#include <unordered_map>
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
	inline PoolMap<BasicMesh> basicMeshes = ("Meshes"); 
	inline PoolMap<Texture> textures = ("Textures");
	inline PoolMap<VertexLayout> vertexLayouts = ("VertexLayouts");
	inline ExternalPools externalProject("ProjectMeshes", "ProjectTextures", "ProjectModels");
	inline ExternalPools externalLevel("LevelMeshes", "LevelTextures", "LevelModels");
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
