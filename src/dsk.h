#pragma once
#include <cstdint>
#include <vector>
#include <string>

struct DskSectorInfo {
	uint8_t C{0}; // cylinder
	uint8_t H{0}; // head
	uint8_t R{0}; // sector id
	uint8_t N{0}; // size code
	std::vector<uint8_t> data; // sector data
};

struct DskTrackInfo {
	uint8_t trackNumber{0};
	uint8_t headNumber{0};
	std::vector<DskSectorInfo> sectors;
};

struct DskImage {
	bool extended{false};
	uint8_t numTracks{0};
	uint8_t numHeads{2};
	std::vector<DskTrackInfo> tracks; // indexed by head*numTracks + track

	bool load(const std::string &path);
	const DskTrackInfo* getTrack(uint8_t cyl, uint8_t head) const;
	const DskSectorInfo* findSector(uint8_t cyl, uint8_t head, uint8_t R, uint8_t N) const;
};