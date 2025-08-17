#include "dsk.h"
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

bool DskImage::load(const std::string &path) {
	std::vector<uint8_t> buf;
	if (!read_file(path, buf)) return false;
	if (buf.size() < 256) return false;
	if (std::memcmp(buf.data(), "EXTENDED CPC DSK File", 21) == 0) extended = true;
	else if (std::memcmp(buf.data(), "MV - CPCEMU Disk-File", 21) == 0) extended = false;
	else return false;
	
	// Common header
	numTracks = buf[0x30]; numHeads = buf[0x31];
	tracks.clear();
	tracks.resize(numTracks * numHeads);
	
	size_t offset = 0x100;
	for (uint8_t t = 0; t < numTracks; ++t) {
		for (uint8_t h = 0; h < numHeads; ++h) {
			if (offset + 0x100 > buf.size()) return false;
			if (std::memcmp(&buf[offset], "Track-Info", 10) != 0) return false;
			uint8_t track = buf[offset + 0x10];
			uint8_t head = buf[offset + 0x11];
			uint8_t sectorCount = buf[offset + 0x15];
			uint16_t dataSize = uint16_t(buf[offset + 0x30]) | (uint16_t(buf[offset + 0x31]) << 8);
			DskTrackInfo tinfo; tinfo.trackNumber = track; tinfo.headNumber = head;
			size_t psec = offset + 0x100;
			for (uint8_t s = 0; s < sectorCount; ++s) {
				if (psec + 8 > buf.size()) return false;
				DskSectorInfo sinfo;
				sinfo.C = buf[offset + 0x18 + s*8 + 0];
				sinfo.H = buf[offset + 0x18 + s*8 + 1];
				sinfo.R = buf[offset + 0x18 + s*8 + 2];
				sinfo.N = buf[offset + 0x18 + s*8 + 3];
				// Skip ST1/ST2, and record data offset sequentially
				// Each sector data block follows the 0x100 header, sequentially; in extended, sizes vary per sector
				// Compute sector length from N; if extended has per-sector sizes, dataSize holds total track size; we step by actual data length
				uint32_t slen = 128u << sinfo.N;
				if (psec + slen > buf.size()) return false;
				sinfo.data.assign(buf.begin() + psec, buf.begin() + psec + slen);
				psec += slen;
				tinfo.sectors.push_back(std::move(sinfo));
			}
			tracks[head * numTracks + track] = std::move(tinfo);
			offset = psec; // move to next track header
		}
	}
	return true;
}

const DskTrackInfo* DskImage::getTrack(uint8_t cyl, uint8_t head) const {
	if (head >= numHeads || cyl >= numTracks) return nullptr;
	return &tracks[head * numTracks + cyl];
}

const DskSectorInfo* DskImage::findSector(uint8_t cyl, uint8_t head, uint8_t R, uint8_t N) const {
	auto t = getTrack(cyl, head);
	if (!t) return nullptr;
	for (const auto &s : t->sectors) {
		if (s.R == R && s.N == N) return &s;
	}
	return nullptr;
}