#pragma once
#include <cstdint>
#include <vector>
#include "CollisionShape.h"

namespace STH2005
{
/*
Reads a Shadow the Hedgehog .CCL (object collision, an entry of a .ONE archive) into shapes, one per record. Throws if the file
does not follow the layout described in CCL.cpp.
Only the shapes are returned; the group name and the record ids are not used. The mapping of a record's floats to a shape is explained in CCL.cpp.
*/
std::vector<Physics::CollisionShape> readCCL(const std::vector<uint8_t>& rBytes_);
}
