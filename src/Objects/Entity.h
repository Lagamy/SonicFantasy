#pragma once
#include "GPU_API.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include "PoolId.h"
#include <array>
#include <vector>
#include "Constants.h"

namespace Graphics
{
struct PoseRequest;
}
#include "BoundingSphere.h"
#include <glm/gtc/matrix_transform.hpp>

/* Which engine's data the entity's instance comes from; selects the instance pool instanceId refers to. */
enum SourceEngineEnum
{
	SA_NINJA,
	STH2005_ONE
};

/*
The motion an entity wants to play, set by gameplay (changeAnimation in API.h) and applied by Entity::update once a frame, only when it differs from the one last applied. At most one of the two is set (the
engine of the entity's instance says which). The keys are not copied: they are static data.
*/
struct AnimationIntent
{
	const SAMotionKey* pSAMotion = nullptr;
	const STH2005::MotionKey* pONEMotion = nullptr;
	bool loop = true;
	/* An SA entity: the mesh to draw with the motion (switched first, since a motion animates the nodes of one model), nullptr = keep the current one. */
	const SAModelKey* pSAMesh = nullptr;

	bool operator==(const AnimationIntent&) const = default;
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
selfId is the entity's own PoolId in Globals::Game::entities (set by addEntity), so the entity can hand it to what needs it: its collision (Collision::entityId) and markForDestroy.
animationIntent is the motion gameplay wants (changeAnimation); update plays it when it differs from appliedAnimation, the last one it played.
stateId is its EntityState in Globals::Game::entityStates (UninitializedPoolId = none; made by entityStateOf in API.h, removed by destroy).
behaviorSetId is the entity's record in Globals::Game::entityBehaviors (UninitializedPoolId = no behaviours): its behaviours, stacked in the order they were given and run in that order by the GameplaySystem every gameplay tick; add one with giveEntityBehavior (API.h), which makes the record. destroy removes it.
Only one of the two setters can be called, once: an entity has a single instance. Until then instanceId is
UninitializedPoolId and the entity is not drawn.
previousPosition and previousRotation are where the entity was at the start of the latest gameplay tick. What is drawn (and shown by the debug views) is renderPosition and renderRotation, by how far the frame is
into the next tick (GameplaySystem::interpolationAlpha): extrapolated from the latest tick by the entity's velocity (a physics entity's body, otherwise the last tick's change) so that it is not a tick behind, or, with
GameplaySystem::extrapolate off, interpolated between the last two ticks. The rotation is turned the same way between the two orientations (a quaternion slerp). setPosition and setRotation place the entity (a
teleport: previous and current are the same); moveTo and turnTo are the simulation's moves (physics, collisions, behaviours) and keep the previous ones.
The matrix of position and rotation (translate * rotation; rotation is a world-space orientation matrix, identity by default, set with setRotation: the caller decides the convention) is kept in transform and rebuilt by draw only after the position or rotation changed: setPosition and setRotation mark it
dirty (a new entity starts dirty). Change them with the setters, not by writing the fields.
*/
struct Entity
{
	SourceEngineEnum sourceEngine = SA_NINJA;
	PoolId instanceId = UninitializedPoolId;
	glm::vec3 position;
	glm::vec3 previousPosition = glm::vec3(0.0f);
	glm::vec3 renderedPosition = glm::vec3(0.0f);
	glm::mat4 previousRotation = glm::mat4(1.0f);
	glm::mat4 renderedRotation = glm::mat4(1.0f);
	glm::mat4 transform;
	bool transformDirty = true;
	/* Set every gameplay tick (SpatialIndex::updateActive): true while the entity is in range of the camera, false otherwise and until the first tick; an entity that is not active has no behaviours, physics or collision. */
	bool active = false;
	/* An entity with this set is never put to sleep by distance: its behaviours, physics and collision run from any distance (the player, things that must keep going). */
	bool behaviorNotCulled = false;
	PoolId collisionId = UninitializedPoolId;
	PoolId physicsId = UninitializedPoolId;
	glm::vec3 stagingPosition = glm::vec3(0.0f);
	glm::mat4 rotation = glm::mat4(1.0f);
	PoolId behaviorSetId = UninitializedPoolId;
	PoolId selfId = UninitializedPoolId;
	PoolId stateId = UninitializedPoolId;
	/* The entity's table for what its script behaviours remember (sf.state, Scripting.h): a Lua registry reference, 0 = none yet; released by destroy. */
	int scriptStateRef = 0;
	AnimationIntent animationIntent;
	AnimationIntent appliedAnimation;
	/*
	A motion drawn in place of animationIntent while it is set (sf.blink; its mesh is switched with it), like the game's ball blink: the intent's motion keeps its own clock meanwhile
	(baseFrame, in frames of baseFrameCount) and goes on from there when the blink ends, instead of restarting. appliedBase is the intent last seen, so a new one starts at frame 0.
	*/
	AnimationIntent blinkIntent;
	AnimationIntent appliedBase;
	float baseFrame = 0.0f;
	uint32_t baseFrameCount = 0;
	/* Frames a second the entity's SA motion plays at, set by gameplay (sf.animate); 0 = the default. Not part of the intent: it can change every tick without restarting the motion. */
	float animationRate = 0.0f;

	void setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey& rTextures_);
	void setSAGPUInstance(PoolId shaderId_, const SAModelKey& rMesh_);
	void setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey& rTextures_);
	void setONEGPUInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_);
	void changeActiveMesh(const STH2005::ModelKey& rKey_);
	void switchMeshPart(const STH2005::ModelPart& rPrevious_, const STH2005::ModelPart& rDesired_);
	void changeActiveTexture(const STH2005::TextureKey& rKey_);
	void changeSAMesh(const SAModelKey& rKey_);
	void playSAMotion(const SAMotionKey& rKey_);
	void playONEMotion(const STH2005::MotionKey& rKey_, bool loop_ = true);
	/* A pose of motions mixed by weight at given frames, in place of the main skeleton's playing motions (ONEInstance::setPose); an empty list ends it. ONE instances only. */
	void setONEPose(const std::vector<Graphics::PoseRequest>& rLayers_);
	void setPhysics(float weight_);
	void setSACollisionShapes(const SACollisionShapesKey& rShapes_);
	void setSACollisionShapes(const std::vector<SACollisionShapesKey>& rShapes_);
	void setONECollisionShapes(const STH2005::CollisionKey& rShapes_);
	void setONECollisionShapes(const std::vector<STH2005::CollisionKey>& rShapes_);
	void setPosition(const glm::vec3& rPosition_);
	void moveTo(const glm::vec3& rPosition_);
	glm::vec3 renderPosition() const;
	void setRotation(const glm::mat4& rRotation_);
	void turnTo(const glm::mat4& rRotation_);
	glm::mat4 renderRotation() const;
	void update(float deltaTime_);
	/* The bounding sphere of the entity's model in its own space, with the engine's own turn applied (invalid without an instance); computed once per mesh. Used to draw. */
	Graphics::BoundingSphere localBounds();
	/*
	The sphere the spatial index uses for the entity (what is in range of the camera and so active), in its own space: the model's sphere and a sphere around the collision shapes (about the origin, a conservative
	|center| + a + b + c each, so it holds whatever the rotation), enclosed in one; a point if it has neither. refreshBounds recomputes it and tells the index to rebuild: every function that attaches or changes a model,
	a collision or a physics calls it, code that sets collisionId by hand has to call it too.
	*/
	Graphics::BoundingSphere bounds;
	void refreshBounds();
	/* Whether the entity's bounding sphere, at the position and rotation of the latest gameplay tick, is within maxDistance_ of the camera: the distance half of the draw culling, for the behaviours. */
	bool withinRange(const glm::vec3& rCameraPosition_, float maxDistance_);
	void draw(const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCameraPosition_, float maxDistance_);
	void destroy();

	/* Asks for the entity to be removed: it goes into Globals::Game::markedForDestroy and the GameplaySystem removes it (destroy runs then) at the start of the next gameplay tick, so a behaviour or a callback can end its own entity safely. */
	void markForDestroy();
};
