#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SclEntry {
	char name[8];
	uint8_t ext;
	uint8_t startSec;
	uint8_t startTrk;
	uint16_t length; // in bytes
};

class SclImage {
public:
	bool load(const std::string &path);
	bool toTrd(std::vector<uint8_t> &outTrd, int &tracks, int &heads) const;
private:
	std::vector<uint8_t> raw;
};