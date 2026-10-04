#include "RingLine.h"
#include "BinaryRead.h"
#include <cstring>
#include <glm/ext/matrix_transform.hpp>

namespace STH2005
{
/*
Layout of a RING record's parameters (little-endian, 20 bytes) and what the game does with them (external, the Ghidra decompile of main.dol: the RING factory
FUN_80112350 builds the object, FUN_8011357c makes `count` rings of 0x50 bytes each and FUN_80112cbc places them; decoded from the files: every RING record of
the stg0100 cmn and nrm files has these 20 bytes with shape 1):
u32 shape, u32 count, f32 a, f32 b, u32 (0 in every record seen).
Shape 0: every ring at the record's position. Shape 1 (the only one in stg0100): a line, ring i at (0, 0, -a / count * i) in the record's local frame, i = 0 .. count - 1.
Shapes 2 (a circle) and 3 (a wave along the line, amplitude b) are also in the code; they are not read here (their angle conventions are not checked).
The local frame is the record's position and rotation: the game builds the matrix from identity by translating to the position, then rotating about Y, X and Z by the
record's three angles in that order (FUN_8040b59c and FUN_8040b848 / b884 / b80c, which axis is which from the angle order of the record, hypothesis). The rotation about Z
does not move a point on the Z axis, so only the Y and X angles matter for a line. The file's angles are degrees by their values (hypothesis; the code works in radians).
*/
constexpr size_t ParamsSize = 20;
constexpr uint32_t ShapePoint = 0;
constexpr uint32_t ShapeLine = 1;

std::vector<glm::vec3> readRingPositions(const Placement& rPlacement_)
{
	if(rPlacement_.params.size() != ParamsSize)
	{
		throw std::runtime_error(std::format("STH2005::readRingPositions: a ring record has {} parameter bytes, not {}.", rPlacement_.params.size(), ParamsSize));
	}
	uint32_t shape = readLE<uint32_t>(rPlacement_.params, 0);
	uint32_t count = readLE<uint32_t>(rPlacement_.params, 4);
	float a = readLE<float>(rPlacement_.params, 8);
	if(shape != ShapePoint && shape != ShapeLine)
	{
		throw std::runtime_error(std::format("STH2005::readRingPositions: ring shape {} is not implemented.", shape));
	}

	glm::mat4 local = glm::rotate(glm::mat4(1.0f), glm::radians(rPlacement_.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	local = glm::rotate(local, glm::radians(rPlacement_.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	local = glm::rotate(local, glm::radians(rPlacement_.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

	std::vector<glm::vec3> positions;
	positions.reserve(count);
	for(uint32_t i = 0; i < count; i++)
	{
		glm::vec3 offset(0.0f);
		if(shape == ShapeLine)
		{
			offset.z = -(a / static_cast<float>(count)) * static_cast<float>(i);
		}
		positions.push_back(rPlacement_.position + glm::vec3(local * glm::vec4(offset, 0.0f)));
	}
	return positions;
}
}
