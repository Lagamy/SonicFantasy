#pragma once
#include <cstdint>
#include <vector>

namespace GameCube
{
/* True if the bytes hold a "SaCompGC" compressed .rel (the tag sits after a small preamble, 4-byte aligned). */
bool isSaComp(const std::vector<uint8_t>& rBytes_);

/* Returns the decompressed .rel of a SaCompGC file. Throws if the stream is malformed. */
std::vector<uint8_t> decompressSaComp(const std::vector<uint8_t>& rBytes_);
}
