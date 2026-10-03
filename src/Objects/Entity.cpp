#include "Entity.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "ResourceLoader.h"
#include <stdexcept>

static void checkNoInstance(const Entity& rEntity_)
{
	if(rEntity_.instanceId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has an instance.");
	}
}

void Entity::setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey& rTextures_)
{
	checkNoInstance(*this);
	this->instanceId = Loading::addSAInstance(shaderId_, rMesh_, &rTextures_);
	this->sourceEngine = SA_NINJA;
}

void Entity::setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_)
{
	checkNoInstance(*this);
	this->instanceId = Loading::addSAInstance(shaderId_, rMesh_, nullptr);
	this->sourceEngine = SA_NINJA;
}

void Entity::setONEGPUInstance(PoolId shaderId_, const STH2005ModelKey& rMesh_, const STH2005TextureKey& rTextures_)
{
	checkNoInstance(*this);
	this->instanceId = Loading::addONEInstance(shaderId_, rMesh_, &rTextures_);
	this->sourceEngine = STH2005;
}

void Entity::setONEGPUInstance(PoolId shaderId_, const STH2005ModelKey& rMesh_)
{
	checkNoInstance(*this);
	this->instanceId = Loading::addONEInstance(shaderId_, rMesh_, nullptr);
	this->sourceEngine = STH2005;
}

static Graphics::ONEInstance& getActiveONEInstance(const Entity& rEntity_)
{
	if(rEntity_.instanceId == UninitializedPoolId || rEntity_.sourceEngine != STH2005)
	{
		throw std::runtime_error("Entity: has no ONE instance.");
	}
	return Graphics::getONEInstance(rEntity_.instanceId);
}

void Entity::changeActiveMesh(const STH2005ModelKey& rKey_)
{
	getActiveONEInstance(*this).changeActiveMesh(rKey_);
}

void Entity::switchMeshPart(const STH2005ModelPart& rPrevious_, const STH2005ModelPart& rDesired_)
{
	getActiveONEInstance(*this).switchMeshPart(rPrevious_, rDesired_);
}

void Entity::changeActiveTexture(const STH2005TextureKey& rKey_)
{
	getActiveONEInstance(*this).changeActiveTexture(rKey_);
}

void Entity::playSAMotion(const SAMotionKey& rKey_)
{
	if(this->instanceId == UninitializedPoolId || this->sourceEngine != SA_NINJA)
	{
		throw std::runtime_error("Entity: has no SA instance.");
	}
	Loading::playSAMotion(this->instanceId, rKey_);
}

void Entity::playONEMotion(const STH2005MotionKey& rKey_)
{
	getActiveONEInstance(*this).playMotion(rKey_);
}

void Entity::update(float deltaTime_)
{
	if(this->instanceId == UninitializedPoolId) return;
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::getSAInstance(this->instanceId).update(deltaTime_);
		break;
	case STH2005:
		Graphics::getONEInstance(this->instanceId).update(deltaTime_);
		break;
	}
}

void Entity::draw()
{
	if(this->instanceId == UninitializedPoolId) return;
	glm::mat4 base = glm::translate(glm::mat4(1.0f), this->position);

	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::getSAInstance(this->instanceId).draw(base);
		break;
	case STH2005:
		Graphics::getONEInstance(this->instanceId).draw(base);
		break;
	}
}

/* Removes the entity's instance, which gives back its TextureSet or archive user. Called by PoolMap::remove. */
void Entity::destroy()
{
	if(this->instanceId == UninitializedPoolId) return;
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::saInstances.remove(this->instanceId);
		break;
	case STH2005:
		Graphics::oneInstances.remove(this->instanceId);
		break;
	}
	this->instanceId = UninitializedPoolId;
}
