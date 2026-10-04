#include "Entity.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "ResourceLoader.h"
#include "Globals.h"
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

void Entity::setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey& rTextures_)
{
	checkNoInstance(*this);
	this->instanceId = Loading::addONEInstance(shaderId_, rMesh_, &rTextures_);
	this->sourceEngine = STH2005_ONE;
}

void Entity::setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_)
{
	checkNoInstance(*this);
	this->instanceId = Loading::addONEInstance(shaderId_, rMesh_, nullptr);
	this->sourceEngine = STH2005_ONE;
}

static Graphics::ONEInstance& getActiveONEInstance(const Entity& rEntity_)
{
	if(rEntity_.instanceId == UninitializedPoolId || rEntity_.sourceEngine != STH2005_ONE)
	{
		throw std::runtime_error("Entity: has no ONE instance.");
	}
	return Graphics::getONEInstance(rEntity_.instanceId);
}

void Entity::changeActiveMesh(const STH2005::ModelKey& rKey_)
{
	getActiveONEInstance(*this).changeActiveMesh(rKey_);
}

void Entity::switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_)
{
	getActiveONEInstance(*this).switchMeshPart(rPrevious_, rDesired_);
}

void Entity::changeActiveTexture(const STH2005::TextureKey& rKey_)
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

void Entity::playONEMotion(const STH2005::MotionKey& rKey_, bool loop_)
{
	getActiveONEInstance(*this).playMotion(rKey_, loop_);
}

void Entity::setPosition(const glm::vec3& rPosition_)
{
	this->position = rPosition_;
	this->stagingPosition = rPosition_;
	this->transformDirty = true;
}

void Entity::setRotation(const glm::mat4& rRotation_)
{
	this->rotation = rRotation_;
	this->transformDirty = true;
}

void Entity::update(float deltaTime_)
{
	if(this->instanceId == UninitializedPoolId) return;
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::getSAInstance(this->instanceId).update(deltaTime_);
		break;
	case STH2005_ONE:
		Graphics::getONEInstance(this->instanceId).update(deltaTime_);
		break;
	}
}

void Entity::draw()
{
	if(this->instanceId == UninitializedPoolId) return;
	if(this->transformDirty)
	{
		this->transform = glm::translate(glm::mat4(1.0f), this->position) * this->rotation;
		this->transformDirty = false;
	}

	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::getSAInstance(this->instanceId).draw(this->transform);
		break;
	case STH2005_ONE:
		Graphics::getONEInstance(this->instanceId).draw(this->transform);
		break;
	}
}

/* Removes the entity's instance, which gives back its TextureSet or archive user. Called by PoolMap::remove. */
void Entity::setSACollisionShapes(const SACollisionShapesKey& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addSAShapes(rShapes_);
}

void Entity::setSACollisionShapes(const std::vector<SACollisionShapesKey>& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addSAShapes(rShapes_);
}

void Entity::setONECollisionShapes(const STH2005::CollisionKey& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addONEShapes(rShapes_);
}

void Entity::setONECollisionShapes(const std::vector<STH2005::CollisionKey>& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addONEShapes(rShapes_);
}

void Entity::setPhysics(float weight_)
{
	if(this->physicsId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a physics.");
	}
	this->physicsId = Globals::Physics::bodies.add(Physics::PhysicsBody{weight_});
}

void Entity::destroy()
{
	if(this->physicsId != UninitializedPoolId)
	{
		Globals::Physics::bodies.remove(this->physicsId);
		this->physicsId = UninitializedPoolId;
	}
	if(this->collisionId != UninitializedPoolId)
	{
		Globals::Physics::collisions.remove(this->collisionId);
		this->collisionId = UninitializedPoolId;
	}
	if(this->instanceId == UninitializedPoolId) return;
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::saInstances.remove(this->instanceId);
		break;
	case STH2005_ONE:
		Graphics::oneInstances.remove(this->instanceId);
		break;
	}
	this->instanceId = UninitializedPoolId;
}
