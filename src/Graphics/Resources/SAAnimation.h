#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Graphics
{
/* One key of a channel: the frame it is on and its value (rotation keys hold BAMS in a vec3 of whole numbers). */
struct SAKeyframe
{
	uint32_t frame;
	glm::vec3 value;
};

/*
The keys of one node of an SA motion, per channel; an empty channel keeps the node's rest value.
*/
struct SAMotionNode
{
	std::vector<SAKeyframe> positions;
	std::vector<SAKeyframe> rotations;
	std::vector<SAKeyframe> scales;
};

/*
A Ninja motion (NJS_MOTION) of an SA model: frameCount frames, one SAMotionNode per node of the model it was made for,
in the model's node order. Pooled in Graphics::saMotions, named "<rel>@0x<offset>"; users counts the instances playing
it (at 0 it is removed) and rel is the .rel module it came from (one user of its RelModule).
*/
struct SAMotion
{
	uint32_t frameCount;
	std::vector<SAMotionNode> nodes;
	uint32_t users = 0;
	std::string rel;
};
}
