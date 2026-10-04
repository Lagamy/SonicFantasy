#pragma once
#include <cstdint>
#include <vector>

namespace GameCube
{
/*
Applies a GameCube .rel module's relocations against itself and returns the patched bytes (a SaCompGC
compressed module is decompressed first).
Patched pointers hold file offsets (target section's file offset + addend), so data inside the module
can then be walked with pointer = file offset, like a .sa1mdl.
*/
std::vector<uint8_t> relocateRel(std::vector<uint8_t> bytes_);
}
