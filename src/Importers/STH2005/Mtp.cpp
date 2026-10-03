#include "Mtp.h"
#include "BinaryRead.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <stdexcept>

namespace STH2005
{
/*
.MTP layout, decoded from SHADOW.MTP (observed, no outside source), big-endian: u16 1, u16 motion count, then at 0x10
u32 offset of the motion table (0x14). Table entry (12 bytes): u32 name offset (null-terminated string in the file),
u32 data offset, u32 unknown (0, or an offset near the end of the file). Each motion's data starts with
04 01 00 00, u32 0, u32 data size (equal to the distance to the next motion on SHADOW.MTP), then see readMtpMotion.
*/
std::vector<MtpMotion> readMtp(const std::vector<uint8_t>& rBytes_)
{
	uint16_t count = readBE<uint16_t>(rBytes_, 2);
	uint32_t table = readBE<uint32_t>(rBytes_, 0x10);

	std::vector<MtpMotion> motions;
	for(uint32_t i = 0; i < count; i++)
	{
		uint32_t entry = table + i * 12;
		uint32_t nameOffset = readBE<uint32_t>(rBytes_, entry);
		uint32_t dataOffset = readBE<uint32_t>(rBytes_, entry + 4);
		uint32_t unknown = readBE<uint32_t>(rBytes_, entry + 8);
		if(nameOffset >= rBytes_.size())
		{
			throw std::runtime_error(std::format("STH2005::Mtp: motion {} has its name at 0x{:X}, past the end of the file.", i, nameOffset));
		}
		const char* pName = reinterpret_cast<const char*>(rBytes_.data() + nameOffset);
		std::string name(pName, strnlen(pName, rBytes_.size() - nameOffset));
		uint32_t size = readBE<uint32_t>(rBytes_, dataOffset + 8);
		if(static_cast<uint64_t>(dataOffset) + size > rBytes_.size())
		{
			throw std::runtime_error(std::format("STH2005::Mtp: motion {} ({} bytes at 0x{:X}) is past the end of the file.", name, size, dataOffset));
		}
		motions.push_back(MtpMotion{name, dataOffset, size, unknown});
	}
	return motions;
}

/*
IEEE half float (1 sign, 5 exponent, 10 mantissa bits) to float.
*/
static float halfToFloat(uint16_t half_)
{
	int exponent = (half_ >> 10) & 0x1F;
	int mantissa = half_ & 0x3FF;
	float value;
	if(exponent == 0)
	{
		value = std::ldexp(static_cast<float>(mantissa), -24);
	}
	else if(exponent == 31)
	{
		value = mantissa == 0 ? INFINITY : NAN;
	}
	else
	{
		value = std::ldexp(static_cast<float>(1024 + mantissa), exponent - 25);
	}
	return (half_ & 0x8000) ? -value : value;
}

/*
Motion layout, decoded from SHADOW.MTP (observed, no outside source; 152 of its 158 motions fit, the 6 listed in
readMtpMotion's comment do not), big-endian. A motion starts with a 0x14 byte header (see readMtp, the u32 at 0x10 is
not a reliable frame count: it reads 0x320041 on lte_CC_b) and a 32 byte skeleton name ("sh", "lte", "rte").
From 0x37 the tracks follow back to back, each one channel of one bone:
  u8 size, u8 0, u8 key count, u8 (0 or 2: padding flag), u8 1, then key count x
  (u8 0xC0, u8 frame, half tangent, half value), then padding up to size (a multiple of 4).
The last track ends 3 bytes past the motion's data size, which is consistent on all 152.
Channels per bone: the first node of the skeleton (the locator, 0x103BF in the .BON) has 9: translation x y z,
rotation x y z, scale x y z. Every other node has 12: three channels whose meaning is unknown (1.0 in sh_idle, other
values in some motions; ignored here), then the same 9. Observed: with 30 nodes (SH.BON) a motion has 9 + 29 x 12 = 357
tracks, with 7 (LTE.BON, RTE.BON) 9 + 6 x 12 = 81.
Values are the .BON's: on sh_j_l1asi1 the translation tracks hold the .BON translation and the rotation tracks its
rotation, Euler angles in radians.
Hypothesis, unverified: tangents are the slope per frame (on sh_root's Y track key 0 has -0.0094 and the average slope
to the next key is -0.0090), so the curve between keys is cubic Hermite.
*/
Motion readMtpMotion(const std::vector<uint8_t>& rBytes_, const char* pName_, const Bon& rBon_)
{
	MtpMotion found;
	bool exists = false;
	for(const MtpMotion& rMotion : readMtp(rBytes_))
	{
		if(rMotion.name == pName_)
		{
			found = rMotion;
			exists = true;
		}
	}
	if(!exists)
	{
		throw std::runtime_error(std::format("STH2005::Mtp: no motion named {}.", pName_));
	}

	uint32_t nodeCount = static_cast<uint32_t>(rBon_.nodes.size());
	uint32_t expectedTracks = nodeCount == 0 ? 0 : 9 + (nodeCount - 1) * 12;
	std::vector<std::pair<uint32_t, uint32_t>> tracks;
	uint32_t cursor = found.offset + 0x37;
	uint32_t end = found.offset + found.size + 3;
	while(cursor < found.offset + found.size)
	{
		uint8_t size = readBE<uint8_t>(rBytes_, cursor);
		if(size < 5 || cursor + size > end)
		{
			throw std::runtime_error(std::format("STH2005::Mtp: {} has a track of {} bytes at 0x{:X}, which doesn't fit.", pName_, size, cursor));
		}
		tracks.emplace_back(cursor, size);
		cursor += size;
	}
	if(cursor != end || tracks.size() != expectedTracks)
	{
		throw std::runtime_error(std::format("STH2005::Mtp: {} has {} tracks ending at 0x{:X}, but {} nodes need {} ending at 0x{:X}: unsupported layout.", pName_, tracks.size(), cursor, nodeCount, expectedTracks, end));
	}

	Motion motion;
	motion.name = pName_;
	motion.lastFrame = 0;
	uint32_t trackIndex = 0;
	for(uint32_t b = 0; b < nodeCount; b++)
	{
		MotionBone bone;
		bone.name = rBon_.nodes[b].name;
		trackIndex += b == 0 ? 0 : 3;
		for(MotionTrack* pTrack : {&bone.translation[0], &bone.translation[1], &bone.translation[2], &bone.rotation[0], &bone.rotation[1], &bone.rotation[2], &bone.scale[0], &bone.scale[1], &bone.scale[2]})
		{
			uint32_t track = tracks[trackIndex].first;
			uint32_t keyCount = readBE<uint8_t>(rBytes_, track + 2);
			if(keyCount == 0 || 5 + keyCount * 6 > tracks[trackIndex].second)
			{
				throw std::runtime_error(std::format("STH2005::Mtp: {} track at 0x{:X} has {} keys in {} bytes.", pName_, track, keyCount, tracks[trackIndex].second));
			}
			for(uint32_t k = 0; k < keyCount; k++)
			{
				uint32_t key = track + 5 + k * 6;
				if(readBE<uint8_t>(rBytes_, key) != 0xC0)
				{
					throw std::runtime_error(std::format("STH2005::Mtp: {} key at 0x{:X} starts with 0x{:X}, not 0xC0.", pName_, key, readBE<uint8_t>(rBytes_, key)));
				}
				Keyframe motionKey{readBE<uint8_t>(rBytes_, key + 1), halfToFloat(readBE<uint16_t>(rBytes_, key + 2)), halfToFloat(readBE<uint16_t>(rBytes_, key + 4))};
				if(!pTrack->empty() && motionKey.frame <= pTrack->back().frame)
				{
					throw std::runtime_error(std::format("STH2005::Mtp: {} key at 0x{:X} is not after the previous one.", pName_, key));
				}
				pTrack->push_back(motionKey);
				motion.lastFrame = std::max(motion.lastFrame, motionKey.frame);
			}
			trackIndex++;
		}
		motion.bones.push_back(std::move(bone));
	}
	return motion;
}
}
