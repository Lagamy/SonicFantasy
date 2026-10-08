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
#include <algorithm>
#include <format>
#include <set>

static void checkNoInstance(const Entity& rEntity_)
{
	if(rEntity_.instanceId != UninitializedPoolId || rEntity_.isExternalOnly())
	{
		throw std::runtime_error("Entity: already has an instance or is external-only.");
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

static Graphics::SAInstance& getActiveSAInstance(const Entity& rEntity_)
{
	if(rEntity_.instanceId == UninitializedPoolId || rEntity_.sourceEngine != SA_NINJA)
	{
		throw std::runtime_error("Entity: has no SA instance.");
	}
	return Graphics::getSAInstance(rEntity_.instanceId);
}

std::string Entity::discTexture() const
{
	if(this->sourceEngine == SA_NINJA)
	{
		const Graphics::SAInstance& rInstance = getActiveSAInstance(*this);
		return rInstance.textureSetId == UninitializedPoolId ? std::string() : Graphics::textureSets.getName(rInstance.textureSetId);
	}
	const Graphics::ONEInstance& rInstance = getActiveONEInstance(*this);
	if(rInstance.textureDictionaryIndex == STH2005::NoIndex) return std::string();
	return Graphics::oneArchives[rInstance.archiveId].textureDictionaryNames[rInstance.textureDictionaryIndex];
}

void Entity::setDiscTexture(const std::string& rName_)
{
	if(this->sourceEngine == SA_NINJA)
	{
		getActiveSAInstance(*this);
		SATextureSetKey key{rName_.c_str()};
		Loading::changeSATextureSet(this->instanceId, rName_.empty() ? nullptr : &key);
		return;
	}
	Graphics::ONEInstance& rInstance = getActiveONEInstance(*this);
	if(rName_.empty()) return;
	STH2005::TextureKey key{STH2005::ArchiveKey{Graphics::oneArchives.getName(rInstance.archiveId).c_str()}, rName_.c_str()};
	rInstance.changeActiveTexture(key);
}

std::vector<std::string> Entity::discTextureOptions() const
{
	std::vector<std::string> names;
	if(this->sourceEngine != STH2005_ONE) return names;
	const STH2005::ONE& rArchive = Graphics::oneArchives[getActiveONEInstance(*this).archiveId];
	for(const STH2005::ONEEntry& rEntry : rArchive.entries)
	{
		if(rEntry.name.size() > 4 && (rEntry.name.ends_with(".TXD") || rEntry.name.ends_with(".txd"))) names.push_back(rEntry.name);
	}
	return names;
}

void Entity::setOverrideTexture(Graphics::Texture* pTexture_)
{
	if(this->isExternalOnly())
	{
		this->pExternalTexture = pTexture_;
		return;
	}
	if(this->sourceEngine == SA_NINJA) getActiveSAInstance(*this).pOverrideTexture = pTexture_;
	else getActiveONEInstance(*this).pOverrideTexture = pTexture_;
}

/* A string that lives for the rest of the run: a Shadow mesh keeps the strings of the recipe it was built from (STH2005::ModelPart). */
static const char* keepString(const std::string& rText_)
{
	static std::set<std::string> kept;
	return kept.insert(rText_).first->c_str();
}

std::string Entity::discMesh() const
{
	if(this->sourceEngine == SA_NINJA) return Graphics::saMeshes.getName(getActiveSAInstance(*this).saMeshId);
	const Graphics::ONEInstance& rInstance = getActiveONEInstance(*this);
	return Graphics::oneArchives[rInstance.archiveId].meshNames[rInstance.meshIndex];
}

void Entity::setDiscMesh(const std::string& rName_)
{
	if(this->sourceEngine == SA_NINJA)
	{
		getActiveSAInstance(*this);
		size_t at = rName_.rfind("@0x");
		if(at == std::string::npos) throw std::runtime_error(std::format("'{}' is not a SADX model (<rel>@0x<offset>)", rName_));
		std::string rel = rName_.substr(0, at);
		SAModelKey key{rel.c_str(), static_cast<uint32_t>(std::stoul(rName_.substr(at + 3), nullptr, 16))};
		std::string previous = this->discMesh();
		Loading::changeSAMesh(this->instanceId, key);
		if(this->discMesh() != previous)
		{
			this->animationIntent = AnimationIntent{};
			this->appliedAnimation = AnimationIntent{};
			this->blinkIntent = AnimationIntent{};
			this->appliedBase = AnimationIntent{};
		}
		this->refreshBounds();
		return;
	}
	Graphics::ONEInstance& rInstance = getActiveONEInstance(*this);
	const STH2005::ONE& rArchive = Graphics::oneArchives[rInstance.archiveId];
	STH2005::ModelKey key{STH2005::ArchiveKey{keepString(Graphics::oneArchives.getName(rInstance.archiveId))}, nullptr, {}};
	auto found = rArchive.meshByName.find(rName_);
	if(found != rArchive.meshByName.end())
	{
		key.name = keepString(rName_);
		key.parts = rArchive.meshParts[found->second];
	}
	else if(rName_.starts_with("(mesh)/"))
	{
		key.name = keepString(rName_);
		key.parts = {STH2005::ModelPart{keepString(rName_.substr(7)), nullptr, nullptr}};
	}
	else throw std::runtime_error(std::format("'{}' is not a model of this archive", rName_));
	rInstance.changeActiveMesh(key);
	this->refreshBounds();
}

std::vector<std::string> Entity::discMeshOptions() const
{
	std::vector<std::string> names;
	if(this->sourceEngine != STH2005_ONE) return names;
	const STH2005::ONE& rArchive = Graphics::oneArchives[getActiveONEInstance(*this).archiveId];
	for(const STH2005::ONEEntry& rEntry : rArchive.entries)
	{
		if(rEntry.name.size() > 4 && (rEntry.name.ends_with(".DFF") || rEntry.name.ends_with(".dff"))) names.push_back(rEntry.name);
	}
	return names;
}

void Entity::setExternalOnly(PoolId shaderId_)
{
	checkNoInstance(*this);
	this->sourceEngine = GLTF_FILE;
	this->externalShaderId = shaderId_;
	this->refreshBounds();
}

void Entity::setOverrideModel(const Graphics::ExternalModel* pModel_)
{
	this->pOverrideModel = pModel_;
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
	this->moveCameraModule();
	/* A teleport has no motion before it: the next move by the mean velocity (PhysicsBody::moveByMeanVelocity) starts from rest. */
	if(this->physicsId != UninitializedPoolId) Globals::Physics::bodies[this->physicsId].tickStartVelocity = glm::vec3(0.0f);
}

void Entity::moveTo(const glm::vec3& rPosition_)
{
	this->position = rPosition_;
	this->stagingPosition = rPosition_;
	this->transformDirty = true;
	this->moveCameraModule();
}

void Entity::moveCameraModule()
{
	if(this->cameraId == UninitializedPoolId) return;
	Camera& rCamera = Globals::Game::cameras[this->cameraId];
	if(rCamera.position != this->position) rCamera.set(this->position);
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

void Entity::setScale(const glm::vec3& rScale_)
{
	this->scale = rScale_;
	this->transformDirty = true;
	this->refreshBounds();
}

glm::vec3 Entity::collisionScale() const
{
	return this->physicsId == UninitializedPoolId ? this->scale : glm::vec3(1.0f);
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
	/* Returned above: an external-only entity has no instance to update. */
	case GLTF_FILE:
		break;
	}
}

Graphics::BoundingSphere Entity::localBounds()
{
	Graphics::BoundingSphere bounds;
	if(this->isExternalOnly()) return this->pOverrideModel != nullptr ? this->pOverrideModel->bounds : bounds;
	if(this->instanceId == UninitializedPoolId) return bounds;
	if(this->pOverrideModel != nullptr) return this->pOverrideModel->bounds;
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

/* Whether the ray (mesh space; the direction is not renormalized, so t is the same parameter as in the world) crosses a triangle of the mesh placed by rWorld_, nearest t in rDistance_. A mesh not in the standard layout has no readable positions and counts as empty. */
static bool rayHitsMesh(const Graphics::BasicMesh& rMesh_, const glm::mat4& rWorld_, const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rDistance_, bool& rAny_)
{
	if(rMesh_.verticies.pLayout != &Graphics::VertexLayout::standard() || rMesh_.verticies.empty()) return false;
	glm::mat4 inverse = glm::inverse(rWorld_);
	glm::vec3 origin = glm::vec3(inverse * glm::vec4(rOrigin_, 1.0f));
	glm::vec3 direction = glm::vec3(inverse * glm::vec4(rDirection_, 0.0f));
	const Graphics::Vertex* pVertices = rMesh_.verticies.begin();
	size_t count = rMesh_.indicies.empty() ? rMesh_.verticies.size() : rMesh_.indicies.size();
	bool hit = false;
	rAny_ = true;

	/* Moller-Trumbore, both sides (the editor selects what it sees from behind too). */
	for(size_t i = 0; i + 2 < count; i += 3)
	{
		const glm::vec3& rA = pVertices[rMesh_.indicies.empty() ? i : rMesh_.indicies[i]].position;
		const glm::vec3& rB = pVertices[rMesh_.indicies.empty() ? i + 1 : rMesh_.indicies[i + 1]].position;
		const glm::vec3& rC = pVertices[rMesh_.indicies.empty() ? i + 2 : rMesh_.indicies[i + 2]].position;
		glm::vec3 edge1 = rB - rA;
		glm::vec3 edge2 = rC - rA;
		glm::vec3 p = glm::cross(direction, edge2);
		float determinant = glm::dot(edge1, p);
		if(std::abs(determinant) < 1e-12f) continue;
		float inverseDeterminant = 1.0f / determinant;
		glm::vec3 toOrigin = origin - rA;
		float u = glm::dot(toOrigin, p) * inverseDeterminant;
		if(u < 0.0f || u > 1.0f) continue;
		glm::vec3 q = glm::cross(toOrigin, edge1);
		float v = glm::dot(direction, q) * inverseDeterminant;
		if(v < 0.0f || u + v > 1.0f) continue;
		float t = glm::dot(edge2, q) * inverseDeterminant;
		if(t <= 0.0f) continue;
		if(!hit || t < rDistance_)
		{
			rDistance_ = t;
			hit = true;
		}
	}
	return hit;
}

bool Entity::rayHitsModel(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rDistance_, bool& rHasTriangles_)
{
	rHasTriangles_ = false;
	if(this->scale.x == 0.0f || this->scale.y == 0.0f || this->scale.z == 0.0f) return false;
	if(!this->isExternalOnly() && this->instanceId == UninitializedPoolId) return false;

	/* The ray is taken into the model's own space by the inverse of the draw transform (a ONE model also has the RenderWareYawDegrees turn); the direction is not renormalized, so the distances stay world distances. */
	bool isONE = !this->isExternalOnly() && this->pOverrideModel == nullptr && this->sourceEngine == STH2005_ONE;
	glm::mat4 model = glm::translate(glm::mat4(1.0f), this->renderPosition()) * this->renderRotation() * glm::scale(glm::mat4(1.0f), this->scale);
	if(isONE) model = model * glm::rotate(glm::mat4(1.0f), glm::radians(Graphics::RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 inverse = glm::inverse(model);
	glm::vec3 origin = glm::vec3(inverse * glm::vec4(rOrigin_, 1.0f));
	glm::vec3 direction = glm::vec3(inverse * glm::vec4(rDirection_, 0.0f));

	/* A rest pose box rejects cheaply before the triangles; an SA model with a pose built (it is animated) can reach out of its rest pose box, so it skips this. */
	bool posed = !this->isExternalOnly() && this->pOverrideModel == nullptr && this->sourceEngine == SA_NINJA && !Graphics::getSAInstance(this->instanceId).nodePose.empty();
	Graphics::BoundingSphere local = this->localBounds();
	if(local.boxValid && !posed)
	{
		/* Slab test: the ray is inside the box between the latest entry and the earliest exit of the three axis slabs. */
		float entry = 0.0f;
		float exit = 1e30f;
		for(int axis = 0; axis < 3; axis++)
		{
			if(std::abs(direction[axis]) < 1e-9f)
			{
				if(origin[axis] < local.boxLow[axis] || origin[axis] > local.boxHigh[axis]) { exit = -1.0f; break; }
				continue;
			}
			float t0 = (local.boxLow[axis] - origin[axis]) / direction[axis];
			float t1 = (local.boxHigh[axis] - origin[axis]) / direction[axis];
			entry = std::max(entry, std::min(t0, t1));
			exit = std::min(exit, std::max(t0, t1));
		}
		if(entry > exit)
		{
			/* A model with a box has triangles, so a miss of the box is a miss of the model (no sphere fallback). */
			rHasTriangles_ = true;
			return false;
		}
	}

	bool hit = false;
	float best = 0.0f;
	auto test = [&](const Graphics::BasicMesh& rMesh_, const glm::mat4& rWorld_)
	{
		float distance = 0.0f;
		if(rayHitsMesh(rMesh_, rWorld_, origin, direction, distance, rHasTriangles_) && (!hit || distance < best))
		{
			best = distance;
			hit = true;
		}
	};
	if(this->pOverrideModel != nullptr)
	{
		Graphics::ExternalPools& rPools = this->pOverrideModel->inProject ? Graphics::externalProject : Graphics::externalLevel;
		for(const Graphics::ExternalSubmesh& rSubmesh : this->pOverrideModel->submeshes) test(rPools.meshes[rSubmesh.meshId], glm::mat4(1.0f));
	}
	else if(this->sourceEngine == SA_NINJA)
	{
		const Graphics::SAInstance& rInstance = Graphics::getSAInstance(this->instanceId);
		const Graphics::SAMesh& rMesh = Graphics::saMeshes[rInstance.saMeshId];

		/* The pose of the last drawn frame places the nodes when there is one (it overrides each node's localTransform). */
		bool usePose = rInstance.nodePose.size() == rMesh.nodes.size();
		std::vector<glm::mat4> world(rMesh.nodes.size());
		for(size_t i = 0; i < rMesh.nodes.size(); i++)
		{
			const Graphics::SANode& rNode = rMesh.nodes[i];
			world[i] = (rNode.parent < 0 ? glm::mat4(1.0f) : world[rNode.parent]) * (usePose ? rInstance.nodePose[i] : rNode.localTransform);
			for(const Graphics::SASubmesh& rSubmesh : rNode.submeshes) test(rSubmesh.mesh, world[i]);
		}
	}
	else if(this->sourceEngine == STH2005_ONE)
	{
		const Graphics::ONEInstance& rInstance = Graphics::getONEInstance(this->instanceId);
		const Graphics::RenderWareMesh& rMesh = Graphics::oneArchives[rInstance.archiveId].meshes[rInstance.meshIndex];

		/* The rest pose, like the bounds (a skinned atomic as if on its frame). */
		std::vector<glm::mat4> world(rMesh.frames.size());
		for(size_t i = 0; i < rMesh.frames.size(); i++) world[i] = (rMesh.frames[i].parent < 0 ? glm::mat4(1.0f) : world[rMesh.frames[i].parent]) * rMesh.frames[i].localTransform;
		for(const Graphics::RenderWareAtomic& rAtomic : rMesh.atomics)
		{
			for(const Graphics::RenderWareSubmesh& rSubmesh : rAtomic.submeshes) test(rSubmesh.mesh, world[rAtomic.frame]);
		}
	}
	if(hit) rDistance_ = best;
	return hit;
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

	/* The index and withinRange place the sphere by position and rotation only, so the model's scale is applied here: the center per axis, the radius by the largest axis. */
	result.center *= this->scale;
	result.radius *= std::max({std::abs(this->scale.x), std::abs(this->scale.y), std::abs(this->scale.z)});
	if(this->collisionId != UninitializedPoolId)
	{
		Graphics::BoundingSphere shapes;
		shapes.valid = true;
		for(const Physics::CollisionShape& rSourceShape : Globals::Physics::collisions[this->collisionId].shapes)
		{
			Physics::CollisionShape rShape = Physics::scaledShape(rSourceShape, this->collisionScale(), this->sourceEngine == SA_NINJA);
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
Draws the entity unless it is out of sight: the model's bounding sphere (from its rest pose, with a margin) is moved by the entity's transform (its radius by the largest scale) and tested against the view frustum and against maxDistance_
from the camera. The test uses the transform of the frame that is drawn (renderPosition and renderRotation).
*/
void Entity::draw(const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCameraPosition_, float maxDistance_)
{
	if(this->instanceId == UninitializedPoolId && !(this->isExternalOnly() && this->pOverrideModel != nullptr)) return;
	glm::vec3 renderPosition = this->renderPosition();
	glm::mat4 renderRotation = this->renderRotation();
	if(this->transformDirty || renderPosition != this->renderedPosition || renderRotation != this->renderedRotation)
	{
		this->transform = glm::translate(glm::mat4(1.0f), renderPosition) * renderRotation * glm::scale(glm::mat4(1.0f), this->scale);
		this->renderedPosition = renderPosition;
		this->renderedRotation = renderRotation;
		this->transformDirty = false;
	}

	Graphics::BoundingSphere bounds = this->localBounds();
	glm::vec3 center = glm::vec3(this->transform * glm::vec4(bounds.center, 1.0f));
	float radius = bounds.radius * std::max({std::abs(this->scale.x), std::abs(this->scale.y), std::abs(this->scale.z)});
	if(glm::distance(center, rCameraPosition_) - radius > maxDistance_) return;
	if(!sphereInFrustum(rPlanes_, center, radius)) return;

	Graphics::renderStats.entitiesDrawn++;
	if(this->isExternalOnly())
	{
		this->pOverrideModel->draw(this->externalShaderId, false, this->transform, this->pExternalTexture);
		return;
	}
	if(this->pOverrideModel != nullptr)
	{
		Graphics::Texture* pTexture = this->sourceEngine == SA_NINJA ? Graphics::getSAInstance(this->instanceId).pOverrideTexture : Graphics::getONEInstance(this->instanceId).pOverrideTexture;
		PoolId shaderId = this->sourceEngine == SA_NINJA ? Graphics::getSAInstance(this->instanceId).shaderId : Graphics::getONEInstance(this->instanceId).shaderId;
		this->pOverrideModel->draw(shaderId, this->sourceEngine == STH2005_ONE, this->transform, pTexture);
		return;
	}
	switch(this->sourceEngine)
	{
	case SA_NINJA:
		Graphics::getSAInstance(this->instanceId).draw(this->transform);
		break;
	case STH2005_ONE:
		Graphics::getONEInstance(this->instanceId).draw(this->transform);
		break;
	/* External-only entities returned above; they have no instance. */
	case GLTF_FILE:
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

	/* The collision shapes of an entity with a physics body are not scaled (collisionScale), which changes its bounds. */
	this->refreshBounds();
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
	if(this->cameraId != UninitializedPoolId)
	{
		if(Globals::Game::mainCamera == this->cameraId) Globals::Game::mainCamera = UninitializedPoolId;
		Globals::Game::cameras.remove(this->cameraId);
		this->cameraId = UninitializedPoolId;
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
	case GLTF_FILE:
		break;
	}
	this->instanceId = UninitializedPoolId;
}
