#pragma once
#include <glm/glm.hpp>

namespace Graphics
{
/*
A sphere around a model in its own space, for deciding whether to draw it (Entity::draw). Computed once from the rest pose (SAMesh::computeBounds, RenderWareMesh::computeBounds): the half diagonal of the
box around the vertices, with a margin (times 1.5 and 5 more) so an animated pose that reaches out of the rest pose is still inside. valid is false until computed.
*/
struct BoundingSphere
{
	glm::vec3 center = glm::vec3(0.0f);
	float radius = 0.0f;
	bool valid = false;

	/* The exact box around the rest pose vertices (no margin), as a cheap reject before picking in the editor tests triangles (Entity::rayHitsModel); boxValid is false for models that only have the sphere (external ones). */
	glm::vec3 boxLow = glm::vec3(0.0f);
	glm::vec3 boxHigh = glm::vec3(0.0f);
	bool boxValid = false;
};
}
