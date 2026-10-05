#pragma once
#include "BoundingSphere.h"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include "BasicMesh.h"
#include "PoolId.h"

namespace Graphics
{
/*
textureIndex is the material's texture id from the file, an index into the file order of the TextureSet that
the instance's TextureBinding resolves it against. The mesh itself holds no Texture pool ids, so different bindings can texture the same mesh differently.
clampU / clampV come from the material: an axis that is not clamped repeats the texture, mirrored at every repeat with flipU / flipV (see readAttach).
doubleSided is bit 23 (0x800000) of the material's attributes: the submesh is drawn without back-face culling (external: SA Tools' material flags / Ninja's NJD_FLAG_DOUBLE_SIDE as remembered; the same enum as the clamp and flip bits above; not yet checked on a model file).
*/
constexpr uint32_t SANoTexture = 0xFFFFFFFF;

struct SASubmesh
{
	BasicMesh mesh;
	uint32_t textureIndex = SANoTexture;
	bool clampU = false;
	bool clampV = false;
	bool flipU = false;
	bool flipV = false;
	bool doubleSided = false;
};

/* NJS_OBJECT eval flags that change the local transform (external, SA Tools Enums.cs ObjectFlags). */
constexpr uint32_t NinjaNoPosition = 0x01;
constexpr uint32_t NinjaNoRotate = 0x02;
constexpr uint32_t NinjaNoScale = 0x04;
constexpr uint32_t NinjaRotateZYX = 0x20;

/*
A Ninja local transform from its parts: position, rotation in BAMS (65536 = full turn; fractional while interpolating),
scale, and the object's eval flags (NoPosition / NoRotate / NoScale skip that part).
Hypothesis, unverified: rotation order. Default applies X, then Y, then Z to the vertex (matrix Z*Y*X);
the RotateZYX flag applies Z, then Y, then X (matrix X*Y*Z).
Used for the rest transform (importSAModel) and for animated poses (SAInstance::update).
*/
inline glm::mat4 composeNinjaTransform(const glm::vec3& rPosition_, const glm::vec3& rRotation_, const glm::vec3& rScale_, uint32_t flags_)
{
	glm::mat4 transform(1.0f);
	if(!(flags_ & NinjaNoPosition))
	{
		transform = glm::translate(transform, rPosition_);
	}
	if(!(flags_ & NinjaNoRotate))
	{
		constexpr float bamsToRadians = glm::two_pi<float>() / 65536.0f;
		glm::vec3 angle = rRotation_ * bamsToRadians;
		if(flags_ & NinjaRotateZYX)
		{
			transform = glm::rotate(transform, angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
			transform = glm::rotate(transform, angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
			transform = glm::rotate(transform, angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
		}
		else
		{
			transform = glm::rotate(transform, angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
			transform = glm::rotate(transform, angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
			transform = glm::rotate(transform, angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
		}
	}
	if(!(flags_ & NinjaNoScale))
	{
		transform = glm::scale(transform, rScale_);
	}
	return transform;
}

/*
One node of the model's hierarchy. parent is an index into SAMesh::nodes, -1 for a root.
Nodes are stored parent before child, so poses can be computed in one pass.
localTransform is the rest transform from the file, composed from restPosition / restRotation / restScale and flags,
which an animation uses for the channels it doesn't set; animated poses live in the instance.
*/
struct SANode
{
	int32_t parent;
	glm::mat4 localTransform;
	glm::vec3 restPosition;
	glm::ivec3 restRotation;
	glm::vec3 restScale;
	uint32_t flags;
	std::vector<SASubmesh> submeshes;
};

/*
A relocated .rel module (see relocateRel), kept while meshes and motions imported from it are alive so that the next one
imported from the same module does not read and relocate it again. users counts those meshes and motions; at 0 the
bytes are freed.
Graphics::relModules, by module path.
*/
struct RelModule
{
	std::vector<uint8_t> bytes;
	uint32_t users = 0;
};

/*
users counts the instances using the mesh; at 0 it is removed (releaseSAMesh).
rel is the path of the .rel module the mesh was imported from (empty for a mesh from another source); the mesh holds one
user of that module's RelModule until it is removed.
*/
struct SAMesh
{
	std::vector<SANode> nodes;
	uint32_t users = 0;
	std::string rel;

	BoundingSphere bounds;

	void use();
	void destroy();

	/* Fills bounds from the vertices in the rest pose, once. */
	void computeBounds();
};
}
