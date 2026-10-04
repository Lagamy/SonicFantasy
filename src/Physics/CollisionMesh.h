#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace Physics
{
/*
One piece of collision geometry (a SADX land table COL): its raw surface flags (no meaning is given to any bit here),
its bounding sphere in level space and its triangles, which are indices[firstTriangle * 3 .. (firstTriangle + triangleCount) * 3).
*/
struct CollisionPiece
{
	uint32_t flags;
	glm::vec3 center;
	float radius;
	uint32_t firstTriangle;
	uint32_t triangleCount;
};

/*
Plain CPU collision data produced by the importers: vertices in level space (the node hierarchy already applied), triangle
indices (three per triangle) into them and the pieces that group the triangles. No GPU data, no pool.
triangleAttributes / triangleMaterials hold one value per triangle when the source stores them per triangle (Shadow's
collision .BSP: the raw 32-bit "attribute" user data and the RenderWare material index); they are empty when the source
keeps its flags per piece (SADX, CollisionPiece::flags).
*/
struct CollisionMesh
{
	std::vector<glm::vec3> vertices;
	std::vector<uint32_t> indices;
	std::vector<CollisionPiece> pieces;
	std::vector<uint32_t> triangleAttributes;
	std::vector<uint16_t> triangleMaterials;

	/*
	The raw value that decides what a triangle is, whatever the source stores: its own attribute when the mesh has them per triangle
	(Shadow), otherwise the surface flags of the piece it is in (SADX), 0 if the triangle is in no piece. The pieces are in triangle order.
	*/
	uint32_t triangleValue(uint32_t triangle_) const
	{
		if(!this->triangleAttributes.empty()) return this->triangleAttributes[triangle_];
		for(size_t i = this->pieces.size(); i > 0; i--)
		{
			const CollisionPiece& rPiece = this->pieces[i - 1];
			if(triangle_ >= rPiece.firstTriangle && triangle_ < rPiece.firstTriangle + rPiece.triangleCount) return rPiece.flags;
		}
		return 0;
	}

	/*
	Adds rOther_'s geometry after this one's (indices and triangle ranges shifted). Both must store per-triangle values or
	neither: mixing a SADX and a Shadow mesh would leave the per-triangle vectors out of step with the triangles.
	*/
	void append(const CollisionMesh& rOther_)
	{
		uint32_t firstVertex = static_cast<uint32_t>(this->vertices.size());
		uint32_t firstTriangle = static_cast<uint32_t>(this->indices.size() / 3);
		this->vertices.insert(this->vertices.end(), rOther_.vertices.begin(), rOther_.vertices.end());
		for(uint32_t index : rOther_.indices)
		{
			this->indices.push_back(firstVertex + index);
		}
		for(CollisionPiece piece : rOther_.pieces)
		{
			piece.firstTriangle += firstTriangle;
			this->pieces.push_back(piece);
		}
		this->triangleAttributes.insert(this->triangleAttributes.end(), rOther_.triangleAttributes.begin(), rOther_.triangleAttributes.end());
		this->triangleMaterials.insert(this->triangleMaterials.end(), rOther_.triangleMaterials.begin(), rOther_.triangleMaterials.end());
	}
};
}
