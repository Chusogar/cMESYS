#include "trd.h"
#include <cstdio>
#include <sys/stat.h>

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

bool TrdImage::load(const std::string &path) {
	std::vector<uint8_t> buf;
	if (!read_file(path, buf)) return false;
	// Expect size = tracks * heads * sectorsPerTrack * sectorSize
	if (buf.size() % (geom.sectorSize * geom.sectorsPerTrack) != 0) {
		// try single-side 80 tracks
		geom.heads = 1;
	}
	// Compute tracks from size
	size_t perTrack = geom.sectorSize * geom.sectorsPerTrack * geom.heads;
	if (perTrack == 0) return false;
	geom.tracks = static_cast<uint16_t>(buf.size() / perTrack);
	if (geom.tracks == 0) return false;
	data.swap(buf);
	return true;
}

bool TrdImage::loadRaw(const std::vector<uint8_t> &raw) {
	if (raw.empty()) return false;
	size_t perGeom = geom.sectorSize * geom.sectorsPerTrack * geom.heads;
	// Try default 80x2x16x256
	geom.heads = 2; geom.tracks = 80;
	size_t expect = size_t(geom.tracks) * geom.heads * geom.sectorsPerTrack * geom.sectorSize;
	if (raw.size() != expect) {
		// try single head
		geom.heads = 1; expect = size_t(geom.tracks) * geom.heads * geom.sectorsPerTrack * geom.sectorSize;
		if (raw.size() != expect) return false;
	}
	data = raw; return true;
}

size_t TrdImage::indexOf(uint8_t cyl, uint8_t head, uint8_t sec) const {
	// TR-DOS sectors numbered 1..16 typically
	if (sec == 0 || sec > geom.sectorsPerTrack) return (size_t)-1;
	if (cyl >= geom.tracks) return (size_t)-1;
	if (head >= geom.heads) return (size_t)-1;
	size_t trackIndex = (size_t(cyl) * geom.heads + head);
	return trackIndex * (geom.sectorSize * geom.sectorsPerTrack) + (size_t(sec-1) * geom.sectorSize);
}

bool TrdImage::readSector(uint8_t cyl, uint8_t head, uint8_t R, uint8_t N, std::vector<uint8_t> &out) const {
	if (!isLoaded()) return false;
	if (N != 1) return false; // expect 256-byte sectors only
	size_t idx = indexOf(cyl, head, R);
	if (idx == (size_t)-1) return false;
	out.resize(geom.sectorSize);
	std::copy(data.begin()+idx, data.begin()+idx+geom.sectorSize, out.begin());
	return true;
}