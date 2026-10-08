#include "BasicMesh.h"
#include "RenderStats.h"
#include <cstdint>
#include <cstddef>

namespace Graphics 
{

// void BasicMesh::addVertexBatch(void* data_, uint64_t vertexAmount_)
// {
// 	/* Offset is taken before resize; resize may reallocate, so the destination pointer is computed after it. */
// 	uint64_t oldSize = this->verticies.size();
// 	this->verticies.resize(oldSize + vertexAmount_);
// 	std::memcpy(this->verticies.data() + oldSize, data_, vertexAmount_ * sizeof(Vertex));
// } 

// void BasicMesh::addIndexBatch(void* data_, uint64_t indexAmount_)
// {
// 	uint64_t oldSize = this->indicies.size();
// 	this->indicies.resize(oldSize + indexAmount_);
// 	std::memcpy(this->indicies.data() + oldSize, data_, indexAmount_ * sizeof(uint32_t));
// }

void BasicMesh::create() 
{
	// init Vertex Buffer 
	glCreateBuffers(1, &this->VBO); 
	glNamedBufferStorage(this->VBO, this->verticies.byteSize(), this->verticies.data(), 0);

	// init VAO
	glCreateVertexArrays(1, &this->VAO);

	/*
	Interleaved vertex buffer on binding 0, the attributes described by the mesh's VertexLayout. The standard layout uses locations
	0 position, 1 normal, 2 uv, 3 color, 4 bone indices (integer), 5 bone weights; shaders must use the same locations.
	*/
	this->verticies.pLayout->setupVertexArray(this->VAO);
	glVertexArrayVertexBuffer(this->VAO, 0, this->VBO, 0, static_cast<GLsizei>(this->verticies.pLayout->stride));

	if(!this->indicies.empty())
	{
		// init Index Buffer
		glCreateBuffers(1, &this->EBO);
    	glNamedBufferStorage(this->EBO, this->indicies.size() * sizeof(uint32_t), this->indicies.data(), 0);
		
		// Index buffer binding (THIS is the important part)
    	glVertexArrayElementBuffer(VAO, EBO);
	}

	this->alive = true; 
}

void BasicMesh::draw()
{
	renderStats.vao(this->VAO);
	glBindVertexArray(this->VAO);
	renderStats.draw(this->indicies.empty() ? this->verticies.size() : this->indicies.size());
	if(!this->indicies.empty())
    	glDrawElements(GL_TRIANGLES, this->indicies.size(), GL_UNSIGNED_INT, 0);
	else
    	glDrawArrays(GL_TRIANGLES, 0, this->verticies.size());
}


void BasicMesh::destroy()
{
	if(this->alive)
	{
		glDeleteVertexArrays(1, &this->VAO);
		glDeleteBuffers(1, &this->VBO); 
		if(!this->indicies.empty()) glDeleteBuffers(1, &this->EBO);
		this->alive = false;
	}
}
}