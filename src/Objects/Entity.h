#pragma once
#include "GPU_API.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include "PoolId.h"
#include "Constants.h"
#include <glm/gtc/matrix_transform.hpp>

/* Which engine's data the entity's instance comes from; selects the instance pool instanceId refers to. */
enum SourceEngineEnum
{
	SA_NINJA,
	STH2005_ONE
};

/*
An entity is created first and gets its instance afterwards, with setSAGPUInstance or setONEGPUInstance.
The instance is created from resource keys (ResourceKeys.h): whatever the keys name that is not loaded yet is loaded
then, and it stays loaded while any instance uses it. The textures are optional (the overload without them is untextured).
A ONE instance can switch to another mesh or .TXD entry of its archive later with changeActiveMesh / changeActiveTexture,
and replace one part of its mesh with switchMeshPart.
An SA instance plays a motion with playSAMotion, a ONE instance with playONEMotion (both loop); update advances them.
Removing the entity from its pool (destroy) removes the instance.
Only one of the two setters can be called, once: an entity has a single instance. Until then instanceId is
UninitializedPoolId and the entity is not drawn.
*/
struct Entity
{
	SourceEngineEnum sourceEngine = SA_NINJA;
	PoolId instanceId = UninitializedPoolId;
	glm::vec3 position;

	void setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey& rTextures_);
	void setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_);
	void setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey& rTextures_);
	void setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_);
	void changeActiveMesh(const STH2005::ModelKey& rKey_);
	void switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_);
	void changeActiveTexture(const STH2005::TextureKey& rKey_);
	void playSAMotion(const SAMotionKey& rKey_);
	void playONEMotion(const STH2005::MotionKey& rKey_);
	void update(float deltaTime_);
	void draw();
	void destroy();
};
