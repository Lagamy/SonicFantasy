#include "SAMesh.h"

namespace Graphics
{
/*
Uploads the vertex/index data of every submesh that is not on the GPU yet.
BasicMesh::create sets up the VAO with fixed attribute locations (0..3), so any shader
following that layout is connected to the data by binding the VAO at draw time.
Safe to call every frame: submeshes already uploaded are skipped.
*/
void SAMesh::use()
{
	for(SANode& rNode : this->nodes)
	{
		for(SASubmesh& rSubmesh : rNode.submeshes)
		{
			if(!rSubmesh.mesh.alive) rSubmesh.mesh.create();
		}
	}
}

void SAMesh::destroy()
{
	for(SANode& rNode : this->nodes)
	{
		for(SASubmesh& rSubmesh : rNode.submeshes)
		{
			rSubmesh.mesh.destroy();
		}
	}
}

void SAMesh::computeBounds()
{
	if(this->bounds.valid) return;
	glm::vec3 low(1e30f);
	glm::vec3 high(-1e30f);
	std::vector<glm::mat4> world(this->nodes.size());
	for(size_t i = 0; i < this->nodes.size(); i++)
	{
		const SANode& rNode = this->nodes[i];
		world[i] = (rNode.parent < 0 ? glm::mat4(1.0f) : world[rNode.parent]) * rNode.localTransform;
		for(const SASubmesh& rSubmesh : rNode.submeshes)
		{
			for(const Vertex& rVertex : rSubmesh.mesh.verticies)
			{
				glm::vec3 point = glm::vec3(world[i] * glm::vec4(rVertex.position, 1.0f));
				low = glm::min(low, point);
				high = glm::max(high, point);
			}
		}
	}
	if(low.x > high.x) low = high = glm::vec3(0.0f);
	this->bounds.boxLow = low;
	this->bounds.boxHigh = high;
	this->bounds.boxValid = true;
	this->bounds.center = (low + high) * 0.5f;
	this->bounds.radius = glm::length(high - low) * 0.5f * 1.5f + 5.0f;
	this->bounds.valid = true;
}
}
