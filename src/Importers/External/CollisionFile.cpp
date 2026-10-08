#include "CollisionFile.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace External
{
namespace
{
using Json = nlohmann::ordered_json;

constexpr uint32_t CollisionVersion = 1;
const std::string JsonSuffix = ".collision.json";

std::vector<unsigned char> readBytes(const std::string& rPath_)
{
	std::ifstream file(rPath_, std::ios::binary);
	if(!file) throw std::runtime_error(std::format("can't open {}", rPath_));
	return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

glm::vec3 readVec3(const Json& rValue_, const char* what_)
{
	std::vector<float> values = rValue_.get<std::vector<float>>();
	if(values.size() != 3) throw std::runtime_error(std::format("{} needs 3 numbers", what_));
	return glm::vec3(values[0], values[1], values[2]);
}
}

std::string collisionBinPath(const std::string& rJsonPath_)
{
	if(!rJsonPath_.ends_with(JsonSuffix)) throw std::runtime_error(std::format("Collision '{}': the file name must end with {}", rJsonPath_, JsonSuffix));
	return rJsonPath_.substr(0, rJsonPath_.size() - JsonSuffix.size()) + ".collision.bin";
}

Physics::CollisionMesh readCollision(const std::string& rJsonPath_)
{
	try
	{
		std::string binPath = collisionBinPath(rJsonPath_);
		std::ifstream file(rJsonPath_);
		if(!file) throw std::runtime_error("can't open the file");
		Json header = Json::parse(file);
		if(header.at("version").get<uint32_t>() != CollisionVersion) throw std::runtime_error(std::format("version {} is not supported", header.at("version").dump()));

		uint64_t vertexCount = header.at("vertexCount").get<uint64_t>();
		uint64_t triangleCount = header.at("triangleCount").get<uint64_t>();
		bool hasValues = header.at("hasTriangleValues").get<bool>();
		std::vector<unsigned char> blob = readBytes(binPath);

		/* Sizes in u64 first: a header that lies must not overflow the check. */
		uint64_t expected = vertexCount * 12 + triangleCount * 12 + (hasValues ? triangleCount * 4 : 0);
		if(vertexCount > blob.size() || triangleCount > blob.size() || expected != blob.size()) throw std::runtime_error(std::format("{} has {} bytes, the header needs {}", binPath, blob.size(), expected));

		Physics::CollisionMesh mesh;
		mesh.vertices.resize(vertexCount);
		if(vertexCount > 0) std::memcpy(mesh.vertices.data(), blob.data(), vertexCount * 12);
		mesh.indices.resize(triangleCount * 3);
		if(triangleCount > 0) std::memcpy(mesh.indices.data(), blob.data() + vertexCount * 12, triangleCount * 12);
		for(uint32_t index : mesh.indices)
		{
			if(index >= vertexCount) throw std::runtime_error(std::format("an index is {} but there are {} vertices", index, vertexCount));
		}
		if(hasValues)
		{
			mesh.triangleAttributes.resize(triangleCount);
			if(triangleCount > 0) std::memcpy(mesh.triangleAttributes.data(), blob.data() + vertexCount * 12 + triangleCount * 12, triangleCount * 4);
		}
		for(const Json& rPiece : header.at("pieces"))
		{
			Physics::CollisionPiece piece;
			piece.flags = rPiece.at("flags").get<uint32_t>();
			piece.center = readVec3(rPiece.at("center"), "a piece's center");
			piece.radius = rPiece.at("radius").get<float>();
			piece.firstTriangle = rPiece.at("firstTriangle").get<uint32_t>();
			piece.triangleCount = rPiece.at("triangleCount").get<uint32_t>();
			if(static_cast<uint64_t>(piece.firstTriangle) + piece.triangleCount > triangleCount) throw std::runtime_error("a piece reaches past the triangles");
			mesh.pieces.push_back(piece);
		}
		return mesh;
	}
	catch(const std::exception& rError)
	{
		throw std::runtime_error(std::format("Collision '{}': {}", rJsonPath_, rError.what()));
	}
}

void writeCollision(const std::string& rJsonPath_, const std::string& rName_, const Physics::CollisionMesh& rMesh_)
{
	std::string binPath = collisionBinPath(rJsonPath_);
	uint64_t triangleCount = rMesh_.indices.size() / 3;
	bool hasValues = !rMesh_.triangleAttributes.empty();
	if(rMesh_.indices.size() % 3 != 0 || (hasValues && rMesh_.triangleAttributes.size() != triangleCount))
	{
		throw std::runtime_error(std::format("Collision '{}': the indices or the per-triangle values do not match the triangles", rJsonPath_));
	}

	Json pieces = Json::array();
	for(const Physics::CollisionPiece& rPiece : rMesh_.pieces)
	{
		pieces.push_back({{"flags", rPiece.flags}, {"center", {rPiece.center.x, rPiece.center.y, rPiece.center.z}}, {"radius", rPiece.radius}, {"firstTriangle", rPiece.firstTriangle}, {"triangleCount", rPiece.triangleCount}});
	}
	Json header = {{"version", CollisionVersion}, {"name", rName_}, {"vertexCount", rMesh_.vertices.size()}, {"triangleCount", triangleCount}, {"hasTriangleValues", hasValues}, {"pieces", pieces}};

	/* Little-endian bytes are the host's own (x86), so the arrays are copied as they are. */
	std::ofstream binFile(binPath, std::ios::binary);
	if(!binFile) throw std::runtime_error(std::format("Collision: can't write {}", binPath));
	binFile.write(reinterpret_cast<const char*>(rMesh_.vertices.data()), static_cast<std::streamsize>(rMesh_.vertices.size() * 12));
	binFile.write(reinterpret_cast<const char*>(rMesh_.indices.data()), static_cast<std::streamsize>(rMesh_.indices.size() * 4));
	if(hasValues) binFile.write(reinterpret_cast<const char*>(rMesh_.triangleAttributes.data()), static_cast<std::streamsize>(rMesh_.triangleAttributes.size() * 4));
	std::ofstream jsonFile(rJsonPath_);
	if(!jsonFile) throw std::runtime_error(std::format("Collision: can't write {}", rJsonPath_));
	jsonFile << header.dump(2) << "\n";
}
}
