#include "microdrive.h"
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

bool MicrodriveCart::load(const std::string &path) {
	std::vector<uint8_t> buf;
	if (!read_file(path, buf)) return false;
	// Accept raw MDR contents; no header parsing for simplicity
	if (buf.empty()) return false;
	data.swap(buf);
	return true;
}