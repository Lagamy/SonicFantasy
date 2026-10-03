#include "GCDisc.h"
#include "BinaryRead.h"
#include <format>
#include <stdexcept>

namespace GameCube
{
/*
GameCube disc layout (external, YAGCD; confirmed on the Shadow the Hedgehog disc), all big-endian:
0x424 u32 offset of the file table (FST), 0x428 u32 its size.
FST: 12-byte entries, then the name string table. Entry: u8 type (0 file, 1 dir) + u24 name offset,
u32 file offset (dir: parent index), u32 file size (dir: index of the first entry after its contents).
Entry 0 is the root directory; its third field is the total entry count.
*/
static void readDirectory(GCDisc& rDisc_, const std::vector<uint8_t>& rFst_, uint32_t stringTable_, uint32_t first_, uint32_t end_, const std::string& prefix_)
{
	uint32_t i = first_;
	while(i < end_)
	{
		uint32_t typeAndName = readBE<uint32_t>(rFst_, i * 12);
		uint32_t second = readBE<uint32_t>(rFst_, i * 12 + 4);
		uint32_t third = readBE<uint32_t>(rFst_, i * 12 + 8);

		uint32_t nameOffset = stringTable_ + (typeAndName & 0xFFFFFF);
		if(nameOffset >= rFst_.size())
		{
			throw std::runtime_error(std::format("GCDisc: file table entry {} has a name outside the table.", i));
		}
		const char* pName = reinterpret_cast<const char*>(rFst_.data() + nameOffset);
		std::string path = prefix_ + pName;

		if((typeAndName >> 24) == 1)
		{
			readDirectory(rDisc_, rFst_, stringTable_, i + 1, third, path + "/");
			i = third;
		}
		else
		{
			rDisc_.files[path] = GCDiscFile{second, third};
			i++;
		}
	}
}

GCDisc openGCDisc(const char* path_)
{
	GCDisc disc;
	disc.file.open(path_, std::ios::binary);
	if(!disc.file.is_open())
	{
		throw std::runtime_error(std::format("GCDisc: failed to open {}", path_));
	}

	std::vector<uint8_t> header(0x430);
	disc.file.read(reinterpret_cast<char*>(header.data()), header.size());
	uint32_t fstOffset = readBE<uint32_t>(header, 0x424);
	uint32_t fstSize = readBE<uint32_t>(header, 0x428);

	std::vector<uint8_t> fst(fstSize);
	disc.file.seekg(fstOffset);
	disc.file.read(reinterpret_cast<char*>(fst.data()), fst.size());
	if(!disc.file)
	{
		throw std::runtime_error(std::format("GCDisc: {} is too short for its file table (0x{:X} + 0x{:X}).", path_, fstOffset, fstSize));
	}

	uint32_t entryCount = readBE<uint32_t>(fst, 8);
	if(static_cast<uint64_t>(entryCount) * 12 > fst.size())
	{
		throw std::runtime_error(std::format("GCDisc: {} has a file table claiming {} entries in 0x{:X} bytes.", path_, entryCount, fstSize));
	}
	fst.push_back(0);
	readDirectory(disc, fst, entryCount * 12, 1, entryCount, "");

	return disc;
}

std::vector<uint8_t> GCDisc::readFile(const std::string& path_)
{
	auto it = this->files.find(path_);
	if(it == this->files.end())
	{
		throw std::runtime_error(std::format("GCDisc: no file {} on the disc.", path_));
	}

	std::vector<uint8_t> bytes(it->second.size);
	this->file.clear();
	this->file.seekg(it->second.offset);
	this->file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
	if(!this->file)
	{
		throw std::runtime_error(std::format("GCDisc: failed to read {} (0x{:X}, {} bytes).", path_, it->second.offset, it->second.size));
	}
	return bytes;
}
}
