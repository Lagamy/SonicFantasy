#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "BoundingSphere.h"
#include "VertexLayout.h"

/*
Our own on-disk model format for assets that are not on a disc (docs/ARCHITECTURE.md, "External assets"). A model is two files next to each other: <name>.model.json (the header) and <name>.model.bin
(the bytes). Header: "version" (1), "name" (equal to the file name), "layout" (the vertex members in order: name, type, attribute location; offsets and stride follow from packing), "bounds" (center, radius),
"submeshes" (each: "texture" name or empty, "vertexCount", "indexCount", "vertexOffset" and "indexOffset" in bytes into the .bin). The .bin is little-endian: per submesh its vertices (vertexCount x stride)
and its u32 triangle indices. Reading checks everything against the file sizes and throws std::runtime_error naming the file.
*/
namespace External
{
struct ModelSubmesh
{
	std::string texture;
	uint32_t vertexCount = 0;
	std::vector<unsigned char> vertices;
	std::vector<uint32_t> indices;
};

struct ModelData
{
	std::string name;
	Graphics::VertexLayout layout;
	Graphics::BoundingSphere bounds;
	std::vector<ModelSubmesh> submeshes;
};

/* The .bin path that belongs to a .model.json path. */
std::string modelBinPath(const std::string& rJsonPath_);

/* Reads <name>.model.json and its .bin. */
ModelData readModel(const std::string& rJsonPath_);

/* Writes both files (the header with the offsets filled in). A submesh whose vertex bytes are not vertexCount x stride throws. */
void writeModel(const std::string& rJsonPath_, const ModelData& rModel_);
}
