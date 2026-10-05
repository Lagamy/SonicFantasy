#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace STH2005
{
/*
One key of a motion track: its frame, the slope of the curve arriving at it and leaving it (value per frame; equal on
keys that store one tangent) and its value.
*/
struct Keyframe
{
	uint32_t frame;
	float inTangent;
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
	/*
	The three channels that come before the nine in every bone but the first (empty for it): observed on SHADOW.MTP, in sh_jump_a they hold the keys of the parent bone's scale per axis (sh_j_head's Y is sh_j_mune's Y scale
	at frames 0, 10 and 13). Hypothesis: a segment scale compensation, the child cancels the parent's scale in its orientation and shape (see ONEInstance::buildPose).
	*/
	MotionTrack compensation[3];
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
	/*
	The frame range the game plays the motion in (observed on SHADOW.MTP, and in the running game: its animation library reads them as the motion's start and end, FUN_804125f4): two u16 at +0x10 and +0x12 of the
	motion's data, e.g. sh_jump_b 21 to 28, sh_jump_c 20 to 30 (it loops there), sh_jump_e 68 to 102. Most motions are 0 to their last frame. end is 0 if the data has none (a .MTN: then lastFrame stands for it).
	*/
	uint32_t start;
	uint32_t end;
	std::vector<MotionBone> bones;
};
}
