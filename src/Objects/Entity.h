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
An SA instance plays a motion with playSAMotion (loops), a ONE instance with playONEMotion (loops, or plays once and holds the last frame with loop_ = false); update advances them.
Removing the entity from its pool (destroy) removes the instance, its collision and its physics.
physicsId is the PoolId of the entity's Physics::PhysicsBody in Globals::Physics::bodies (UninitializedPoolId = none, the entity is then not moved by the physics and collision systems); setPhysics creates it.
stagingPosition is where the physics system wants the entity to go this frame: the collision system checks it, may change it, and then sets position to it (see PhysicsSystem.h, CollisionSystem.h).
The new fields come after the position ones so Entity{engine, instanceId, position} (API.cpp) still initializes position.
collisionId is the PoolId of the entity's collision in Globals::Physics::collisions (UninitializedPoolId = none); the entity owns it.
setSACollisionShapes gives it the shapes of an SADX collision table (Physics::CollisionShape); the collision system moves an entity by its sphere (form 0) and capsule (form 6) shapes, and by a segment if it has none of those; the lines view draws them all. setONECollisionShapes does the same for a Shadow .CCL entry (readCCL, whose mapping to shapes is a hypothesis); Shadow himself has no .CCL.
Only one of the two setters can be called, once: an entity has a single instance. Until then instanceId is
UninitializedPoolId and the entity is not drawn.
The matrix of position and rotation (translate * rotation; rotation is a world-space orientation matrix, identity by default, set with setRotation: the caller decides the convention) is kept in transform and rebuilt by draw only after the position or rotation changed: setPosition and setRotation mark it
dirty (a new entity starts dirty). Change them with the setters, not by writing the fields.
*/
struct Entity
{
	SourceEngineEnum sourceEngine = SA_NINJA;
	PoolId instanceId = UninitializedPoolId;
	glm::vec3 position;
	glm::mat4 transform;
	bool transformDirty = true;
	PoolId collisionId = UninitializedPoolId;
	PoolId physicsId = UninitializedPoolId;
	glm::vec3 stagingPosition = glm::vec3(0.0f);
	glm::mat4 rotation = glm::mat4(1.0f);

	void setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey& rTextures_);
	void setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_);
	void setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey& rTextures_);
	void setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_);
	void changeActiveMesh(const STH2005::ModelKey& rKey_);
	void switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_);
	void changeActiveTexture(const STH2005::TextureKey& rKey_);
	void playSAMotion(const SAMotionKey& rKey_);
	void playONEMotion(const STH2005::MotionKey& rKey_, bool loop_ = true);
	void setPhysics(float weight_);
	void setSACollisionShapes(const SACollisionShapesKey& rShapes_);
	void setSACollisionShapes(const std::vector<SACollisionShapesKey>& rShapes_);
	void setONECollisionShapes(const STH2005::CollisionKey& rShapes_);
	void setONECollisionShapes(const std::vector<STH2005::CollisionKey>& rShapes_);
	void setPosition(const glm::vec3& rPosition_);
	void setRotation(const glm::mat4& rRotation_);
	void update(float deltaTime_);
	void draw();
	void destroy();
};
