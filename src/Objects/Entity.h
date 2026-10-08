#pragma once
#include "GPU_API.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include "PoolId.h"
#include "TextureChoice.h"
#include "MeshChoice.h"
#include "ExternalField.h"
#include <array>
#include <map>
#include <string>
#include <vector>
#include "Constants.h"

namespace Graphics
{
struct PoseRequest;
struct ExternalModel;
}
#include "BoundingSphere.h"
#include <glm/gtc/matrix_transform.hpp>

/* Which engine's data the entity's instance comes from; selects the instance pool instanceId refers to. GLTF_FILE is a Level's (glTF levels) and an external-only entity's (setExternalOnly): it has no instance, only an external model. */
enum SourceEngineEnum
{
	SA_NINJA,
	STH2005_ONE,
	GLTF_FILE
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
cameraId is the entity's camera module, its Camera in Globals::Game::cameras (UninitializedPoolId = none; made by addCamera in API.h, removed by destroy). The camera is at the entity's position: setPosition and moveTo give it to the camera (moveCameraModule); the view direction is the camera's own. Set the position of a camera through its entity (the follow camera does), not with Camera::set alone, or the entity is left behind.
Only one of the two setters can be called, once: an entity has a single instance. Until then instanceId is
UninitializedPoolId and the entity is not drawn.
previousPosition and previousRotation are where the entity was at the start of the latest gameplay tick. What is drawn (and shown by the debug views) is renderPosition and renderRotation, by how far the frame is
into the next tick (GameplaySystem::interpolationAlpha): extrapolated from the latest tick by the entity's velocity (a physics entity's body, otherwise the last tick's change) so that it is not a tick behind, or, with
GameplaySystem::extrapolate off, interpolated between the last two ticks. The rotation is turned the same way between the two orientations (a quaternion slerp). setPosition and setRotation place the entity (a
teleport: previous and current are the same); moveTo and turnTo are the simulation's moves (physics, collisions, behaviours) and keep the previous ones.
The matrix of position, rotation and scale (translate * rotation * scale; rotation is a world-space orientation matrix, identity by default, set with setRotation: the caller decides the convention; scale is per axis of the entity's own space, (1, 1, 1) by default, set with setScale) is kept in transform and rebuilt by draw only after the position, rotation or scale changed: setPosition, setRotation and setScale mark it
dirty (a new entity starts dirty). Change them with the setters, not by writing the fields.
The model and the collision shapes of an entity without a physics body are scaled (collisionScale, Physics::scaledShape: the port's choice, whether the game's objects scale their collision is unknown); the physics body's shapes and the camera module keep their size. bounds holds the scaled model sphere.
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
	glm::vec3 scale = glm::vec3(1.0f);
	PoolId behaviorSetId = UninitializedPoolId;
	PoolId selfId = UninitializedPoolId;
	PoolId stateId = UninitializedPoolId;
	PoolId cameraId = UninitializedPoolId;
	/* The prefab the entity was made from (Globals::Game::prefabs, set by spawnPrefab; UninitializedPoolId = none, e.g. a camera): the editor groups the entity list by it. */
	PoolId prefabId = UninitializedPoolId;
	/* The texture the editor gave the entity in place of its model's own (setEntityTexture in API.h; Default = none). defaultTexture is the model's own disc texture (discTexture), remembered by the first change so Default can put it back; hasDefaultTexture says it was. */
	TextureChoice textureChoice;
	std::string defaultTexture;
	bool hasDefaultTexture = false;
	/* The same for the model (setEntityMesh in API.h): the choice, the model's own disc mesh (discMesh) remembered by the first change, and the external model drawn in place of the instance's (setOverrideModel; nullptr = the instance's). */
	MeshChoice meshChoice;
	std::string defaultMesh;
	bool hasDefaultMesh = false;
	const Graphics::ExternalModel* pOverrideModel = nullptr;
	/* External-only entity (setExternalOnly; sourceEngine GLTF_FILE): the shader it is drawn with (the GLTF one) and the external texture it takes, in place of the instance's shader and pOverrideTexture it does not have. */
	PoolId externalShaderId = UninitializedPoolId;
	Graphics::Texture* pExternalTexture = nullptr;
	/* The entity's table for what its script behaviours remember (sf.state, Scripting.h): a Lua registry reference, 0 = none yet; released by destroy. */
	int scriptStateRef = 0;
	/* The values the editor set for the external fields (ExternalField.h) of the entity's script behaviours: behaviour name -> field name -> value; a field not in it has its declared default. externalsApplied lists the behaviours whose fields Scripting::callBehavior has already written into the state table. */
	std::map<std::string, std::map<std::string, ExternalValue>> externals;
	std::vector<PoolId> externalsApplied;
	/* Values of the level file that no declared field takes (the behaviour or field is not declared when the level loads, or the value does not fit its type), as their JSON text, behaviour name -> field name: kept so that saving the level does not lose them (Loading::saveLevelEdits writes them back). */
	std::map<std::string, std::map<std::string, std::string>> externalsKept;
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
	/* The disc texture the instance draws with: an SA instance's TextureSet (its .gvm path), a ONE instance's .TXD entry name; empty = untextured. Throws without an instance. */
	std::string discTexture() const;
	/* Draws with another disc texture (empty = untextured; Loading::changeSATextureSet / changeActiveTexture, which throw when it can't, leaving the entity as it was). A ONE name is an entry of the instance's own archive. */
	void setDiscTexture(const std::string& rName_);
	/* The .TXD entries of the instance's archive, for a ONE instance (empty for an SA one). */
	std::vector<std::string> discTextureOptions() const;
	/* The disc model the instance draws: an SA instance's SAMesh name ("<rel>@0x<offset>"), a ONE instance's mesh name in its archive. Throws without an instance. */
	std::string discMesh() const;
	/*
	Draws another disc model: an SA name is the form discMesh gives (Loading::changeSAMesh: the entity's texture set must hold the texture ids the model uses; a motion in progress is dropped and gameplay has to ask for one again), a ONE name is a mesh the archive
	has built already (the entity's own) or "(mesh)/<entry>" for a .DFF entry of the archive, built as a static model. Throws when it can't, leaving the entity as it was.
	*/
	void setDiscMesh(const std::string& rName_);
	/* The .DFF entries of the instance's archive, for a ONE instance (empty for an SA one). */
	std::vector<std::string> discMeshOptions() const;
	/*
	Makes the entity one without a disc instance: it is drawn only by an external model (setOverrideModel, nothing is drawn until one is set) with an external texture (setOverrideTexture) and this shader (the GLTF one: unlit, linear vertex colour, as the
	external models are written), independent of SADX and Shadow data. sourceEngine becomes GLTF_FILE. Throws if the entity already has an instance. It has no disc model or texture: setEntityMesh / setEntityTexture refuse a Disc choice for it.
	*/
	void setExternalOnly(PoolId shaderId_);
	bool isExternalOnly() const { return this->sourceEngine == GLTF_FILE; }
	/* Whether setEntityMesh and setEntityTexture have something to change: an instance, or the external-only drawing. */
	bool hasModelSlot() const { return this->instanceId != UninitializedPoolId || this->isExternalOnly(); }
	/* Draws this external model in place of the instance's (nullptr = the instance's) and refreshes the bounds. The model is not owned: it has to stay alive while set (the external pools do). */
	void setOverrideModel(const Graphics::ExternalModel* pModel_);
	/* Draws every textured submesh with this texture (nullptr = the instance's own textures). The texture is not owned: it has to stay alive while set (the external pools do). */
	void setOverrideTexture(Graphics::Texture* pTexture_);
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
	/* Puts the camera module (if any) at the entity's position. */
	void moveCameraModule();
	glm::vec3 renderPosition() const;
	void setRotation(const glm::mat4& rRotation_);
	void turnTo(const glm::mat4& rRotation_);
	glm::mat4 renderRotation() const;
	/* Sets the scale of the model (see above) and refreshes the bounds. */
	void setScale(const glm::vec3& rScale_);
	/* The scale the entity's collision shapes get in the contacts and the lines view: its scale, or (1, 1, 1) for an entity with a physics body. */
	glm::vec3 collisionScale() const;
	void update(float deltaTime_);
	/* The bounding sphere of the entity's model in its own space, with the engine's own turn applied (invalid without an instance); computed once per mesh. Used to draw. */
	Graphics::BoundingSphere localBounds();

	/* Editor picking: whether the ray (world space, unit direction) crosses a triangle of the model (both sides; an SA model as posed in the last drawn frame, a ONE model in its rest pose); distance_ is along the ray to the nearest one. hasTriangles_ is false when the model has none readable (no model, or a custom vertex layout), then the result is false and the caller falls back to bounds. */
	bool rayHitsModel(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rDistance_, bool& rHasTriangles_);
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
