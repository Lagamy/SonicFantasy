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
Motion layout, decoded from SHADOW.MTP (observed, no outside source; all 158 of its motions fit), big-endian. A motion
starts with a 0x14 byte header (see readMtp, the u32 at 0x10 is not a reliable frame count: it reads 0x320041 on
lte_CC_b) and a 32 byte skeleton name ("sh", "lte", "rte").
From 0x37 the tracks follow back to back, each one channel of one bone:
  u8 size, u8 0, u8 key count, u8 (0 or 2: padding flag), u8 1 (0 on the last track of a motion), then the keys, then
  padding up to size (a multiple of 4).
The size has 9 bits: the 9th is the byte just before the track (the last padding byte of the previous track, 0 unless
the track is 256 bytes or longer; set on 41 tracks of sh_CB). Reading the size as 8 bits broke sh_CB, sh_CC_a, sh_atc,
sh_atc_eda and sh_idle_f.
A key is a marker byte and a u8 frame, then halves. Bit 0 of the marker is bit 8 of the frame (observed on DOOMSEYE.MTP's doomseye_fly: a track whose keys read C0 00, C0 C7, C1 8E, C1 E0, i.e. frames 0, 199, 398, 480; no
motion of SHADOW.MTP has a frame past 255). Markers seen: 0xC0 and 0x80 (6 bytes: half tangent, half value),
0x40 and 0x00 (8 bytes: two half tangents, half value). Only sh_slow_skate has the last three, on rotation tracks only.
Hypothesis, unverified: bit 7 of the marker means a single tangent. Without it the first half is the tangent arriving at
the key and the second the one leaving it (on sh_slow_skate the slope to the next key is closest to a key's second half
on 320 segments and to the next key's first half on 267, any other candidate on fewer than 60). Bit 6 is unknown: no
difference seen in the values.
The last track ends 3 bytes past the motion's data size, which is consistent on all 158.
Channels per bone: the first node of the skeleton (the locator, 0x103BF in the .BON) has 9: translation x y z,
rotation x y z, scale x y z. Every other node has 12: three channels (1.0 in sh_idle, other values in some motions) that hold the parent's scale in sh_jump_a (MotionBone::compensation), then the same 9. Observed: with 30 nodes (SH.BON) a motion has 9 + 29 x 12 = 357
tracks, with 7 (LTE.BON, RTE.BON) 9 + 6 x 12 = 81.
Values are the .BON's: on sh_j_l1asi1 the translation tracks hold the .BON translation and the rotation tracks its
rotation, Euler angles in radians.
Hypothesis, unverified: tangents are the slope per frame (on sh_root's Y track key 0 has -0.0094 and the average slope
to the next key is -0.0090), so the curve between keys is cubic Hermite.
*/
static Motion decodeMotion(const std::vector<uint8_t>& rBytes_, const char* pName_, const MtpMotion& found, const Bon& rBon_)
{
	uint32_t nodeCount = static_cast<uint32_t>(rBon_.nodes.size());
	uint32_t expectedTracks = nodeCount == 0 ? 0 : 9 + (nodeCount - 1) * 12;
	std::vector<std::pair<uint32_t, uint32_t>> tracks;
	uint32_t cursor = found.offset + 0x37;
	uint32_t end = found.offset + found.size + 3;
	while(cursor < found.offset + found.size)
	{
		uint32_t size = (readBE<uint8_t>(rBytes_, cursor - 1) << 8) | readBE<uint8_t>(rBytes_, cursor);
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
	motion.start = readBE<uint16_t>(rBytes_, found.offset + 0x10);
	motion.end = readBE<uint16_t>(rBytes_, found.offset + 0x12);
	uint32_t trackIndex = 0;
	for(uint32_t b = 0; b < nodeCount; b++)
	{
		MotionBone bone;
		bone.name = rBon_.nodes[b].name;
		std::vector<MotionTrack*> channels;
		if(b != 0) channels = {&bone.compensation[0], &bone.compensation[1], &bone.compensation[2]};
		for(MotionTrack* pChannel : {&bone.translation[0], &bone.translation[1], &bone.translation[2], &bone.rotation[0], &bone.rotation[1], &bone.rotation[2], &bone.scale[0], &bone.scale[1], &bone.scale[2]})
		{
			channels.push_back(pChannel);
		}
		for(MotionTrack* pTrack : channels)
		{
			uint32_t track = tracks[trackIndex].first;
			uint32_t keyCount = readBE<uint8_t>(rBytes_, track + 2);
			if(keyCount == 0 || 5 + keyCount * 6 > tracks[trackIndex].second)
			{
				throw std::runtime_error(std::format("STH2005::Mtp: {} track at 0x{:X} has {} keys in {} bytes.", pName_, track, keyCount, tracks[trackIndex].second));
			}
			uint32_t trackEnd = track + tracks[trackIndex].second;
			uint32_t key = track + 5;
			for(uint32_t k = 0; k < keyCount; k++)
			{
				uint8_t marker = readBE<uint8_t>(rBytes_, key);
				uint32_t keySize = (marker & 0x80) ? 6 : 8;
				if((marker & 0x3E) != 0 || key + keySize > trackEnd)
				{
					throw std::runtime_error(std::format("STH2005::Mtp: {} key at 0x{:X} starts with 0x{:X}, which is not a known key.", pName_, key, marker));
				}
				float inTangent = halfToFloat(readBE<uint16_t>(rBytes_, key + 2));
				float tangent = (marker & 0x80) ? inTangent : halfToFloat(readBE<uint16_t>(rBytes_, key + 4));
				Keyframe motionKey{static_cast<uint32_t>(((marker & 1) << 8) | readBE<uint8_t>(rBytes_, key + 1)), inTangent, tangent, halfToFloat(readBE<uint16_t>(rBytes_, key + keySize - 2))};
				if(!pTrack->empty() && motionKey.frame <= pTrack->back().frame)
				{
					throw std::runtime_error(std::format("STH2005::Mtp: {} key at 0x{:X} is not after the previous one.", pName_, key));
				}
				pTrack->push_back(motionKey);
				motion.lastFrame = std::max(motion.lastFrame, motionKey.frame);
				key += keySize;
			}
			trackIndex++;
		}
		motion.bones.push_back(std::move(bone));
	}
	return motion;
}

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
	return decodeMotion(rBytes_, pName_, found, rBon_);
}

/*
A .MTN is one motion with no table: the file is the motion's data as it is inside a .MTP (observed on SPRING00_ANM.MTN: it starts with 04 01 00 00, a zero word and the size,
which is the file's size, then the skeleton name "spring00" at 0x14 and a track of 0x14 bytes at 0x37, as in a .MTP motion). The size word has to equal the file size.
*/
Motion readMtnMotion(const std::vector<uint8_t>& rBytes_, const char* pName_, const Bon& rBon_)
{
	uint32_t size = readBE<uint32_t>(rBytes_, 8);
	if(size != rBytes_.size())
	{
		throw std::runtime_error(std::format("STH2005::Mtn: {} says it is {} bytes, the file has {}.", pName_, size, rBytes_.size()));
	}
	return decodeMotion(rBytes_, pName_, MtpMotion{pName_, 0, size, 0}, rBon_);
}
}
