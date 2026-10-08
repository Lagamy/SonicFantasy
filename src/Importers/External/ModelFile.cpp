#include "ModelFile.h"
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

constexpr uint32_t ModelVersion = 1;
const std::string JsonSuffix = ".model.json";

std::vector<unsigned char> readBytes(const std::string& rPath_)
{
	std::ifstream file(rPath_, std::ios::binary);
	if(!file) throw std::runtime_error(std::format("can't open {}", rPath_));
	return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/* True when count x unit bytes starting at offset_ fit in size_ (without overflowing). */
bool fits(uint64_t offset_, uint64_t count_, uint64_t unit_, uint64_t size_)
{
	if(unit_ != 0 && count_ > size_ / unit_) return false;
	uint64_t bytes = count_ * unit_;
	return offset_ <= size_ && bytes <= size_ - offset_;
}
}

std::string modelBinPath(const std::string& rJsonPath_)
{
	if(!rJsonPath_.ends_with(JsonSuffix)) throw std::runtime_error(std::format("Model '{}': the file name must end with {}", rJsonPath_, JsonSuffix));
	return rJsonPath_.substr(0, rJsonPath_.size() - JsonSuffix.size()) + ".model.bin";
}

ModelData readModel(const std::string& rJsonPath_)
{
	try
	{
		std::string binPath = modelBinPath(rJsonPath_);
		std::ifstream file(rJsonPath_);
		if(!file) throw std::runtime_error("can't open the file");
		Json header = Json::parse(file);
		if(header.at("version").get<uint32_t>() != ModelVersion) throw std::runtime_error(std::format("version {} is not supported", header.at("version").dump()));

		ModelData model;
		model.name = header.at("name").get<std::string>();
		for(const Json& rMember : header.at("layout"))
		{
			model.layout.addMember(rMember.at("name").get<std::string>().c_str(), Graphics::vertexMemberTypeFromName(rMember.at("type").get<std::string>()), rMember.at("location").get<uint32_t>());
		}
		if(model.layout.members.empty()) throw std::runtime_error("the layout has no members");

		std::vector<float> center = header.at("bounds").at("center").get<std::vector<float>>();
		if(center.size() != 3) throw std::runtime_error("bounds.center needs 3 numbers");
		model.bounds.center = glm::vec3(center[0], center[1], center[2]);
		model.bounds.radius = header.at("bounds").at("radius").get<float>();
		model.bounds.valid = true;

		std::vector<unsigned char> blob = readBytes(binPath);
		uint64_t stride = model.layout.stride;
		for(const Json& rSubmesh : header.at("submeshes"))
		{
			ModelSubmesh submesh;
			submesh.texture = rSubmesh.value("texture", std::string());
			uint64_t vertexCount = rSubmesh.at("vertexCount").get<uint64_t>();
			uint64_t indexCount = rSubmesh.at("indexCount").get<uint64_t>();
			uint64_t vertexOffset = rSubmesh.at("vertexOffset").get<uint64_t>();
			uint64_t indexOffset = rSubmesh.at("indexOffset").get<uint64_t>();
			size_t number = model.submeshes.size();
			if(!fits(vertexOffset, vertexCount, stride, blob.size()) || !fits(indexOffset, indexCount, sizeof(uint32_t), blob.size()))
			{
				throw std::runtime_error(std::format("submesh {} reaches past the end of {}", number, binPath));
			}
			submesh.vertexCount = static_cast<uint32_t>(vertexCount);
			submesh.vertices.assign(blob.begin() + vertexOffset, blob.begin() + vertexOffset + vertexCount * stride);
			submesh.indices.resize(indexCount);
			if(indexCount > 0) std::memcpy(submesh.indices.data(), blob.data() + indexOffset, indexCount * sizeof(uint32_t));
			for(uint32_t index : submesh.indices)
			{
				if(index >= vertexCount) throw std::runtime_error(std::format("submesh {} has index {} but {} vertices", number, index, vertexCount));
			}
			model.submeshes.push_back(std::move(submesh));
		}
		return model;
	}
	catch(const std::exception& rError)
	{
		throw std::runtime_error(std::format("Model '{}': {}", rJsonPath_, rError.what()));
	}
}

void writeModel(const std::string& rJsonPath_, const ModelData& rModel_)
{
	std::string binPath = modelBinPath(rJsonPath_);
	uint64_t stride = rModel_.layout.stride;

	/* Little-endian bytes are the host's own (x86), so vertices and indices are copied as they are. */
	std::vector<unsigned char> blob;
	Json submeshes = Json::array();
	for(size_t i = 0; i < rModel_.submeshes.size(); i++)
	{
		const ModelSubmesh& rSubmesh = rModel_.submeshes[i];
		if(rSubmesh.vertices.size() != rSubmesh.vertexCount * stride)
		{
			throw std::runtime_error(std::format("Model '{}': submesh {} has {} vertex bytes, not {} x {}", rJsonPath_, i, rSubmesh.vertices.size(), rSubmesh.vertexCount, stride));
		}
		uint64_t vertexOffset = blob.size();
		blob.insert(blob.end(), rSubmesh.vertices.begin(), rSubmesh.vertices.end());
		uint64_t indexOffset = blob.size();
		const unsigned char* pIndices = reinterpret_cast<const unsigned char*>(rSubmesh.indices.data());
		blob.insert(blob.end(), pIndices, pIndices + rSubmesh.indices.size() * sizeof(uint32_t));
		submeshes.push_back({{"texture", rSubmesh.texture}, {"vertexCount", rSubmesh.vertexCount}, {"indexCount", rSubmesh.indices.size()}, {"vertexOffset", vertexOffset}, {"indexOffset", indexOffset}});
	}

	Json layout = Json::array();
	for(const Graphics::VertexMember& rMember : rModel_.layout.members)
	{
		layout.push_back({{"name", rMember.name}, {"type", Graphics::vertexMemberTypeName(rMember.type)}, {"location", rMember.location}});
	}
	Json header = {{"version", ModelVersion}, {"name", rModel_.name}, {"layout", layout},
		{"bounds", {{"center", {rModel_.bounds.center.x, rModel_.bounds.center.y, rModel_.bounds.center.z}}, {"radius", rModel_.bounds.radius}}}, {"submeshes", submeshes}};

	std::ofstream binFile(binPath, std::ios::binary);
	if(!binFile) throw std::runtime_error(std::format("Model: can't write {}", binPath));
	binFile.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
	std::ofstream jsonFile(rJsonPath_);
	if(!jsonFile) throw std::runtime_error(std::format("Model: can't write {}", rJsonPath_));
	jsonFile << header.dump(2) << "\n";
}
}
