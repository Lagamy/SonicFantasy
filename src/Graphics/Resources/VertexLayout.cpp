#include "VertexLayout.h"
#include "BasicMesh.h"
#include <format>
#include <stdexcept>

namespace Graphics
{
static size_t memberSize(VertexMemberType type_)
{
	switch(type_)
	{
		case VertexMemberType::Float: return sizeof(float);
		case VertexMemberType::Vec2: return sizeof(glm::vec2);
		case VertexMemberType::Vec3: return sizeof(glm::vec3);
		case VertexMemberType::Vec4: return sizeof(glm::vec4);
		case VertexMemberType::U8Vec4: return sizeof(glm::u8vec4);
	}
	throw std::runtime_error("VertexLayout: unknown member type");
}

static GLint memberComponents(VertexMemberType type_)
{
	switch(type_)
	{
		case VertexMemberType::Float: return 1;
		case VertexMemberType::Vec2: return 2;
		case VertexMemberType::Vec3: return 3;
		case VertexMemberType::Vec4: return 4;
		case VertexMemberType::U8Vec4: return 4;
	}
	throw std::runtime_error("VertexLayout: unknown member type");
}

const char* vertexMemberTypeName(VertexMemberType type_)
{
	switch(type_)
	{
		case VertexMemberType::Float: return "Float";
		case VertexMemberType::Vec2: return "Vec2";
		case VertexMemberType::Vec3: return "Vec3";
		case VertexMemberType::Vec4: return "Vec4";
		case VertexMemberType::U8Vec4: return "U8Vec4";
	}
	throw std::runtime_error("VertexLayout: unknown member type");
}

VertexMemberType vertexMemberTypeFromName(const std::string& rName_)
{
	for(VertexMemberType type : {VertexMemberType::Float, VertexMemberType::Vec2, VertexMemberType::Vec3, VertexMemberType::Vec4, VertexMemberType::U8Vec4})
	{
		if(rName_ == vertexMemberTypeName(type)) return type;
	}
	throw std::runtime_error(std::format("VertexLayout: '{}' is not a member type", rName_));
}

std::string VertexLayout::key() const
{
	std::string result;
	for(const VertexMember& rMember : this->members)
	{
		if(!result.empty()) result += ",";
		result += std::format("{}:{}@{}", rMember.name, vertexMemberTypeName(rMember.type), rMember.location);
	}
	return result;
}

uint32_t VertexLayout::addMember(const char* name_, VertexMemberType type_, GLuint location_)
{
	for(const VertexMember& rMember : this->members)
	{
		if(rMember.name == name_) throw std::runtime_error(std::format("VertexLayout: member '{}' exists already", name_));
		if(rMember.location == location_) throw std::runtime_error(std::format("VertexLayout: location {} of '{}' is taken by '{}'", location_, name_, rMember.name));
	}
	VertexMember member{name_, type_, location_, this->stride, memberSize(type_)};
	this->stride += member.size;
	this->members.push_back(member);
	return static_cast<uint32_t>(this->members.size() - 1);
}

uint32_t VertexLayout::memberIdFromName(const char* name_) const
{
	for(uint32_t i = 0; i < this->members.size(); i++)
	{
		if(this->members[i].name == name_) return i;
	}
	throw std::runtime_error(std::format("VertexLayout: no member named '{}'", name_));
}

void VertexLayout::setupVertexArray(GLuint vao_) const
{
	for(const VertexMember& rMember : this->members)
	{
		if(rMember.type == VertexMemberType::U8Vec4)
		{
			glVertexArrayAttribIFormat(vao_, rMember.location, memberComponents(rMember.type), GL_UNSIGNED_BYTE, static_cast<GLuint>(rMember.offset));
		}
		else
		{
			glVertexArrayAttribFormat(vao_, rMember.location, memberComponents(rMember.type), GL_FLOAT, GL_FALSE, static_cast<GLuint>(rMember.offset));
		}
		glEnableVertexArrayAttrib(vao_, rMember.location);
		glVertexArrayAttribBinding(vao_, rMember.location, 0);
	}
}

/*
The layout of Vertex, with the attribute locations the shaders use: 0 position, 1 normal, 2 uv, 3 color, 4 bone indices (integer), 5 bone weights. The members are tightly packed, which is what the
struct is too, so the offsets are checked against it once and a mismatch (a member added to Vertex but not here) throws.
*/
const VertexLayout& VertexLayout::standard()
{
	static const VertexLayout layout = []()
	{
		VertexLayout result;
		result.addMember("position", VertexMemberType::Vec3, 0);
		result.addMember("normal", VertexMemberType::Vec3, 1);
		result.addMember("uv", VertexMemberType::Vec2, 2);
		result.addMember("color", VertexMemberType::Vec4, 3);
		result.addMember("boneIndices", VertexMemberType::U8Vec4, 4);
		result.addMember("boneWeights", VertexMemberType::Vec4, 5);
		const size_t expected[] = {offsetof(Vertex, position), offsetof(Vertex, normal), offsetof(Vertex, uv), offsetof(Vertex, color), offsetof(Vertex, boneIndices), offsetof(Vertex, boneWeights)};
		bool matches = result.stride == sizeof(Vertex);
		for(size_t i = 0; i < result.members.size(); i++) matches = matches && result.members[i].offset == expected[i];
		if(!matches) throw std::runtime_error("VertexLayout::standard does not match the struct Vertex");
		return result;
	}();
	return layout;
}
}
