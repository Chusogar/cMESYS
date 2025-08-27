#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>

#ifndef ZX_HEADLESS
#include <SDL2/SDL.h>
#endif

#include "z80.h"
#include "memory.h"
#include "ula.h"
#include "keyboard.h"
#include "beeper.h"
#include "snapshot_sna.h"
#include "tape.h"
#include "ay.h"
#include "dsk.h"
#include "fdc.h"
#include "betadisk.h"
#include "trd.h"
#include "microdrive.h"
#include "scl.h"

static bool load_file_to_buffer(const std::string &path, std::vector<uint8_t> &out) {
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

static bool parse_model_and_roms(int argc, char **argv, std::string &modelOut, std::string &if1Rom, std::string &trdosRom, int &firstNonFlag) {
	modelOut = ""; if1Rom=""; trdosRom=""; firstNonFlag = 1;
	for (int i=1; i<argc; ++i) {
		if (std::strncmp(argv[i], "--model=", 8) == 0) { modelOut = std::string(argv[i]+8); continue; }
		if (std::strncmp(argv[i], "--if1-rom=", 10) == 0) { if1Rom = std::string(argv[i]+10); continue; }
		if (std::strncmp(argv[i], "--trdos-rom=", 12) == 0) { trdosRom = std::string(argv[i]+12); continue; }
		firstNonFlag = i; break;
	}
	return true;
}

struct ZXMachine {
	Z80Cpu cpu{};
	Memory memory{};
	ULA ula{};
	Keyboard keyboard{};
	Beeper beeper{};
	Tape tape{};
	AY38912 ay{};
	DskImage dsk{};
	Plus3FDC fdc{};
	bool fdcEnabled{false};
	TrdImage trd{};
	BetaDisk beta{};
	bool betaEnabled{false};
	Interface1 if1{};
	MicrodriveCart mdr{};
	bool if1Enabled{false};
	uint64_t tstate_counter{0};

	bool init_with_model(const std::string &romPath, const std::string &modelName, const std::string &if1RomPath, const std::string &trdosRomPath) {
		if (modelName == "48" || modelName == "48k") memory.model = Memory::Model::ZX48;
		else if (modelName == "128" || modelName == "128k") memory.model = Memory::Model::ZX128;
		else if (modelName == "+3" || modelName == "plus3") memory.model = Memory::Model::ZXPlus3;
		else if (modelName == "pentagon") memory.model = Memory::Model::Pentagon;
		else if (modelName == "scorpion") memory.model = Memory::Model::Scorpion;
		bool ok = init(romPath);
		if (!ok) return false;
		// Load optional overlay ROMs
		if (!if1RomPath.empty()) { std::vector<uint8_t> buf; if (load_file_to_buffer(if1RomPath, buf)) memory.load_if1_rom(buf.data(), buf.size()); memory.setIf1(true); }
		if (!trdosRomPath.empty()) { std::vector<uint8_t> buf; if (load_file_to_buffer(trdosRomPath, buf)) memory.load_trdos_rom(buf.data(), buf.size()); memory.setRomcs(true); }
		return true;
	}
	bool init(const std::string &romPath) {
		std::vector<uint8_t> rom;
		if (!load_file_to_buffer(romPath, rom)) {
			std::fprintf(stderr, "Failed to load ROM from %s\n", romPath.c_str());
			return false;
		}
		if (!(rom.size() == 16384 || rom.size() == 32768 || rom.size() == 65536)) {
			std::fprintf(stderr, "ROM must be 16KB (48K), 32KB (128K), or 64KB (+3). Got %zu bytes.\n", rom.size());
			return false;
		}
		memory.reset();
		memory.load_rom(rom.data(), rom.size());

		cpu.reset();
		cpu.connectMemory(
			[this](uint16_t addr) { return memory.read(addr); },
			[this](uint16_t addr, uint8_t value) { memory.write(addr, value); }
		);
		cpu.connectIO(
			[this](uint16_t port) { return this->io_read(port); },
			[this](uint16_t port, uint8_t value) { this->io_write(port, value); }
		);

		ula.reset();
		ula.connectMemory(&memory);
		keyboard.reset();
		beeper.reset(44100);
		tape.reset(3500000);
		ay.reset(1750000, 44100);
		fdc.reset(); fdcEnabled = (memory.model == Memory::Model::ZXPlus3);
		beta.reset(); betaEnabled = (memory.model == Memory::Model::Pentagon || memory.model == Memory::Model::Scorpion || memory.model == Memory::Model::ZX128);
		if1.reset(); if1Enabled = true;
		return true;
	}
	uint8_t io_read(uint16_t port) {
		if ((port & 0x0001) == 0) {
			uint8_t k = keyboard.readRow(static_cast<uint8_t>((port >> 8) & 0xFF));
			uint8_t earMic = ((tape.earBit() || (if1Enabled && if1.earActive())) ? 0x40 : 0x00);
			return (k & 0x1F) | earMic | (ula.borderColour() & 0x07) << 0;
		}
		if (fdcEnabled) {
			if ((port & 0xFFFF) == 0x3FFD) return fdc.readStatus();
			if ((port & 0xFFFF) == 0x2FFD) { return fdc.hasDataByte() ? fdc.readDataByte() : fdc.readData(); }
		}
		if (betaEnabled) {
			uint16_t low = port & 0xFF;
			if (low == 0x1F || low == 0x3F || low == 0x5F || low == 0x7F || low == 0x3D) return beta.in(port);
		}
		if (if1Enabled) {
			uint8_t low = uint8_t(port & 0xFF);
			if (low == 0xE7 || low == 0xEF) return if1.in(port);
		}
		if ((port & 0xFFFF) == 0xFFFD) { return ay.readData(); }
		return 0xFF;
	}
	void io_write(uint16_t port, uint8_t value) {
		if ((port & 0x0001) == 0) { ula.setBorderColour(value & 0x07); beeper.setLevel((value & 0x10) ? 1.0f : 0.0f); return; }
		if ((port & 0xFFFF) == 0x7FFD) { memory.out7FFD(value); return; }
		if ((port & 0xFFFF) == 0x1FFD) { memory.out1FFD(value); return; }
		if (fdcEnabled) {
			if ((port & 0xFFFF) == 0x3FFD) { fdc.writeCommand(value); return; }
			if ((port & 0xFFFF) == 0x2FFD) { fdc.writeData(value); return; }
		}
		if (betaEnabled) {
			uint16_t low = port & 0xFF;
			if (low == 0x1F || low == 0x3F || low == 0x5F || low == 0x7F || low == 0x3D) { beta.out(port, value); memory.setRomcs(beta.trdosRomActive()); return; }
		}
		if (if1Enabled) {
			uint8_t low = uint8_t(port & 0xFF);
			if (low == 0xEB) { if1.out(port, value); return; }
		}
		if ((port & 0xFFFF) == 0xFFFD) { ay.setIndex(value); return; }
		if ((port & 0xFFFF) == 0xBFFD) { ay.writeData(value); return; }
	}
	int stepInstruction() { int t = cpu.step(); tstate_counter += (uint64_t)t; ula.tick((uint32_t)t); tape.tick((uint32_t)t); if1.tick((uint32_t)t); ay.tickTstates((uint32_t)t); return t; }
};

#ifndef ZX_WITH_SDL
int main(int argc, char **argv) {
	std::string model, if1rom, trdosrom; int argi=1; parse_model_and_roms(argc, argv, model, if1rom, trdosrom, argi);
	if (argc - argi < 1) { std::fprintf(stderr, "Usage: %s [--model=48|128|plus3|pentagon|scorpion] [--if1-rom=if1.rom] [--trdos-rom=trdos.rom] <rom> [file.sna|.tap|.tzx|.dsk|.trd|.mdr]\n", argv[0]); return 1; }
	ZXMachine zx; if (!zx.init_with_model(argv[argi], model, if1rom, trdosrom)) return 1;
	if (argc - argi >= 2) {
		std::string path = argv[argi+1];
		if (path.size() >= 4 && (path.rfind(".sna") == path.size()-4 || path.rfind(".SNA") == path.size()-4)) { std::vector<uint8_t> buf; if (load_file_to_buffer(path, buf)) loadSNA48(buf, zx.cpu, zx.memory); }
		else if (path.size() >= 4 && (path.rfind(".dsk") == path.size()-4 || path.rfind(".DSK") == path.size()-4)) { if (zx.dsk.load(path)) { zx.fdc.attachImage(&zx.dsk); std::fprintf(stderr, "Mounted DSK: %s\n", path.c_str()); } }
		else if (path.size() >= 4 && (path.rfind(".trd") == path.size()-4 || path.rfind(".TRD") == path.size()-4)) { if (zx.trd.load(path)) { zx.beta.attachImage(&zx.trd); std::fprintf(stderr, "Mounted TRD: %s\n", path.c_str()); } }
		else if (path.size() >= 4 && (path.rfind(".mdr") == path.size()-4 || path.rfind(".MDR") == path.size()-4)) { if (zx.mdr.load(path)) { zx.if1.mount(&zx.mdr); std::fprintf(stderr, "Mounted MDR: %s\n", path.c_str()); } }
		else if (path.size() >= 4 && (path.rfind(".scl") == path.size()-4 || path.rfind(".SCL") == path.size()-4)) {
			SclImage scl; if (scl.load(path)) { std::vector<uint8_t> trdRaw; int trk, hd; if (scl.toTrd(trdRaw, trk, hd)) { if (zx.trd.loadRaw(trdRaw)) { zx.beta.attachImage(&zx.trd); std::fprintf(stderr, "Mounted SCL as TRD\n"); } } }
		}
		else { if (zx.tape.load(path)) { std::fprintf(stderr, "Loaded tape: %s\nPress PLAY (F9) when ready.\n", path.c_str()); } }
	}
	for(;;) zx.stepInstruction();
}
#endif