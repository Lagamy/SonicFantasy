#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Bon.h"
#include "STH2005Motion.h"

namespace STH2005
{
/* One motion of a .MTP: its name and where its data is inside the file. unknown is the entry's third field. */
struct MtpMotion
{
	std::string name;
	uint32_t offset;
	uint32_t size;
	uint32_t unknown;
};

/*
Reads the motion table of a Shadow the Hedgehog .MTP (motion package, e.g. character/shadow.one > SHADOW.MTP).
Only the table; readMtpMotion decodes one motion.
*/
std::vector<MtpMotion> readMtp(const std::vector<uint8_t>& rBytes_);

/*
Decodes the motion called rName_ of a .MTP for the skeleton rBon_ (the .BON of the motion's skeleton: its node names
become the bones' names). Only the layout described in Mtp.cpp is supported; anything else throws, e.g.
sh_CB, sh_CC_a, sh_atc, sh_atc_eda, sh_idle_f and sh_slow_skate of SHADOW.MTP.
*/
Motion readMtpMotion(const std::vector<uint8_t>& rBytes_, const char* pName_, const Bon& rBon_);
}
