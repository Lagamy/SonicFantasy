#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace STH2005
{
/* One key of a motion track: its frame, the slope of the curve there (value per frame) and its value. */
struct Keyframe
{
	uint32_t frame;
	float tangent;
	float value;
};

/* The keys of one channel, in frame order (always at least one). */
using MotionTrack = std::vector<Keyframe>;

/*
The tracks of one bone of a Shadow the Hedgehog motion: its name (from the .BON the motion was read with) and, per axis
x, y, z, its translation, rotation (Euler angles in radians) and scale.
*/
struct MotionBone
{
	std::string name;
	MotionTrack translation[3];
	MotionTrack rotation[3];
	MotionTrack scale[3];
};

/*
A Shadow the Hedgehog motion (one entry of a .MTP, see readMtpMotion): one MotionBone per node of its skeleton's .BON,
in the .BON's order. lastFrame is the last key frame of all tracks, where it loops.
It lives in its ONE (ONE::motions).
*/
struct Motion
{
	std::string name;
	uint32_t lastFrame;
	std::vector<MotionBone> bones;
};
}
