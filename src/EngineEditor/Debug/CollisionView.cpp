#include "CollisionView.h"
#include "GPU_API.h"
#include "Globals.h"
#include "Editor.h"
#include "SAMesh.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <cstddef>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
static void setupLineVAO(GLuint& rVAO_, GLuint vbo_)
{
	/* Same attribute locations as every shader (0 position, 3 color); the Default shader reads only those. */
	glCreateVertexArrays(1, &rVAO_);
	glVertexArrayAttribFormat(rVAO_, 0, 3, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribFormat(rVAO_, 3, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec3));
	glEnableVertexArrayAttrib(rVAO_, 0);
	glEnableVertexArrayAttrib(rVAO_, 3);
	glVertexArrayAttribBinding(rVAO_, 0, 0);
	glVertexArrayAttribBinding(rVAO_, 3, 0);
	glVertexArrayVertexBuffer(rVAO_, 0, vbo_, 0, sizeof(glm::vec3) + sizeof(glm::vec4));
}

void CollisionView::init()
{
	glCreateBuffers(1, &this->levelVBO);
	setupLineVAO(this->levelVAO, this->levelVBO);
	glCreateBuffers(1, &this->fillVBO);
	setupLineVAO(this->fillVAO, this->fillVBO);
	glCreateBuffers(1, &this->dynamicVBO);
	setupLineVAO(this->dynamicVAO, this->dynamicVBO);
}

/* Color of a triangle by the slope of its face (see CollisionView). */
static glm::vec4 slopeColor(const glm::vec3& rA_, const glm::vec3& rB_, const glm::vec3& rC_)
{
	glm::vec3 normal = glm::cross(rB_ - rA_, rC_ - rA_);
	float length = glm::length(normal);
	if(length <= 0.0f) return glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
	float up = normal.y / length;
	if(up > 0.7f) return glm::vec4(0.1f, 0.9f, 0.2f, 1.0f);
	if(up < -0.7f) return glm::vec4(0.2f, 0.4f, 1.0f, 1.0f);
	return glm::vec4(1.0f, 0.5f, 0.1f, 1.0f);
}

/*
A color for a raw attribute value: the hue steps by the golden ratio per value (so values close to each other, which are common, get
distinct hues), an attribute of 0 is white. Only a way to tell values apart.
*/
static glm::vec4 attributeColor(uint32_t attribute_)
{
	if(attribute_ == 0) return glm::vec4(1.0f);
	float hue = glm::fract(static_cast<float>(attribute_ % 4096) * 0.61803398875f) * 6.0f;
	glm::vec3 rgb = glm::clamp(glm::abs(glm::vec3(glm::mod(hue + 0.0f, 6.0f) - 3.0f, glm::mod(hue + 4.0f, 6.0f) - 3.0f, glm::mod(hue + 2.0f, 6.0f) - 3.0f)) - 1.0f, 0.0f, 1.0f);
	return glm::vec4(0.25f + 0.75f * rgb, 1.0f);
}

glm::vec4 CollisionView::colorOf(uint32_t value_)
{
	return attributeColor(value_);
}

void CollisionView::selectAttribute(int step_)
{
	int options = static_cast<int>(this->attributes.size()) + 1;
	this->selectedAttribute = ((this->selectedAttribute + 1 + step_) % options + options) % options - 1;
}

void CollisionView::selectEntityAttribute(int step_)
{
	int options = static_cast<int>(this->entityAttributes.size()) + 1;
	int current = -1;
	for(int i = 0; this->hasSelectedEntityValue && i < static_cast<int>(this->entityAttributes.size()); i++)
	{
		if(this->entityAttributes[i].attribute == this->selectedEntityValue) current = i;
	}
	int next = ((current + 1 + step_) % options + options) % options - 1;
	this->hasSelectedEntityValue = next >= 0;
	if(next >= 0) this->selectedEntityValue = this->entityAttributes[next].attribute;
}

/* The name of a behaviour (Globals::Game::behaviors) for the legends, "?" if the id is none. */
static std::string behaviorName(PoolId behaviorId_)
{
	return behaviorId_ != UninitializedPoolId ? Globals::Game::behaviors.names[behaviorId_.id] : "?";
}

/*
The value shown for each triangle of a mesh: its own attribute when the mesh stores them per triangle (Shadow), otherwise the surface
flags of the piece it is in (SADX: one per land table COL). Empty if the mesh has neither, which colors by slope.
*/
static std::vector<uint32_t> triangleValues(const Physics::CollisionMesh& rMesh_)
{
	uint32_t triangleCount = static_cast<uint32_t>(rMesh_.indices.size() / 3);
	if(rMesh_.triangleAttributes.size() == triangleCount) return rMesh_.triangleAttributes;

	std::vector<uint32_t> values;
	if(rMesh_.pieces.empty()) return values;
	values.assign(triangleCount, 0);
	for(const Physics::CollisionPiece& rPiece : rMesh_.pieces)
	{
		for(uint32_t t = rPiece.firstTriangle; t < rPiece.firstTriangle + rPiece.triangleCount && t < triangleCount; t++) values[t] = rPiece.flags;
	}
	return values;
}

/*
Fills the level buffer with the edges of the collisions' triangles (three lines each). While a value is selected only the triangles
with that value are kept.
*/
void CollisionView::rebuildLevelLines(const std::vector<PoolId>& rCollisionIds_)
{
	this->attributes.clear();
	std::vector<std::vector<uint32_t>> values;
	for(PoolId id : rCollisionIds_)
	{
		const Physics::Collision& rCollision = Globals::Physics::collisions[id];
		values.push_back(triangleValues(rCollision.mesh));
		for(uint32_t value : values.back())
		{
			auto found = std::find_if(this->attributes.begin(), this->attributes.end(), [&](const AttributeCount& rEntry_) { return rEntry_.attribute == value; });
			auto behavior = rCollision.behaviorByValue.find(value);
			if(found == this->attributes.end()) this->attributes.push_back(AttributeCount{value, 1, attributeColor(value), behaviorName(behavior != rCollision.behaviorByValue.end() ? behavior->second : rCollision.fallbackBehavior)});
			else found->count++;
		}
	}
	std::stable_sort(this->attributes.begin(), this->attributes.end(), [](const AttributeCount& rA_, const AttributeCount& rB_) { return rA_.count > rB_.count; });
	if(this->selectedAttribute >= static_cast<int>(this->attributes.size())) this->selectedAttribute = -1;
	bool haveSelected = this->colorByAttribute && this->selectedAttribute >= 0;
	uint32_t selectedValue = haveSelected ? this->attributes[this->selectedAttribute].attribute : 0;

	/*
	The lines are grouped by the grid cell of their triangle's center (CellSize units), the cell's lines kept together; the cell's box is the box of its triangles'
	corners, whose bounding sphere draw tests against the view.
	*/
	constexpr float CellSize = 256.0f;
	constexpr float FillAlpha = 0.25f;
	struct CellLines
	{
		std::vector<LineVertex> lines;
		std::vector<LineVertex> fill;
		glm::vec3 min = glm::vec3(1e30f);
		glm::vec3 max = glm::vec3(-1e30f);
	};
	std::map<std::tuple<int, int, int>, CellLines> cells;
	for(size_t c = 0; c < rCollisionIds_.size(); c++)
	{
		const Physics::Collision& rCollision = Globals::Physics::collisions[rCollisionIds_[c]];
		const Physics::CollisionMesh& rMesh = rCollision.mesh;
		bool haveValues = this->colorByAttribute && !values[c].empty();
		for(size_t i = 0; i + 2 < rMesh.indices.size(); i += 3)
		{
			if(haveSelected && haveValues && values[c][i / 3] != selectedValue) continue;
			const glm::vec3 corners[3] = {rMesh.vertices[rMesh.indices[i]] + rCollision.position, rMesh.vertices[rMesh.indices[i + 1]] + rCollision.position, rMesh.vertices[rMesh.indices[i + 2]] + rCollision.position};
			glm::vec4 color = haveValues ? attributeColor(values[c][i / 3]) : slopeColor(corners[0], corners[1], corners[2]);
			glm::ivec3 cellIndex = glm::ivec3(glm::floor((corners[0] + corners[1] + corners[2]) / (3.0f * CellSize)));
			CellLines& rCell = cells[std::make_tuple(cellIndex.x, cellIndex.y, cellIndex.z)];
			glm::vec4 fillColor = color;
			fillColor.a = FillAlpha;
			for(int e = 0; e < 3; e++)
			{
				rCell.fill.push_back(LineVertex{corners[e], fillColor});
				rCell.lines.push_back(LineVertex{corners[e], color});
				rCell.lines.push_back(LineVertex{corners[(e + 1) % 3], color});
				rCell.min = glm::min(rCell.min, corners[e]);
				rCell.max = glm::max(rCell.max, corners[e]);
			}
		}
	}
	std::vector<LineVertex> lines;
	std::vector<LineVertex> fill;
	this->levelCells.clear();
	for(const auto& rEntry : cells)
	{
		const CellLines& rCell = rEntry.second;
		this->levelCells.push_back(LineCell{(rCell.min + rCell.max) * 0.5f, glm::length(rCell.max - rCell.min) * 0.5f, static_cast<GLint>(lines.size()), static_cast<GLsizei>(rCell.lines.size()), static_cast<GLint>(fill.size()), static_cast<GLsizei>(rCell.fill.size())});
		lines.insert(lines.end(), rCell.lines.begin(), rCell.lines.end());
		fill.insert(fill.end(), rCell.fill.begin(), rCell.fill.end());
	}
	this->levelVertexCount = static_cast<GLsizei>(lines.size());

	/* Immutable storage can't be resized: the buffer is replaced. */
	glDeleteBuffers(1, &this->levelVBO);
	glDeleteVertexArrays(1, &this->levelVAO);
	glCreateBuffers(1, &this->levelVBO);
	if(!lines.empty()) glNamedBufferStorage(this->levelVBO, lines.size() * sizeof(LineVertex), lines.data(), 0);
	setupLineVAO(this->levelVAO, this->levelVBO);
	glDeleteBuffers(1, &this->fillVBO);
	glDeleteVertexArrays(1, &this->fillVAO);
	glCreateBuffers(1, &this->fillVBO);
	if(!fill.empty()) glNamedBufferStorage(this->fillVBO, fill.size() * sizeof(LineVertex), fill.data(), 0);
	setupLineVAO(this->fillVAO, this->fillVBO);
	this->shownLevelCollisions = rCollisionIds_;
	this->shownByAttribute = this->colorByAttribute;
	this->shownSelected = this->selectedAttribute;
}

/*
Outline of an object collision shape (Physics::CollisionShape) at a position, as line segments through addLine_. Only a visualization of the
forms the init code of the game suggests (hypotheses, see CollisionShape.h): form 0 a sphere of radius a (three circles); forms 1 and 2
a vertical cylinder of radius a and height b centered on the shape's center (observed: standing characters' tables have center y about half of b);
forms 3 to 5 and 7 a box with half sizes a, b, c. Shadow's .CCL shapes (readCCL) use two forms of their own, both along the shape's z axis (hypothesis, see
readCCL): 6 a capsule of radius a whose segment is 2 * b long, and 11 a cylinder of radius a and length 2 * b; SADX's meaning of form 6 is unknown. Anything
else (8 to 10) a circle of radius a and a cross. The shape's angles (BAMS, axis order unknown: Graphics::composeNinjaTransform's) rotate it about its center.
*/
template <typename AddLine>
static void addShapeLines(AddLine addLine_, const Physics::CollisionShape& rShape_, const glm::vec3& rPosition_, const glm::mat4& rEntityRotation_, bool sadx_, const glm::vec4& rColor_)
{
	glm::mat4 rotation = rEntityRotation_ * composeNinjaTransform(glm::vec3(0.0f), glm::vec3(rShape_.angle), glm::vec3(1.0f), NinjaNoPosition | NinjaNoScale);

	/*
	An SADX form 6 is drawn as a capsule along y, not z as Shadow's (see below): observed on the palm trees' table (_Stg01.rel 0x24A0F8), whose one shape is form 6 with
	a = 3, b = 20 and center (0, 20, 0), a trunk standing on the ground when its segment runs along y, which is where its center and the half length b put it (hypothesis for the form).
	*/
	if(sadx_ && rShape_.form == 6)
	{
		rotation = rotation * glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
	}
	glm::vec3 origin = rPosition_ + glm::vec3(rEntityRotation_ * glm::vec4(rShape_.center, 0.0f));
	auto place = [&](const glm::vec3& rLocal_) { return origin + glm::vec3(rotation * glm::vec4(rLocal_, 1.0f)); };
	constexpr int Segments = 16;
	auto circle = [&](int axis_, float radius_, float offset_)
	{
		for(int i = 0; i < Segments; i++)
		{
			float from = glm::two_pi<float>() * i / Segments;
			float to = glm::two_pi<float>() * (i + 1) / Segments;
			glm::vec3 p0(0.0f);
			glm::vec3 p1(0.0f);
			int u = (axis_ + 1) % 3;
			int v = (axis_ + 2) % 3;
			p0[u] = std::cos(from) * radius_; p0[v] = std::sin(from) * radius_; p0[axis_] = offset_;
			p1[u] = std::cos(to) * radius_; p1[v] = std::sin(to) * radius_; p1[axis_] = offset_;
			addLine_(place(p0), place(p1), rColor_);
		}
	};

	if(rShape_.form == 0)
	{
		for(int axis = 0; axis < 3; axis++) circle(axis, rShape_.a, 0.0f);
	}
	else if(rShape_.form == 1 || rShape_.form == 2)
	{
		float half = rShape_.b * 0.5f;
		circle(1, rShape_.a, -half);
		circle(1, rShape_.a, half);
		for(int i = 0; i < 4; i++)
		{
			float angle = glm::half_pi<float>() * i;
			glm::vec3 side(std::cos(angle) * rShape_.a, 0.0f, std::sin(angle) * rShape_.a);
			addLine_(place(side + glm::vec3(0.0f, -half, 0.0f)), place(side + glm::vec3(0.0f, half, 0.0f)), rColor_);
		}
	}
	else if(rShape_.form == 6 || rShape_.form == 11)
	{
		circle(2, rShape_.a, -rShape_.b);
		circle(2, rShape_.a, rShape_.b);
		for(int i = 0; i < 4; i++)
		{
			float angle = glm::half_pi<float>() * i;
			glm::vec3 side(std::cos(angle) * rShape_.a, std::sin(angle) * rShape_.a, 0.0f);
			addLine_(place(side + glm::vec3(0.0f, 0.0f, -rShape_.b)), place(side + glm::vec3(0.0f, 0.0f, rShape_.b)), rColor_);
		}
		/* A capsule's caps: a half circle in each of two planes at each end of the segment. */
		if(rShape_.form == 6)
		{
			constexpr int Half = Segments / 2;
			for(int sideAxis = 0; sideAxis < 2; sideAxis++)
			{
				for(float end : {-1.0f, 1.0f})
				{
					for(int i = 0; i < Half; i++)
					{
						float from = glm::pi<float>() * i / Half;
						float to = glm::pi<float>() * (i + 1) / Half;
						glm::vec3 p0(0.0f);
						glm::vec3 p1(0.0f);
						p0[sideAxis] = std::cos(from) * rShape_.a; p0[2] = end * (rShape_.b + std::sin(from) * rShape_.a);
						p1[sideAxis] = std::cos(to) * rShape_.a; p1[2] = end * (rShape_.b + std::sin(to) * rShape_.a);
						addLine_(place(p0), place(p1), rColor_);
					}
				}
			}
		}
	}
	else if((rShape_.form >= 3 && rShape_.form <= 5) || rShape_.form == 7)
	{
		glm::vec3 half(rShape_.a, rShape_.b, rShape_.c);
		for(int i = 0; i < 8; i++)
		{
			glm::vec3 corner((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
			for(int axis = 0; axis < 3; axis++)
			{
				if(i & (1 << axis)) continue;
				glm::vec3 other = corner;
				other[axis] = -corner[axis];
				addLine_(place(corner), place(other), rColor_);
			}
		}
	}
	else
	{
		circle(1, rShape_.a, 0.0f);
		for(int axis = 0; axis < 3; axis++)
		{
			glm::vec3 offset(0.0f);
			offset[axis] = 1.0f;
			addLine_(place(-offset), place(offset), rColor_);
		}
	}
}

void CollisionView::draw(const Camera& rCamera_)
{
	if(!editor.active || (!this->enabled && !this->boundAreaTabActive && this->markers.empty())) return;

	std::vector<PoolId> levelCollisions;
	if(this->enabled)
	{
		for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
		{
			if(!Globals::Game::levels.alive[i]) continue;
			PoolId id = Globals::Game::levels.objects[i].collisionId;
			if(id != UninitializedPoolId) levelCollisions.push_back(id);
		}
		if(levelCollisions != this->shownLevelCollisions || this->colorByAttribute != this->shownByAttribute || this->selectedAttribute != this->shownSelected) this->rebuildLevelLines(levelCollisions);
	}

	std::vector<LineVertex> dynamic;
	auto addLine = [&](const glm::vec3& rFrom_, const glm::vec3& rTo_, const glm::vec4& rColor_)
	{
		dynamic.push_back(LineVertex{rFrom_, rColor_});
		dynamic.push_back(LineVertex{rTo_, rColor_});
	};
	float distance = std::min(this->drawDistance, rCamera_.farPlane);
	std::array<glm::vec4, 6> entityPlanes = rCamera_.frustumPlanes();
	std::map<uint32_t, std::pair<uint32_t, PoolId>> shapeValues;
	bool drawEntities = this->enabled || this->boundAreaTabActive;
	for(uint32_t i = 0; drawEntities && i < Globals::Game::entities.objects.size(); i++)
	{
		if(!Globals::Game::entities.alive[i]) continue;
		const Entity& rEntity = Globals::Game::entities.objects[i];
		glm::vec3 entityPosition = rEntity.renderPosition();
		if(glm::distance(entityPosition, rCamera_.position) > distance) continue;

		/* An entity outside the view (with a margin for a big shape far from its position) is not drawn: building every shape's lines every frame is expensive. */
		constexpr float EntityCullRadius = 150.0f;
		if(!sphereInFrustum(entityPlanes, entityPosition, EntityCullRadius)) continue;
		glm::mat4 entityRotation = rEntity.renderRotation();
		if(rEntity.physicsId != UninitializedPoolId)
		{
			const glm::vec3& rPosition = entityPosition;
			addLine(rPosition + glm::vec3(0.0f, Physics::ProbeStartHeight, 0.0f), rPosition, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
			addLine(rPosition - glm::vec3(1.0f, 0.0f, 0.0f), rPosition + glm::vec3(1.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
			addLine(rPosition - glm::vec3(0.0f, 0.0f, 1.0f), rPosition + glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
		}
		if(rEntity.collisionId != UninitializedPoolId)
		{
			const Physics::Collision& rCollision = Globals::Physics::collisions[rEntity.collisionId];
			for(const Physics::CollisionShape& rShape : rCollision.shapes)
			{
				std::pair<uint32_t, PoolId>& rValue = shapeValues[rShape.attr];
				rValue.first++;
				rValue.second = rShape.behavior;
				if(this->colorByAttribute && this->hasSelectedEntityValue && rShape.attr != this->selectedEntityValue) continue;
				glm::vec4 shapeColor = this->colorByAttribute ? attributeColor(rShape.attr) : glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
				addShapeLines(addLine, Physics::scaledShape(rShape, rEntity.collisionScale(), rEntity.sourceEngine == SA_NINJA), entityPosition, rEntity.sourceEngine == SA_NINJA ? entityRotation : glm::mat4(1.0f), rEntity.sourceEngine == SA_NINJA, shapeColor);
			}
			const Physics::CollisionMesh& rMesh = rCollision.mesh;
			for(size_t t = 0; t + 2 < rMesh.indices.size(); t += 3)
			{
				const glm::vec3 corners[3] = {rMesh.vertices[rMesh.indices[t]] + entityPosition, rMesh.vertices[rMesh.indices[t + 1]] + entityPosition, rMesh.vertices[rMesh.indices[t + 2]] + entityPosition};
				for(int c = 0; c < 3; c++) addLine(corners[c], corners[(c + 1) % 3], glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
			}
		}
	}

	this->entityAttributes.clear();
	for(const auto& rValue : shapeValues) this->entityAttributes.push_back(AttributeCount{rValue.first, rValue.second.first, attributeColor(rValue.first), behaviorName(rValue.second.second)});
	std::stable_sort(this->entityAttributes.begin(), this->entityAttributes.end(), [](const AttributeCount& rA_, const AttributeCount& rB_) { return rA_.count > rB_.count; });

	for(const Marker& rMarker : this->markers)
	{
		if(glm::distance(rMarker.position, rCamera_.position) > distance) continue;
		constexpr float MarkerHalfSize = 8.0f;
		glm::vec4 color = attributeColor(rMarker.value);
		for(int axis = 0; axis < 3; axis++)
		{
			glm::vec3 offset(0.0f);
			offset[axis] = MarkerHalfSize;
			addLine(rMarker.position - offset, rMarker.position + offset, color);
		}
	}

	GLsizeiptr dynamicBytes = static_cast<GLsizeiptr>(dynamic.size() * sizeof(LineVertex));
	if(dynamicBytes > this->dynamicCapacity)
	{
		glDeleteBuffers(1, &this->dynamicVBO);
		glDeleteVertexArrays(1, &this->dynamicVAO);
		glCreateBuffers(1, &this->dynamicVBO);
		this->dynamicCapacity = std::max<GLsizeiptr>(dynamicBytes * 2, 4096);
		glNamedBufferStorage(this->dynamicVBO, this->dynamicCapacity, nullptr, GL_DYNAMIC_STORAGE_BIT);
		setupLineVAO(this->dynamicVAO, this->dynamicVBO);
	}
	if(dynamicBytes > 0) glNamedBufferSubData(this->dynamicVBO, 0, dynamicBytes, dynamic.data());

	Shader& rShader = getShaderByName("Default");
	glUseProgram(rShader.id);
	glm::mat4 identity(1.0f);
	glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(identity));

	if(this->enabled && this->levelVertexCount > 0)
	{
		/* The cells that are within drawDistance (and the far plane) and not outside the frustum, drawn as ranges of the one buffer. */
		std::array<glm::vec4, 6> planes = rCamera_.frustumPlanes();
		std::vector<GLint> firsts;
		std::vector<GLsizei> counts;
		std::vector<GLint> fillFirsts;
		std::vector<GLsizei> fillCounts;
		for(const LineCell& rCell : this->levelCells)
		{
			if(glm::distance(rCell.center, rCamera_.position) - rCell.radius > distance) continue;
			if(!sphereInFrustum(planes, rCell.center, rCell.radius)) continue;
			firsts.push_back(rCell.first);
			counts.push_back(rCell.count);
			fillFirsts.push_back(rCell.fillFirst);
			fillCounts.push_back(rCell.fillCount);
		}

		/* The semi transparent faces first, depth tested but not written and drawn from both sides, so the lines and the scene behind them stay visible. */
		glBindVertexArray(this->fillVAO);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);
		if(!fillFirsts.empty()) glMultiDrawArrays(GL_TRIANGLES, fillFirsts.data(), fillCounts.data(), static_cast<GLsizei>(fillFirsts.size()));
		glEnable(GL_CULL_FACE);
		glDepthMask(GL_TRUE);
		glBindVertexArray(this->levelVAO);
		if(!firsts.empty()) glMultiDrawArrays(GL_LINES, firsts.data(), counts.data(), static_cast<GLsizei>(firsts.size()));
	}
	if(!dynamic.empty())
	{
		glDisable(GL_DEPTH_TEST);
		glBindVertexArray(this->dynamicVAO);
		glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(dynamic.size()));
		glEnable(GL_DEPTH_TEST);
	}
}

void CollisionView::destroy()
{
	glDeleteVertexArrays(1, &this->levelVAO);
	glDeleteBuffers(1, &this->levelVBO);
	glDeleteVertexArrays(1, &this->fillVAO);
	glDeleteBuffers(1, &this->fillVBO);
	glDeleteVertexArrays(1, &this->dynamicVAO);
	glDeleteBuffers(1, &this->dynamicVBO);
}

/*
The collision lines view's panel (drawn in the Debug window's and the Level panel's Collision tab): its switches, then a sub tab for each kind of collision with the legend of its raw attribute values, in the color their lines have. Level: the shown level collisions
(Shadow attributes / SADX piece flags), most triangles first; clicking a value highlights it (the left and right arrow keys do the same), clicking it again shows all. Entities: the object shapes' attributes, highlighted the same way (the arrow keys move the selection of the sub tab that is shown).
*/
void CollisionView::drawPanel()
{
	ImGui::Checkbox("Show collision lines (C)", &this->enabled);
	ImGui::Checkbox("Color by attribute, otherwise by slope (V)", &this->colorByAttribute);
	ImGui::SliderFloat("Draw distance", &this->drawDistance, 100.0f, 20000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
	if(ImGui::BeginTabBar("CollisionTabs"))
	{
		if(ImGui::BeginTabItem("Level"))
		{
			if(this->enabled && this->colorByAttribute && !this->attributes.empty())
			{
				ImGui::Text("%u values", static_cast<uint32_t>(this->attributes.size()));
				ImGui::BeginChild("CollisionValues", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
				for(int i = 0; i < static_cast<int>(this->attributes.size()); i++)
				{
					const CollisionView::AttributeCount& rEntry = this->attributes[i];
					char line[96];
					std::snprintf(line, sizeof(line), "0x%08X  %s  %u triangles", rEntry.attribute, rEntry.behavior.c_str(), rEntry.count);
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(rEntry.color.r, rEntry.color.g, rEntry.color.b, 1.0f));
					if(ImGui::Selectable(line, i == this->selectedAttribute)) this->selectedAttribute = i == this->selectedAttribute ? -1 : i;
					ImGui::PopStyleColor();
				}
				ImGui::EndChild();
			}
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Entities"))
		{
			this->entityTabActive = true;
			if(this->enabled && this->colorByAttribute && !this->entityAttributes.empty())
			{
				ImGui::Text("%u values", static_cast<uint32_t>(this->entityAttributes.size()));
				ImGui::BeginChild("EntityCollisionValues", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
				for(const CollisionView::AttributeCount& rEntry : this->entityAttributes)
				{
					char line[96];
					std::snprintf(line, sizeof(line), "0x%08X  %s  %u shapes", rEntry.attribute, rEntry.behavior.c_str(), rEntry.count);
					bool selected = this->hasSelectedEntityValue && this->selectedEntityValue == rEntry.attribute;
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(rEntry.color.r, rEntry.color.g, rEntry.color.b, 1.0f));
					if(ImGui::Selectable(line, selected))
					{
						this->hasSelectedEntityValue = !selected;
						this->selectedEntityValue = rEntry.attribute;
					}
					ImGui::PopStyleColor();
				}
				ImGui::EndChild();
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}
}
