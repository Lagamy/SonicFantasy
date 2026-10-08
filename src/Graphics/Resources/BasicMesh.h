#pragma once

#include <cstdint>
#include <glad/glad.h>
#include <glm/ext/vector_float3.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>
#include <cstring>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <vector>
#include "VertexLayout.h"

namespace Graphics
{
/*
boneIndices / boneWeights are a skinned vertex's up to 4 skin bones and their weights (Shaders/STH2005); zero for
everything else, which ignores them.
*/
struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 color;
	glm::u8vec4 boneIndices{0};
	glm::vec4 boneWeights{0.0f};
};

/*
The vertices of a mesh as bytes in the mesh's VertexLayout (the buffer BasicMesh::create uploads as it is). A mesh made of the standard layout (Vertex, what the games' importers write) is read and written
as Vertex through push_back, operator[], begin/end and operator=; those throw for any other layout. A custom layout is read and written by member (setMember / getMember). The layout is set while the array is empty.
*/

struct VertexArray
{
	const VertexLayout* pLayout = &VertexLayout::standard();
	std::vector<unsigned char> bytes;

	size_t size() const { return this->bytes.size() / this->pLayout->stride; }
	bool empty() const { return this->bytes.empty(); }
	const void* data() const { return this->bytes.data(); }
	size_t byteSize() const { return this->bytes.size(); }

	void setLayout(const VertexLayout* pLayout_)
	{
		if(!this->bytes.empty()) throw std::runtime_error("VertexArray: the layout can only change while there are no vertices");
		this->pLayout = pLayout_;
	}

	/* Makes the array count vertices long, new ones zero. */
	void resize(size_t count_) { this->bytes.resize(count_ * this->pLayout->stride); }

	void push_back(const Vertex& rVertex_)
	{
		this->requireStandard();
		size_t old = this->bytes.size();
		this->bytes.resize(old + sizeof(Vertex));
		std::memcpy(this->bytes.data() + old, &rVertex_, sizeof(Vertex));
	}

	Vertex& operator[](size_t index_) { this->requireStandard(); return *reinterpret_cast<Vertex*>(this->bytes.data() + index_ * sizeof(Vertex)); }
	const Vertex& operator[](size_t index_) const { this->requireStandard(); return *reinterpret_cast<const Vertex*>(this->bytes.data() + index_ * sizeof(Vertex)); }
	Vertex* begin() { this->requireStandard(); return reinterpret_cast<Vertex*>(this->bytes.data()); }
	Vertex* end() { return this->begin() + this->size(); }
	const Vertex* begin() const { this->requireStandard(); return reinterpret_cast<const Vertex*>(this->bytes.data()); }
	const Vertex* end() const { return this->begin() + this->size(); }

	VertexArray& operator=(const std::vector<Vertex>& rVertices_)
	{
		this->requireStandard();
		this->bytes.resize(rVertices_.size() * sizeof(Vertex));
		if(!rVertices_.empty()) std::memcpy(this->bytes.data(), rVertices_.data(), this->bytes.size());
		return *this;
	}

	VertexArray& operator=(std::initializer_list<Vertex> vertices_)
	{
		this->requireStandard();
		this->bytes.resize(vertices_.size() * sizeof(Vertex));
		if(vertices_.size() > 0) std::memcpy(this->bytes.data(), vertices_.begin(), this->bytes.size());
		return *this;
	}

	template<typename T>
	void setMember(uint32_t memberId_, size_t vertexId_, const T& rValue_)
	{
		std::memcpy(this->bytes.data() + this->memberOffset<T>(memberId_, vertexId_), &rValue_, sizeof(T));
	}

	template<typename T>
	T getMember(uint32_t memberId_, size_t vertexId_) const
	{
		T value;
		std::memcpy(&value, this->bytes.data() + this->memberOffset<T>(memberId_, vertexId_), sizeof(T));
		return value;
	}

private:
	void requireStandard() const
	{
		if(this->pLayout != &VertexLayout::standard()) throw std::runtime_error("VertexArray: the vertices are not in the standard layout");
	}

	/* The byte of the member in the vertex array. Throws if T is not the member's type or an id is out of range. */
	template<typename T>
	size_t memberOffset(uint32_t memberId_, size_t vertexId_) const
	{
		if(memberId_ >= this->pLayout->members.size()) throw std::runtime_error(std::format("VertexArray: member {} is out of range", memberId_));
		const VertexMember& rMember = this->pLayout->members[memberId_];
		if(VertexMemberTypeOf<T>::value != rMember.type) throw std::runtime_error(std::format("VertexArray: member '{}' is not of the type asked for", rMember.name));
		if(vertexId_ >= this->size()) throw std::runtime_error(std::format("VertexArray: vertex {} is out of range", vertexId_));
		return vertexId_ * this->pLayout->stride + rMember.offset;
	}
};

struct BasicMesh
{
	GLuint VAO;
	GLuint VBO;
	GLuint EBO;

	VertexArray verticies;
	std::vector<uint32_t> indicies; 
	bool alive = false; 
	
	void create(); 
	void draw(); // Using current shader
	void destroy();
	
	// Used for importing from any bin. (Proved to be not really usefull)
	// void addVertexBatch(void* data_, uint64_t vertexAmount_); 
	// void addIndexBatch(void* data_, uint64_t indexAmount_); 
};
}
