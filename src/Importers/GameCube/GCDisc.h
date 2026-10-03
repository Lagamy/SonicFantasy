#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace GameCube
{
struct GCDiscFile
{
	uint32_t offset;
	uint32_t size;
};

/*
An opened GameCube disc image (.iso/.gcm). Only the file table is read up front;
file contents are read from disk on request, so the image is never loaded whole.
Paths are relative to the disc root with '/' separators, e.g. "character/shadow.one".
*/
struct GCDisc
{
	std::ifstream file;
	std::unordered_map<std::string, GCDiscFile> files;

	std::vector<uint8_t> readFile(const std::string& path_);
};

GCDisc openGCDisc(const char* path_);
}
