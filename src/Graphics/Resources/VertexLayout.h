#pragma once
#include <cstddef>
#include <cstdint>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>
#include <string>
#include <vector>

namespace Graphics
{
/*
The types a vertex member can have. U8Vec4 is read by the shader as an integer attribute (skin bone indices), the others as floats.
*/
enum class VertexMemberType : uint8_t
{
	Float,
	Vec2,
	Vec3,
	Vec4,
	U8Vec4
};

template<typename T>
struct VertexMemberTypeOf;
template<> struct VertexMemberTypeOf<float> { static constexpr VertexMemberType value = VertexMemberType::Float; };
template<> struct VertexMemberTypeOf<glm::vec2> { static constexpr VertexMemberType value = VertexMemberType::Vec2; };
template<> struct VertexMemberTypeOf<glm::vec3> { static constexpr VertexMemberType value = VertexMemberType::Vec3; };
template<> struct VertexMemberTypeOf<glm::vec4> { static constexpr VertexMemberType value = VertexMemberType::Vec4; };
template<> struct VertexMemberTypeOf<glm::u8vec4> { static constexpr VertexMemberType value = VertexMemberType::U8Vec4; };

/* The names a member type has in files ("Float", "Vec2", "Vec3", "Vec4", "U8Vec4"). The reverse throws for any other name. */
const char* vertexMemberTypeName(VertexMemberType type_);
VertexMemberType vertexMemberTypeFromName(const std::string& rName_);

/* location is the shader's attribute location, offset the member's first byte inside a vertex. */
struct VertexMember
{
	std::string name;
	VertexMemberType type;
	GLuint location;
	size_t offset;
	size_t size;
};

/*
What one vertex of a mesh is made of: its members in order, tightly packed (no padding), and the byte size of a vertex (stride). The same layout describes the vertices of the games' meshes
(standard(), the layout of Vertex) and of custom ones, and tells BasicMesh::create how to set up the attributes. A layout must outlive the meshes that point to it; standard() lives for the whole run.
*/
struct VertexLayout
{
	std::vector<VertexMember> members;
	size_t stride = 0;

	/* Appends a member after the last one and returns its index. Throws if the name or the location is taken. */
	uint32_t addMember(const char* name_, VertexMemberType type_, GLuint location_);

	/* The index of the member with that name. Throws if there is none. */
	uint32_t memberIdFromName(const char* name_) const;

	/* The layout as one line, "position:Vec3@0,normal:Vec3@1": equal text means an identical layout (names, types, locations, order). */
	std::string key() const;

	/* Describes every member as an attribute of the vertex array, all read from vertex buffer binding 0. */
	void setupVertexArray(GLuint vao_) const;

	static const VertexLayout& standard();
};
}
