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
}
