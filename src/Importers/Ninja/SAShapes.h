#pragma once
#include <cstdint>
#include <vector>
#include "CollisionShape.h"

namespace Ninja
{
/*
Reads count_ collision entries (Physics::CollisionShape, 0x30 bytes each, big-endian) starting at offset_ of a relocated GameCube
.rel module (see relocateRel). The entries are plain data in the module's data sections: the offset is a file offset into the
decompressed module (for an address of the Ghidra pseudocode in sadxDecomp: the section's file_offset + address - the section's address, from
section-layout.json). Throws if the range is outside the file.
*/
std::vector<Physics::CollisionShape> readSACollisionShapes(const std::vector<uint8_t>& rBytes_, uint32_t offset_, uint32_t count_);
}
