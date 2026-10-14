#include "GLTFFile.h"
#include "BasicMesh.h"
#include <cgltf.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <fstream>
#include <map>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace External
{
namespace
{
/* One triangle-list primitive with its node's world transform applied. */
struct Primitive
{
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec2> uvs;
	std::vector<glm::vec4> colors;
	std::vector<uint32_t> indices;
	const cgltf_material* pMaterial = nullptr;
};

/* Owns the parsed file; the buffers are loaded, so the accessors can be read. */
struct File
{
	cgltf_data* pData = nullptr;
	std::string path;

	explicit File(const std::string& rPath_) : path(rPath_)
	{
		cgltf_options options{};
		cgltf_result result = cgltf_parse_file(&options, rPath_.c_str(), &this->pData);
		if(result != cgltf_result_success) throw std::runtime_error(std::format("glTF '{}': can't read the file (cgltf error {}).", rPath_, static_cast<int>(result)));
		result = cgltf_load_buffers(&options, this->pData, rPath_.c_str());
		if(result != cgltf_result_success) throw std::runtime_error(std::format("glTF '{}': can't load its buffers (cgltf error {}).", rPath_, static_cast<int>(result)));
		result = cgltf_validate(this->pData);
		if(result != cgltf_result_success) throw std::runtime_error(std::format("glTF '{}': the file is not valid (cgltf error {}).", rPath_, static_cast<int>(result)));
	}
	~File() { cgltf_free(this->pData); }
	File(const File&) = delete;
	File& operator=(const File&) = delete;
};

const cgltf_accessor* findAttribute(const cgltf_primitive& rPrimitive_, cgltf_attribute_type type_)
{
	for(cgltf_size i = 0; i < rPrimitive_.attributes_count; i++)
	{
		const cgltf_attribute& rAttribute = rPrimitive_.attributes[i];
		if(rAttribute.type == type_ && rAttribute.index == 0) return rAttribute.data;
	}
	return nullptr;
}

/* Calls the function for every triangle-list primitive of every node that has a mesh, in the file's order. */
void forEachPrimitive(const File& rFile_, const std::function<void(const Primitive&)>& rFunction_)
{
	const cgltf_data& rData = *rFile_.pData;
	for(cgltf_size n = 0; n < rData.nodes_count; n++)
	{
		const cgltf_node& rNode = rData.nodes[n];
		if(rNode.mesh == nullptr) continue;
		float matrixValues[16];
		cgltf_node_transform_world(&rNode, matrixValues);
		glm::mat4 world = glm::make_mat4(matrixValues);
		glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));

		for(cgltf_size p = 0; p < rNode.mesh->primitives_count; p++)
		{
			const cgltf_primitive& rSource = rNode.mesh->primitives[p];
			if(rSource.type != cgltf_primitive_type_triangles)
			{
				fprintf(stderr, "glTF '%s': a primitive of mesh '%s' is not a triangle list, skipped.\n", rFile_.path.c_str(), rNode.mesh->name != nullptr ? rNode.mesh->name : "");
				continue;
			}
			const cgltf_accessor* pPositions = findAttribute(rSource, cgltf_attribute_type_position);
			if(pPositions == nullptr) continue;

			Primitive primitive;
			primitive.pMaterial = rSource.material;
			size_t count = pPositions->count;
			primitive.positions.resize(count);
			for(size_t i = 0; i < count; i++)
			{
				glm::vec3 position(0.0f);
				cgltf_accessor_read_float(pPositions, i, glm::value_ptr(position), 3);
				primitive.positions[i] = glm::vec3(world * glm::vec4(position, 1.0f));
			}

			if(const cgltf_accessor* pNormals = findAttribute(rSource, cgltf_attribute_type_normal); pNormals != nullptr && pNormals->count == count)
			{
				primitive.normals.resize(count);
				for(size_t i = 0; i < count; i++)
				{
					glm::vec3 normal(0.0f, 1.0f, 0.0f);
					cgltf_accessor_read_float(pNormals, i, glm::value_ptr(normal), 3);
					primitive.normals[i] = glm::normalize(normalMatrix * normal);
				}
			}
			if(const cgltf_accessor* pUvs = findAttribute(rSource, cgltf_attribute_type_texcoord); pUvs != nullptr && pUvs->count == count)
			{
				primitive.uvs.resize(count);
				for(size_t i = 0; i < count; i++)
				{
					glm::vec2 uv(0.0f);
					cgltf_accessor_read_float(pUvs, i, glm::value_ptr(uv), 2);
					primitive.uvs[i] = uv;
				}
			}
			if(const cgltf_accessor* pColors = findAttribute(rSource, cgltf_attribute_type_color); pColors != nullptr && pColors->count == count)
			{
				size_t components = cgltf_num_components(pColors->type);
				primitive.colors.resize(count);
				for(size_t i = 0; i < count; i++)
				{
					glm::vec4 color(1.0f);
					cgltf_accessor_read_float(pColors, i, glm::value_ptr(color), components);
					primitive.colors[i] = color;
				}
			}

			if(rSource.indices != nullptr)
			{
				primitive.indices.resize(rSource.indices->count);
				for(size_t i = 0; i < primitive.indices.size(); i++) primitive.indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(rSource.indices, i));
			}
			else
			{
				primitive.indices.resize(count);
				for(size_t i = 0; i < count; i++) primitive.indices[i] = static_cast<uint32_t>(i);
			}
			primitive.indices.resize(primitive.indices.size() / 3 * 3);
			for(uint32_t index : primitive.indices)
			{
				if(index >= count) throw std::runtime_error(std::format("glTF '{}': an index is past the vertices of mesh '{}'.", rFile_.path, rNode.mesh->name != nullptr ? rNode.mesh->name : ""));
			}
			rFunction_(primitive);
		}
	}
}

/* The face normals of the triangles a vertex is in, added up and normalised. */
std::vector<glm::vec3> faceNormals(const Primitive& rPrimitive_)
{
	std::vector<glm::vec3> normals(rPrimitive_.positions.size(), glm::vec3(0.0f));
	for(size_t t = 0; t + 2 < rPrimitive_.indices.size(); t += 3)
	{
		uint32_t a = rPrimitive_.indices[t], b = rPrimitive_.indices[t + 1], c = rPrimitive_.indices[t + 2];
		glm::vec3 normal = glm::cross(rPrimitive_.positions[b] - rPrimitive_.positions[a], rPrimitive_.positions[c] - rPrimitive_.positions[a]);
		normals[a] += normal;
		normals[b] += normal;
		normals[c] += normal;
	}
	for(glm::vec3& rNormal : normals) rNormal = glm::length(rNormal) > 0.0f ? glm::normalize(rNormal) : glm::vec3(0.0f, 1.0f, 0.0f);
	return normals;
}

/* Center of the box around the points and the farthest point from it. */
Graphics::BoundingSphere boundsOf(const std::vector<glm::vec3>& rPoints_)
{
	Graphics::BoundingSphere sphere;
	if(rPoints_.empty()) return sphere;
	glm::vec3 minimum = rPoints_[0], maximum = rPoints_[0];
	for(const glm::vec3& rPoint : rPoints_)
	{
		minimum = glm::min(minimum, rPoint);
		maximum = glm::max(maximum, rPoint);
	}
	sphere.center = (minimum + maximum) * 0.5f;
	for(const glm::vec3& rPoint : rPoints_) sphere.radius = std::max(sphere.radius, glm::length(rPoint - sphere.center));
	sphere.valid = true;
	return sphere;
}

/* The bytes of an image: from the .glb's buffer view, a data: uri, or a file next to the glTF. */
Graphics::Texture decodeImage(const File& rFile_, const cgltf_image& rImage_)
{
	std::vector<unsigned char> bytes;
	if(rImage_.buffer_view != nullptr)
	{
		const unsigned char* pBegin = static_cast<const unsigned char*>(rImage_.buffer_view->buffer->data) + rImage_.buffer_view->offset;
		bytes.assign(pBegin, pBegin + rImage_.buffer_view->size);
	}
	else if(rImage_.uri != nullptr && std::strncmp(rImage_.uri, "data:", 5) == 0)
	{
		const char* pComma = std::strchr(rImage_.uri, ',');
		if(pComma == nullptr) throw std::runtime_error(std::format("glTF '{}': an image has a broken data uri.", rFile_.path));
		std::string base64 = pComma + 1;
		cgltf_options options{};
		void* pDecoded = nullptr;
		cgltf_size size = base64.size() / 4 * 3;
		for(size_t i = base64.size(); i > 0 && base64[i - 1] == '='; i--) size--;
		if(cgltf_load_buffer_base64(&options, size, base64.c_str(), &pDecoded) != cgltf_result_success) throw std::runtime_error(std::format("glTF '{}': an image's data uri can't be decoded.", rFile_.path));
		bytes.assign(static_cast<unsigned char*>(pDecoded), static_cast<unsigned char*>(pDecoded) + size);
		std::free(pDecoded);
	}
	else if(rImage_.uri != nullptr)
	{
		std::string uri = rImage_.uri;
		cgltf_decode_uri(uri.data());
		uri.resize(std::strlen(uri.c_str()));
		std::filesystem::path imagePath = std::filesystem::path(rFile_.path).parent_path() / uri;
		FILE* pFile = fopen(imagePath.string().c_str(), "rb");
		if(pFile == nullptr) throw std::runtime_error(std::format("glTF '{}': can't open the image {}.", rFile_.path, imagePath.string()));
		unsigned char chunk[4096];
		size_t read;
		while((read = fread(chunk, 1, sizeof(chunk), pFile)) > 0) bytes.insert(bytes.end(), chunk, chunk + read);
		fclose(pFile);
	}
	else throw std::runtime_error(std::format("glTF '{}': an image has no data.", rFile_.path));

	int width = 0, height = 0, channels = 0;
	unsigned char* pPixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
	if(pPixels == nullptr) throw std::runtime_error(std::format("glTF '{}': an image can't be decoded: {}", rFile_.path, stbi_failure_reason()));
	Graphics::Texture texture(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 4);
	std::memcpy(texture.pixels.data(), pPixels, texture.pixels.size());
	stbi_image_free(pPixels);
	return texture;
}
}

ModelData readGLTFModel(const std::string& rPath_, const std::string& rModelName_, std::vector<Graphics::Texture>& rTextures_, std::vector<std::string>& rTextureNames_)
{
	File file(rPath_);
	ModelData model;
	model.name = rModelName_;
	model.layout = Graphics::VertexLayout::standard();

	/* image index -> index in rTextures_, so two materials sharing an image share the texture. */
	std::map<cgltf_size, size_t> textureOfImage;
	std::vector<glm::vec3> allPositions;

	forEachPrimitive(file, [&](const Primitive& rPrimitive_)
	{
		ModelSubmesh submesh;
		std::vector<glm::vec3> generated;
		if(rPrimitive_.normals.empty()) generated = faceNormals(rPrimitive_);
		const std::vector<glm::vec3>& rNormals = rPrimitive_.normals.empty() ? generated : rPrimitive_.normals;

		glm::vec4 baseColor(1.0f);
		const cgltf_texture* pTexture = nullptr;
		if(rPrimitive_.pMaterial != nullptr && rPrimitive_.pMaterial->has_pbr_metallic_roughness)
		{
			const cgltf_pbr_metallic_roughness& rPbr = rPrimitive_.pMaterial->pbr_metallic_roughness;
			baseColor = glm::make_vec4(rPbr.base_color_factor);
			pTexture = rPbr.base_color_texture.texture;
		}
		if(pTexture != nullptr && pTexture->image != nullptr)
		{
			cgltf_size imageIndex = cgltf_image_index(file.pData, pTexture->image);
			auto found = textureOfImage.find(imageIndex);
			if(found == textureOfImage.end())
			{
				Graphics::Texture texture = decodeImage(file, *pTexture->image);
				texture.filter = GL_LINEAR;
				texture.wrap = GL_REPEAT;
				if(pTexture->sampler != nullptr)
				{
					if(pTexture->sampler->mag_filter == cgltf_filter_type_nearest) texture.filter = GL_NEAREST;
					if(pTexture->sampler->wrap_s == cgltf_wrap_mode_clamp_to_edge) texture.wrap = GL_CLAMP_TO_EDGE;
					if(pTexture->sampler->wrap_s == cgltf_wrap_mode_mirrored_repeat) texture.wrap = GL_MIRRORED_REPEAT;
				}
				std::string name = pTexture->image->name != nullptr && pTexture->image->name[0] != '\0' ? pTexture->image->name : std::format("{}_{}", rModelName_, imageIndex);
				rTextures_.push_back(std::move(texture));
				rTextureNames_.push_back(name);
				found = textureOfImage.emplace(imageIndex, rTextures_.size() - 1).first;
			}
			submesh.texture = rTextureNames_[found->second];
		}

		std::vector<Graphics::Vertex> vertices(rPrimitive_.positions.size());
		for(size_t i = 0; i < vertices.size(); i++)
		{
			vertices[i].position = rPrimitive_.positions[i];
			vertices[i].normal = rNormals[i];
			vertices[i].uv = rPrimitive_.uvs.empty() ? glm::vec2(0.0f) : rPrimitive_.uvs[i];
			vertices[i].color = (rPrimitive_.colors.empty() ? glm::vec4(1.0f) : rPrimitive_.colors[i]) * baseColor;
		}
		submesh.vertexCount = static_cast<uint32_t>(vertices.size());
		submesh.vertices.resize(vertices.size() * sizeof(Graphics::Vertex));
		if(!vertices.empty()) std::memcpy(submesh.vertices.data(), vertices.data(), submesh.vertices.size());
		submesh.indices = rPrimitive_.indices;
		allPositions.insert(allPositions.end(), rPrimitive_.positions.begin(), rPrimitive_.positions.end());
		model.submeshes.push_back(std::move(submesh));
	});

	if(model.submeshes.empty()) throw std::runtime_error(std::format("glTF '{}': no triangle mesh in the file.", rPath_));
	model.bounds = boundsOf(allPositions);
	return model;
}

Physics::CollisionMesh readGLTFCollision(const std::string& rPath_)
{
	File file(rPath_);
	Physics::CollisionMesh mesh;
	forEachPrimitive(file, [&](const Primitive& rPrimitive_)
	{
		if(rPrimitive_.indices.empty()) return;
		Physics::CollisionPiece piece;
		piece.flags = 0;
		Graphics::BoundingSphere sphere = boundsOf(rPrimitive_.positions);
		piece.center = sphere.center;
		piece.radius = sphere.radius;
		piece.firstTriangle = static_cast<uint32_t>(mesh.indices.size() / 3);
		piece.triangleCount = static_cast<uint32_t>(rPrimitive_.indices.size() / 3);
		uint32_t firstVertex = static_cast<uint32_t>(mesh.vertices.size());
		mesh.vertices.insert(mesh.vertices.end(), rPrimitive_.positions.begin(), rPrimitive_.positions.end());
		for(uint32_t index : rPrimitive_.indices) mesh.indices.push_back(firstVertex + index);
		mesh.pieces.push_back(piece);
	});
	if(mesh.pieces.empty()) throw std::runtime_error(std::format("glTF '{}': no triangle mesh in the file.", rPath_));
	return mesh;
}

void writeGLTF(const std::string& rPath_, const GLTFScene& rScene_, FieldEncoding encoding_)
{
	using Json = nlohmann::ordered_json;
	constexpr uint32_t FloatType = 5126, UintType = 5125, ArrayTarget = 34962, ElementTarget = 34963, MaxExactFloat = 16777215;

	auto fail = [&](const std::string& rWhat_) { throw std::runtime_error(std::format("glTF '{}': {}", rPath_, rWhat_)); };
	std::filesystem::path gltfPath(rPath_);
	std::filesystem::path binPath = gltfPath;
	binPath.replace_extension(".bin");

	std::vector<unsigned char> blob;
	Json bufferViews = Json::array();
	Json accessors = Json::array();

	/* Every element is 4 bytes, so each array starts aligned. Returns the accessor index. */
	auto addAccessor = [&](const void* pData_, size_t count_, size_t components_, uint32_t componentType_, uint32_t target_, const char* pType_, const Json& rBounds_) -> int
	{
		size_t bytes = count_ * components_ * 4;
		Json view = {{"buffer", 0}, {"byteOffset", blob.size()}, {"byteLength", bytes}, {"target", target_}};
		const unsigned char* pBytes = static_cast<const unsigned char*>(pData_);
		blob.insert(blob.end(), pBytes, pBytes + bytes);
		bufferViews.push_back(view);
		Json accessor = {{"bufferView", bufferViews.size() - 1}, {"componentType", componentType_}, {"count", count_}, {"type", pType_}};
		if(!rBounds_.is_null()) { accessor["min"] = rBounds_["min"]; accessor["max"] = rBounds_["max"]; }
		accessors.push_back(accessor);
		return static_cast<int>(accessors.size() - 1);
	};
	auto vec3Accessor = [&](const std::vector<glm::vec3>& rValues_, uint32_t target_, bool bounds_) -> int
	{
		Json bounds;
		if(bounds_ && !rValues_.empty())
		{
			glm::vec3 minimum = rValues_[0], maximum = rValues_[0];
			for(const glm::vec3& rValue : rValues_) { minimum = glm::min(minimum, rValue); maximum = glm::max(maximum, rValue); }
			bounds = {{"min", {minimum.x, minimum.y, minimum.z}}, {"max", {maximum.x, maximum.y, maximum.z}}};
		}
		return addAccessor(rValues_.data(), rValues_.size(), 3, FloatType, target_, "VEC3", bounds);
	};
	auto checkSize = [&](size_t size_, size_t count_, const char* pName_, size_t mesh_)
	{
		if(size_ != 0 && size_ != count_) fail(std::format("mesh {}: {} has {} entries for {} positions.", mesh_, pName_, size_, count_));
	};

	Json meshes = Json::array();
	for(size_t m = 0; m < rScene_.meshes.size(); m++)
	{
		const GLTFMesh& rMesh = rScene_.meshes[m];
		Json primitives = Json::array();
		for(const GLTFPrimitive& rPrimitive : rMesh.primitives)
		{
			size_t count = rPrimitive.positions.size();
			checkSize(rPrimitive.normals.size(), count, "normals", m);
			checkSize(rPrimitive.uvs.size(), count, "uvs", m);
			checkSize(rPrimitive.colors.size(), count, "colors", m);
			checkSize(rPrimitive.src.size(), count, "src", m);
			checkSize(rPrimitive.srcFlag.size(), count, "srcFlag", m);
			checkSize(rPrimitive.srcIdx.size(), count, "srcIdx", m);
			checkSize(rPrimitive.orig.size(), count, "orig", m);
			if(rPrimitive.indices.size() % 3 != 0) fail(std::format("mesh {}: the index count is not a multiple of 3.", m));
			for(uint32_t index : rPrimitive.indices)
			{
				if(index >= count) fail(std::format("mesh {}: an index is past the vertices.", m));
			}
			if(rPrimitive.material >= static_cast<int>(rScene_.materials.size())) fail(std::format("mesh {}: bad material index {}.", m, rPrimitive.material));

			Json attributes = Json::object();
			attributes["POSITION"] = vec3Accessor(rPrimitive.positions, ArrayTarget, true);
			if(!rPrimitive.normals.empty()) attributes["NORMAL"] = vec3Accessor(rPrimitive.normals, ArrayTarget, false);
			if(!rPrimitive.uvs.empty()) attributes["TEXCOORD_0"] = addAccessor(rPrimitive.uvs.data(), count, 2, FloatType, ArrayTarget, "VEC2", Json());
			if(!rPrimitive.colors.empty()) attributes["COLOR_0"] = addAccessor(rPrimitive.colors.data(), count, 4, FloatType, ArrayTarget, "VEC4", Json());

			bool hasSource = !rPrimitive.src.empty() || !rPrimitive.srcFlag.empty() || !rPrimitive.srcIdx.empty();
			bool hasOrig = !rPrimitive.orig.empty();
			if(encoding_ == FieldEncoding::Custom)
			{
				if(!rPrimitive.src.empty()) attributes["_SRC"] = addAccessor(rPrimitive.src.data(), count, 1, UintType, ArrayTarget, "SCALAR", Json());
				if(!rPrimitive.srcFlag.empty()) attributes["_SRCFLAG"] = addAccessor(rPrimitive.srcFlag.data(), count, 1, UintType, ArrayTarget, "SCALAR", Json());
				if(!rPrimitive.srcIdx.empty()) attributes["_SRCIDX"] = addAccessor(rPrimitive.srcIdx.data(), count, 1, UintType, ArrayTarget, "SCALAR", Json());
				if(hasOrig) attributes["_ORIG"] = vec3Accessor(rPrimitive.orig, ArrayTarget, false);
			}
			else
			{
				auto exact = [&](const std::vector<uint32_t>& rValues_, size_t i_, const char* pName_) -> float
				{
					if(rValues_.empty()) return 0.0f;
					if(rValues_[i_] > MaxExactFloat) fail(std::format("mesh {}: {} {} is past 2^24 - 1, too large for the TexCoord encoding.", m, pName_, rValues_[i_]));
					return static_cast<float>(rValues_[i_]);
				};
				if(hasSource)
				{
					std::vector<glm::vec2> srcSet(count);
					for(size_t i = 0; i < count; i++) srcSet[i] = glm::vec2(exact(rPrimitive.src, i, "SRC"), exact(rPrimitive.srcFlag, i, "SRCFLAG"));
					attributes["TEXCOORD_1"] = addAccessor(srcSet.data(), count, 2, FloatType, ArrayTarget, "VEC2", Json());
				}
				if(hasOrig || !rPrimitive.srcIdx.empty())
				{
					std::vector<glm::vec2> xySet(count), zIdxSet(count);
					for(size_t i = 0; i < count; i++)
					{
						glm::vec3 orig = hasOrig ? rPrimitive.orig[i] : glm::vec3(0.0f);
						xySet[i] = glm::vec2(orig.x, orig.y);
						zIdxSet[i] = glm::vec2(orig.z, exact(rPrimitive.srcIdx, i, "SRCIDX"));
					}
					attributes["TEXCOORD_2"] = addAccessor(xySet.data(), count, 2, FloatType, ArrayTarget, "VEC2", Json());
					attributes["TEXCOORD_3"] = addAccessor(zIdxSet.data(), count, 2, FloatType, ArrayTarget, "VEC2", Json());
				}
			}

			Json primitive = {{"attributes", attributes}, {"mode", 4}};
			if(!rPrimitive.indices.empty()) primitive["indices"] = addAccessor(rPrimitive.indices.data(), rPrimitive.indices.size(), 1, UintType, ElementTarget, "SCALAR", Json());
			if(rPrimitive.material >= 0) primitive["material"] = rPrimitive.material;
			primitives.push_back(primitive);
		}
		meshes.push_back({{"name", rMesh.name}, {"primitives", primitives}});
	}

	Json nodes = Json::array();
	for(size_t n = 0; n < rScene_.nodes.size(); n++)
	{
		const GLTFNode& rNode = rScene_.nodes[n];
		Json node = {{"name", rNode.name}};
		if(rNode.mesh >= static_cast<int>(rScene_.meshes.size())) fail(std::format("node {}: bad mesh index {}.", n, rNode.mesh));
		if(rNode.mesh >= 0) node["mesh"] = rNode.mesh;
		if(!rNode.children.empty())
		{
			for(int child : rNode.children)
			{
				if(child < 0 || child >= static_cast<int>(rScene_.nodes.size())) fail(std::format("node {}: bad child index {}.", n, child));
			}
			node["children"] = rNode.children;
		}
		if(rNode.translation != glm::vec3(0.0f)) node["translation"] = {rNode.translation.x, rNode.translation.y, rNode.translation.z};
		if(rNode.rotation != glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) node["rotation"] = {rNode.rotation.x, rNode.rotation.y, rNode.rotation.z, rNode.rotation.w};
		if(rNode.scale != glm::vec3(1.0f)) node["scale"] = {rNode.scale.x, rNode.scale.y, rNode.scale.z};
		if(!rNode.extras.empty())
		{
			Json extras = Json::object();
			for(const auto& [key, value] : rNode.extras) extras[key] = value;
			node["extras"] = extras;
		}
		nodes.push_back(node);
	}
	for(int root : rScene_.rootNodes)
	{
		if(root < 0 || root >= static_cast<int>(rScene_.nodes.size())) fail(std::format("bad root node index {}.", root));
	}

	/* One texture per image, one shared sampler (linear, repeat; glTF's v = 0 is the first row, as in the stored textures). */
	Json materials = Json::array();
	for(size_t i = 0; i < rScene_.materials.size(); i++)
	{
		const GLTFMaterial& rMaterial = rScene_.materials[i];
		if(rMaterial.image >= static_cast<int>(rScene_.images.size())) fail(std::format("material {}: bad image index {}.", i, rMaterial.image));
		Json pbr = {{"baseColorFactor", {rMaterial.baseColor.r, rMaterial.baseColor.g, rMaterial.baseColor.b, rMaterial.baseColor.a}}, {"metallicFactor", 0.0}, {"roughnessFactor", 1.0}};
		if(rMaterial.image >= 0) pbr["baseColorTexture"] = {{"index", rMaterial.image}};
		materials.push_back({{"name", rMaterial.name}, {"pbrMetallicRoughness", pbr}});
	}
	Json images = Json::array(), textures = Json::array();
	for(size_t i = 0; i < rScene_.images.size(); i++)
	{
		images.push_back({{"uri", rScene_.images[i]}});
		textures.push_back({{"source", i}, {"sampler", 0}});
	}

	Json root = {{"asset", {{"version", "2.0"}, {"generator", "SonicSunset"}}}, {"scene", 0}, {"scenes", {{{"nodes", rScene_.rootNodes}}}}};
	root["nodes"] = nodes;
	root["meshes"] = meshes;
	if(!materials.empty()) root["materials"] = materials;
	if(!images.empty())
	{
		root["images"] = images;
		root["textures"] = textures;
		root["samplers"] = Json::array({{{"magFilter", 9729}, {"minFilter", 9729}, {"wrapS", 10497}, {"wrapT", 10497}}});
	}
	root["accessors"] = accessors;
	root["bufferViews"] = bufferViews;
	root["buffers"] = Json::array({{{"uri", binPath.filename().string()}, {"byteLength", blob.size()}}});

	std::ofstream bin(binPath, std::ios::binary);
	if(!bin) fail(std::format("can't open {} for writing.", binPath.string()));
	bin.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
	bin.close();
	if(!bin) fail(std::format("can't write {}.", binPath.string()));
	std::ofstream text(gltfPath);
	if(!text) fail("can't open the file for writing.");
	text << root.dump(1, '\t') << '\n';
	text.close();
	if(!text) fail("can't write the file.");
}
}
