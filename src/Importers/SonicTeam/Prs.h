#pragma once
#include <cstdint>
#include <vector>

namespace Importers
{
/* Decompresses a PRS stream (Sega's LZ77 variant, used by .ONE archive entries). */
std::vector<uint8_t> decompressPrs(const std::vector<uint8_t>& rSrc_);
}
