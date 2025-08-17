#include "scl.h"
#include <cstdio>
#include <cstring>

static bool read_file(const std::string &path, std::vector<uint8_t> &out) {
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) return false;
	std::fseek(f, 0, SEEK_END);
	long size = std::ftell(f);
	if (size < 0) { std::fclose(f); return false; }
	std::rewind(f);
	out.resize(static_cast<size_t>(size));
	size_t rd = std::fread(out.data(), 1, out.size(), f);
	std::fclose(f);
	return rd == out.size();
}

bool SclImage::load(const std::string &path) { return read_file(path, raw); }

bool SclImage::toTrd(std::vector<uint8_t> &outTrd, int &tracks, int &heads) const {
	// This is a placeholder: convert SCL by placing file data sequentially starting at TR-DOS dir area
	tracks = 80; heads = 2;
	const size_t trdSize = size_t(tracks) * heads * 16 * 256;
	outTrd.assign(trdSize, 0x00);
	// Leave directory/metadata empty; copy raw blob into data area starting sector 1, track 0, head 0
	size_t copyBytes = raw.size(); if (copyBytes > trdSize) copyBytes = trdSize;
	std::copy(raw.begin(), raw.begin() + copyBytes, outTrd.begin());
	return true;
}