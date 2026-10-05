#include "RenderStats.h"
#include "Scripting.h"
#include <glm/gtc/quaternion.hpp>
#include <cstdio>
#include "Entity.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include "ResourceLoader.h"
#include "Globals.h"
#include <stdexcept>
#include <cmath>

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
	this->refreshBounds();
}

void Entity::setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_)
{
	checkNoInstance(*this);
	this->instanceId = Loading::addSAInstance(shaderId_, rMesh_, nullptr);
	this->sourceEngine = SA_NINJA;
	this->refreshBounds();
}

void Entity::setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey& rTextures_)
{
	checkNoInstance(*this);
	this->instanceId = Loading::addONEInstance(shaderId_, rMesh_, &rTextures_);
	this->sourceEngine = STH2005_ONE;
	this->refreshBounds();
}

void Entity::setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_)
{
	checkNoInstance(*this);
	this->instanceId = Loading::addONEInstance(shaderId_, rMesh_, nullptr);
	this->sourceEngine = STH2005_ONE;
	this->refreshBounds();
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
	this->refreshBounds();
}

void Entity::switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_)
{
	getActiveONEInstance(*this).switchMeshPart(rPrevious_, rDesired_);
	this->refreshBounds();
}

void Entity::changeActiveTexture(const STH2005::TextureKey& rKey_)
{
	getActiveONEInstance(*this).changeActiveTexture(rKey_);
}

/* Makes an SA instance draw another mesh with its textures (Loading::changeSAMesh); its motion stops, so play one after. */
void Entity::changeSAMesh(const SAModelKey& rKey_)
{
	if(this->instanceId == UninitializedPoolId || this->sourceEngine != SA_NINJA)
	{
		throw std::runtime_error("Entity: has no SA instance.");
	}
	Loading::changeSAMesh(this->instanceId, rKey_);
	this->refreshBounds();
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

void Entity::setONEPose(const std::vector<Graphics::PoseRequest>& rLayers_)
{
	getActiveONEInstance(*this).setPose(rLayers_);
}

void Entity::setPosition(const glm::vec3& rPosition_)
{
	this->position = rPosition_;
	this->previousPosition = rPosition_;
	this->stagingPosition = rPosition_;
	this->transformDirty = true;
}

void Entity::moveTo(const glm::vec3& rPosition_)
{
	this->position = rPosition_;
	this->stagingPosition = rPosition_;
	this->transformDirty = true;
}

glm::vec3 Entity::renderPosition() const
{
	const GameplaySystem& rSystem = Globals::Game::gameplaySystem;
	if(rSystem.extrapolate)
	{
		/*
		A body on the floor is carried on by the move it actually made last tick, not by its velocity: a script that presses it into the floor (Shadow's ground gravity, about 0.2 units a tick) or keeps its velocity in the slope's plane
		sets a velocity the collision then cancels in part, and extrapolating by it sank the model into the floor and back every tick (observed in a log: velocity y of -48 against a move of -36 a second, and -11 at rest).
		*/
		glm::vec3 velocity = (this->position - this->previousPosition) * rSystem.tickRate;
		if(this->physicsId != UninitializedPoolId && !Globals::Physics::bodies[this->physicsId].grounded) velocity = Globals::Physics::bodies[this->physicsId].velocity;
		return this->position + velocity * (rSystem.interpolationAlpha() * rSystem.tickTime());
	}
	if(this->previousPosition == this->position) return this->position;
	return glm::mix(this->previousPosition, this->position, rSystem.interpolationAlpha());
}

/* The rotation the same way: the turn of the last tick carried on (extrapolation, t past 1) or the way between the last two orientations. Orientations are rotation matrices, so they go through quaternions. */
glm::mat4 Entity::renderRotation() const
{
	if(this->previousRotation == this->rotation) return this->rotation;
	const GameplaySystem& rSystem = Globals::Game::gameplaySystem;
	glm::quat from = glm::quat_cast(glm::mat3(this->previousRotation));
	glm::quat to = glm::quat_cast(glm::mat3(this->rotation));
	return glm::mat4_cast(glm::normalize(glm::slerp(from, to, (rSystem.extrapolate ? 1.0f : 0.0f) + rSystem.interpolationAlpha())));
}

void Entity::setRotation(const glm::mat4& rRotation_)
{
	this->rotation = rRotation_;
	this->previousRotation = rRotation_;
	this->transformDirty = true;
}

void Entity::turnTo(const glm::mat4& rRotation_)
{
	this->rotation = rRotation_;
	this->transformDirty = true;
}

void Entity::update(float deltaTime_)
{
	if(this->instanceId == UninitializedPoolId) return;

	/* Gameplay only says what it wants; the instance changes here, once a frame, and only if the wish changed. A motion that can't be played is reported once (it counts as applied). */
	bool baseChanged = this->animationIntent != this->appliedBase;
	if(baseChanged)
	{
		this->appliedBase = this->animationIntent;
		this->baseFrame = 0.0f;
		this->baseFrameCount = 0;
	}
	const bool blinking = this->blinkIntent.pSAMotion != nullptr && this->sourceEngine == SA_NINJA;
	const AnimationIntent& rWanted = blinking ? this->blinkIntent : this->animationIntent;
	if(rWanted != this->appliedAnimation)
	{
		this->appliedAnimation = rWanted;
		try
		{
			if(rWanted.pSAMotion != nullptr && this->sourceEngine == SA_NINJA)
			{
				if(rWanted.pSAMesh != nullptr) this->changeSAMesh(*rWanted.pSAMesh);
				this->playSAMotion(*rWanted.pSAMotion);
				if(!blinking) Graphics::getSAInstance(this->instanceId).animationFrame = this->baseFrame;
			}
			else if(rWanted.pONEMotion != nullptr && this->sourceEngine == STH2005_ONE) this->playONEMotion(*rWanted.pONEMotion, rWanted.loop);
		}
		catch(const std::runtime_error& rError)
		{
			std::fprintf(stderr, "Entity: could not play the wanted motion: %s\n", rError.what());
		}
	}
	switch(this->sourceEngine)
	{
	case SA_NINJA:
	{
		Graphics::SAInstance& rInstance = Graphics::getSAInstance(this->instanceId);
		rInstance.framesPerSecond = this->animationRate;
		rInstance.update(deltaTime_);
		if(!blinking)
		{
			this->baseFrame = rInstance.animationFrame;
			if(rInstance.currentSAAnimation != UninitializedPoolId) this->baseFrameCount = Graphics::saMotions[rInstance.currentSAAnimation].frameCount;
		}
		else if(this->baseFrameCount != 0) this->baseFrame = std::fmod(this->baseFrame + deltaTime_ * rInstance.playbackRate(), static_cast<float>(this->baseFrameCount));
		break;
	}
	case STH2005_ONE:
		Graphics::getONEInstance(this->instanceId).speed = this->animationRate > 0.0f ? this->animationRate : 1.0f;
		Graphics::getONEInstance(this->instanceId).update(deltaTime_);
		break;
	}
}

Graphics::BoundingSphere Entity::localBounds()
{
	Graphics::BoundingSphere bounds;
	if(this->instanceId == UninitializedPoolId) return bounds;
	if(this->sourceEngine == SA_NINJA)
	{
		Graphics::SAMesh& rMesh = Graphics::saMeshes[Graphics::getSAInstance(this->instanceId).saMeshId];
		rMesh.computeBounds();
		bounds = rMesh.bounds;
	}
	else
	{
		const Graphics::ONEInstance& rInstance = Graphics::getONEInstance(this->instanceId);
		Graphics::RenderWareMesh& rMesh = Graphics::oneArchives[rInstance.archiveId].meshes[rInstance.meshIndex];
		rMesh.computeBounds();
		bounds = rMesh.bounds;

		/* A ONE instance turns the model by RenderWareYawDegrees about y before the entity's transform. */
		bounds.center = glm::vec3(glm::rotate(glm::mat4(1.0f), glm::radians(Graphics::RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::vec4(bounds.center, 1.0f));
	}
	return bounds;
}

/* The smallest sphere around both (one inside the other gives the bigger). */
static Graphics::BoundingSphere enclose(const Graphics::BoundingSphere& rA_, const Graphics::BoundingSphere& rB_)
{
	float distance = glm::distance(rA_.center, rB_.center);
	if(distance + rB_.radius <= rA_.radius) return rA_;
	if(distance + rA_.radius <= rB_.radius) return rB_;
	Graphics::BoundingSphere result;
	result.radius = (distance + rA_.radius + rB_.radius) * 0.5f;
	result.center = rA_.center + (rB_.center - rA_.center) * ((result.radius - rA_.radius) / distance);
	result.valid = true;
	return result;
}

void Entity::refreshBounds()
{
	Graphics::BoundingSphere result = this->localBounds();
	result.valid = true;
	if(this->collisionId != UninitializedPoolId)
	{
		Graphics::BoundingSphere shapes;
		shapes.valid = true;
		for(const Physics::CollisionShape& rShape : Globals::Physics::collisions[this->collisionId].shapes)
		{
			shapes.radius = std::max(shapes.radius, glm::length(rShape.center) + rShape.a + rShape.b + rShape.c);
		}
		result = enclose(result, shapes);
	}
	this->bounds = result;
	Globals::Game::spatialIndex.dirty = true;
}

bool Entity::withinRange(const glm::vec3& rCameraPosition_, float maxDistance_)
{
	glm::vec3 center = this->position + glm::mat3(this->rotation) * this->bounds.center;
	return glm::distance(center, rCameraPosition_) - this->bounds.radius <= maxDistance_;
}

/*
Draws the entity unless it is out of sight: the model's bounding sphere (from its rest pose, with a margin) is moved by the entity's transform and tested against the view frustum and against maxDistance_
from the camera. The test uses the transform of the frame that is drawn (renderPosition and renderRotation).
*/
void Entity::draw(const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCameraPosition_, float maxDistance_)
{
	if(this->instanceId == UninitializedPoolId) return;
	glm::vec3 renderPosition = this->renderPosition();
	glm::mat4 renderRotation = this->renderRotation();
	if(this->transformDirty || renderPosition != this->renderedPosition || renderRotation != this->renderedRotation)
	{
		this->transform = glm::translate(glm::mat4(1.0f), renderPosition) * renderRotation;
		this->renderedPosition = renderPosition;
		this->renderedRotation = renderRotation;
		this->transformDirty = false;
	}

	Graphics::BoundingSphere bounds = this->localBounds();
	glm::vec3 center = glm::vec3(this->transform * glm::vec4(bounds.center, 1.0f));
	if(glm::distance(center, rCameraPosition_) - bounds.radius > maxDistance_) return;
	if(!sphereInFrustum(rPlanes_, center, bounds.radius)) return;

	Graphics::renderStats.entitiesDrawn++;
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
	Globals::Physics::collisions[this->collisionId].entityId = this->selfId;
	this->refreshBounds();
}

void Entity::setSACollisionShapes(const std::vector<SACollisionShapesKey>& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addSAShapes(rShapes_);
	Globals::Physics::collisions[this->collisionId].entityId = this->selfId;
	this->refreshBounds();
}

void Entity::setONECollisionShapes(const STH2005::CollisionKey& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addONEShapes(rShapes_);
	Globals::Physics::collisions[this->collisionId].entityId = this->selfId;
	this->refreshBounds();
}

void Entity::setONECollisionShapes(const std::vector<STH2005::CollisionKey>& rShapes_)
{
	if(this->collisionId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a collision.");
	}
	this->collisionId = Loading::addONEShapes(rShapes_);
	Globals::Physics::collisions[this->collisionId].entityId = this->selfId;
	this->refreshBounds();
}

void Entity::setPhysics(float weight_)
{
	if(this->physicsId != UninitializedPoolId)
	{
		throw std::runtime_error("Entity: already has a physics.");
	}
	this->physicsId = Globals::Physics::bodies.add(Physics::PhysicsBody{weight_});
	Globals::Game::spatialIndex.dirty = true;
}

void Entity::markForDestroy()
{
	Globals::Game::markedForDestroy.add(this->selfId);
}

void Entity::destroy()
{
	if(this->scriptStateRef != 0)
	{
		Scripting::releaseState(this->scriptStateRef);
		this->scriptStateRef = 0;
	}
	if(this->stateId != UninitializedPoolId)
	{
		Globals::Game::entityStates.remove(this->stateId);
		this->stateId = UninitializedPoolId;
	}
	if(this->behaviorSetId != UninitializedPoolId)
	{
		Globals::Game::entityBehaviors.remove(this->behaviorSetId);
		this->behaviorSetId = UninitializedPoolId;
	}
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
