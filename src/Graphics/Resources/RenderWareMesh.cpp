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
}
