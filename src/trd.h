#pragma once
#include <cstdint>
#include <vector>
#include <string>

struct TrdGeometry {
	uint8_t sectorsPerTrack{16};
	uint8_t heads{2};
	uint16_t tracks{80};
	uint16_t sectorSize{256};
};

class TrdImage {
public:
	bool load(const std::string &path);
	bool loadRaw(const std::vector<uint8_t> &raw);
	bool isLoaded() const { return !data.empty(); }
	const TrdGeometry& geometry() const { return geom; }
	bool readSector(uint8_t cyl, uint8_t head, uint8_t R, uint8_t N, std::vector<uint8_t> &out) const;
private:
	TrdGeometry geom{};
	std::vector<uint8_t> data; // flat image
	inline size_t indexOf(uint8_t cyl, uint8_t head, uint8_t sec) const;
};