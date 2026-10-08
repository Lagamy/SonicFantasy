#include "RenderWareMesh.h"
#include "GPU_API.h"

namespace Graphics
{
/*
Same as SAMesh::use: uploads submeshes not yet on the GPU, safe to call every frame.
*/
void RenderWareMesh::use()
{
	for(RenderWareAtomic& rAtomic : this->atomics)
	{
		for(RenderWareSubmesh& rSubmesh : rAtomic.submeshes)
		{
			if(!rSubmesh.mesh.alive) rSubmesh.mesh.create();
		}
	}
}

void RenderWareMesh::destroy()
{
	for(RenderWareAtomic& rAtomic : this->atomics)
	{
		for(RenderWareSubmesh& rSubmesh : rAtomic.submeshes)
		{
			rSubmesh.mesh.destroy();
		}
	}
}

void RenderWareMesh::computeBounds()
{
	if(this->bounds.valid) return;
	glm::vec3 low(1e30f);
	glm::vec3 high(-1e30f);
	std::vector<glm::mat4> world(this->frames.size());
	for(size_t i = 0; i < this->frames.size(); i++)
	{
		world[i] = (this->frames[i].parent < 0 ? glm::mat4(1.0f) : world[this->frames[i].parent]) * this->frames[i].localTransform;
	}
	for(const RenderWareAtomic& rAtomic : this->atomics)
	{
		for(const RenderWareSubmesh& rSubmesh : rAtomic.submeshes)
		{
			for(const Vertex& rVertex : rSubmesh.mesh.verticies)
			{
				glm::vec3 point = glm::vec3(world[rAtomic.frame] * glm::vec4(rVertex.position, 1.0f));
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
