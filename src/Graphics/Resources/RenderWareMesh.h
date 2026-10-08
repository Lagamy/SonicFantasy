#pragma once
#include "BoundingSphere.h"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "BasicMesh.h"
#include "PoolId.h"

namespace Graphics
{
/*
textureName is the base texture's name in its texture dictionary, empty if the material is untextured.
A ONEInstance resolves it into a Texture pool id when it is created.
uvAnimName is the name of the material's UV animation in the model's .UVA, empty if it has none (not used by drawing yet).
*/
struct RenderWareSubmesh
{
	BasicMesh mesh;
	std::string textureName;
	std::string uvAnimName;
};

/*
One RenderWare frame (bone/node). parent is an index into RenderWareMesh::frames, -1 for a root.
Frames are stored parent before child, so poses can be computed in one pass.
localTransform is the rest transform from the file; animated poses live in the instance.
boneId is the frame's HAnim node id, -1 when it has none. name comes from a matching .BON, empty otherwise.
*/
struct RenderWareFrame
{
	int32_t parent;
	glm::mat4 localTransform;
	int32_t boneId;
	std::string name;
};

/*
A drawable piece of the model, placed by the world matrix of frames[frame].
A skinned atomic (from the DFF Skin plugin) instead places each vertex by its skin bones: skinBoneFrames[b] is the frame
of skin bone b and inverseBindMatrices[b] its inverse bind matrix; both are empty for an unskinned atomic.
*/
struct RenderWareAtomic
{
	int32_t frame;
	std::vector<RenderWareSubmesh> submeshes;
	std::vector<int32_t> skinBoneFrames;
	std::vector<glm::mat4> inverseBindMatrices;
};

struct RenderWareMesh
{
	std::vector<RenderWareFrame> frames;
	std::vector<RenderWareAtomic> atomics;

	BoundingSphere bounds;

	void use();
	void destroy();

	/* Fills bounds from the vertices in the rest pose (a skinned vertex as if on its atomic's frame), once. */
	void computeBounds();
};
}
