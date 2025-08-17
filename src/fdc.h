#pragma once
#include <cstdint>
#include <vector>
#include "dsk.h"

class Plus3FDC {
public:
	void reset() { statusMain = 0x80; dataReg = 0; cmdPhase = Phase::Idle; resultIdx = 0; }
	void attachImage(const DskImage *img) { image = img; }

	// Ports (approximate +3 mapping; actual +3 gate-array differs; for simplicity use 0x3FFD: command/status, 0x2FFD: data)
	uint8_t readStatus() const { return statusMain; }
	uint8_t readData() {
		uint8_t v = 0xFF;
		if (cmdPhase == Phase::Result && resultIdx < result.size()) {
			v = result[resultIdx++];
			if (resultIdx >= result.size()) { cmdPhase = Phase::Idle; statusMain = 0x80; }
		}
		return v;
	}
	void writeCommand(uint8_t v) {
		cmd = v; param.clear(); result.clear(); resultIdx = 0; cmdPhase = Phase::Params;
		statusMain = 0x10; // CB (command busy)
	}
	void writeData(uint8_t v) {
		if (cmdPhase == Phase::Params) {
			param.push_back(v);
			if (isParamComplete()) executeCommand();
		}
	}

private:
	enum class Phase { Idle, Params, Exec, Result };
	Phase cmdPhase{Phase::Idle};
	const DskImage *image{nullptr};
	uint8_t statusMain{0x80};
	uint8_t dataReg{0};
	uint8_t cmd{0};
	std::vector<uint8_t> param;
	std::vector<uint8_t> result;
	size_t resultIdx{0};

	bool isParamComplete() const {
		// Only implement READ DATA (0x06) command: expects 8 params
		if ((cmd & 0x1F) == 0x06) return param.size() >= 8;
		// SENSE INTERRUPT STATUS (0x08): no params
		if ((cmd & 0x1F) == 0x08) return true;
		return false;
	}
	void executeCommand() {
		uint8_t c = cmd & 0x1F;
		if (c == 0x08) {
			// Sense interrupt status: return fake ST0=0x00, PCN=0
			result = { 0x00, 0x00 };
			cmdPhase = Phase::Result; statusMain = 0xD0; return;
		}
		if (c == 0x06 && image) {
			// READ DATA: params: C H R N EOT GPL DTL
			uint8_t C = param[0]; uint8_t H = param[1]; uint8_t R = param[2]; uint8_t N = param[3];
			const DskSectorInfo *sec = image->findSector(C, H, R, N);
			if (!sec) {
				// Not found: status with not found
				result = { 0x40, 0x04, 0x00, C, H, R, N }; // ST0/1/2 minimal
				cmdPhase = Phase::Result; statusMain = 0xD0; return;
			}
			// Prepare a READ result: +3DOS will then read from data port repeatedly; we shortcut by returning status only and the ROM will proceed to IN from data
			dataFifo = sec->data;
			dataPos = 0;
			// Return result after read completes (we'll serve data through readData override using a special phase)
			cmdPhase = Phase::Exec; statusMain = 0xB0; // RQM|DIO|CB
			return;
		}
		// Unsupported: return fake OK
		result = { 0x00 }; cmdPhase = Phase::Result; statusMain = 0xD0;
	}

public:
	// Hook to serve data on data port when in Exec phase
	bool hasDataByte() const { return cmdPhase == Phase::Exec && dataPos < dataFifo.size(); }
	uint8_t readDataByte() {
		if (!hasDataByte()) return 0xFF;
		uint8_t v = dataFifo[dataPos++];
		if (dataPos >= dataFifo.size()) {
			// End of transfer: present result
			result = { 0x00, 0x00, 0x00, /* ST0/1/2 */ param[0], param[1], param[2], param[3] };
			cmdPhase = Phase::Result; statusMain = 0xD0; // RQM|DIO
		}
		return v;
	}

private:
	std::vector<uint8_t> dataFifo;
	size_t dataPos{0};
};