#include "BasicMesh.h"
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
	glNamedBufferStorage(this->VBO, this->verticies.size() * sizeof(Vertex), this->verticies.data(), 0);

	// init VAO
	glCreateVertexArrays(1, &this->VAO);

	/*
	Interleaved Vertex buffer on binding 0. Attribute locations:
	0 position, 1 normal, 2 uv, 3 color, 4 bone indices (integer), 5 bone weights. Shaders must use the same locations.
	*/
	glVertexArrayAttribFormat(this->VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
	glVertexArrayAttribFormat(this->VAO, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
	glVertexArrayAttribFormat(this->VAO, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
	glVertexArrayAttribFormat(this->VAO, 3, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, color));
	glVertexArrayAttribIFormat(this->VAO, 4, 4, GL_UNSIGNED_BYTE, offsetof(Vertex, boneIndices));
	glVertexArrayAttribFormat(this->VAO, 5, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, boneWeights));
	for(GLuint i = 0; i < 6; i++)
	{
		glEnableVertexArrayAttrib(this->VAO, i);
		glVertexArrayAttribBinding(this->VAO, i, 0);
	}
	glVertexArrayVertexBuffer(this->VAO, 0, this->VBO, 0, sizeof(Vertex));

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
	glBindVertexArray(this->VAO);
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