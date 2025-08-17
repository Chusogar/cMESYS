#include "z80.h"
#include <cstring>

static inline uint16_t make16(uint8_t hi, uint8_t lo) { return (uint16_t(hi) << 8) | lo; }

void Z80Cpu::reset() {
	a = f = b = c = d = e = h = l = 0;
	a2 = f2 = b2 = c2 = d2 = e2 = h2 = l2 = 0;
	ix = iy = 0; sp = 0xFFFF; pc = 0x0000;
	i = r = 0; iff1 = iff2 = false; im = 0; halted = false;
	tstates = 0;
}

void Z80Cpu::nmi() {
	iff1 = false;
	// push PC
	uint16_t sp2 = sp - 1; memWrite(sp2, uint8_t(pc >> 8)); sp = sp2;
	sp2 = sp - 1; memWrite(sp2, uint8_t(pc & 0xFF)); sp = sp2;
	pc = 0x0066;
	halted = false;
}

void Z80Cpu::irq(bool level) {
	if (level && iff1) {
		iff1 = iff2 = false;
		// push PC
		uint16_t sp2 = sp - 1; memWrite(sp2, uint8_t(pc >> 8)); sp = sp2;
		sp2 = sp - 1; memWrite(sp2, uint8_t(pc & 0xFF)); sp = sp2;
		switch (im) {
			case 0: pc = 0x0038; break; // simplify
			case 1: pc = 0x0038; break;
			case 2: {
				uint16_t vec = make16(i, 0x00) + (a & 0xFE);
				uint8_t lo = memRead(vec);
				uint8_t hi = memRead(vec + 1);
				pc = make16(hi, lo);
				break;
			}
		}
		halted = false;
	}
}

// Helpers for flags
static inline uint8_t szp_table(uint8_t v) {
	// Precompute S,Z,PV from value
	static uint8_t tbl[256];
	static bool init = false;
	if (!init) {
		for (int i = 0; i < 256; ++i) {
			uint8_t f = 0;
			if (i & 0x80) f |= Z80Cpu::FLAG_S;
			if (i == 0) f |= Z80Cpu::FLAG_Z;
			int bits = 0; for (int b = 0; b < 8; ++b) if (i & (1 << b)) ++bits;
			if ((bits % 2) == 0) f |= Z80Cpu::FLAG_PV;
			tbl[i] = f;
		}
		init = true;
	}
	return tbl[v];
}

static inline uint8_t parity(uint8_t v) { return (szp_table(v) & Z80Cpu::FLAG_PV) ? 1 : 0; }

static inline bool cond_true(uint8_t f, int cc) {
	switch (cc) {
		case 0: return (f & Z80Cpu::FLAG_Z) == 0; // NZ
		case 1: return (f & Z80Cpu::FLAG_Z) != 0; // Z
		case 2: return (f & Z80Cpu::FLAG_C) == 0; // NC
		case 3: return (f & Z80Cpu::FLAG_C) != 0; // C
		case 4: return (f & Z80Cpu::FLAG_PV) == 0; // PO
		case 5: return (f & Z80Cpu::FLAG_PV) != 0; // PE
		case 6: return (f & Z80Cpu::FLAG_S) == 0; // P
		case 7: return (f & Z80Cpu::FLAG_S) != 0; // M
	}
	return false;
}

int Z80Cpu::step() {
	if (halted) {
		// HALT burns 4 T-states until interrupt
		tstates = 4;
		return tstates;
	}

	r = (r & 0x80) | ((r + 1) & 0x7F);
	uint8_t prefix = 0; // 0 none, 0xDD IX, 0xFD IY
restart_decode:
	uint8_t op = memRead(pc++);
	if (op == 0xDD || op == 0xFD) { prefix = op; op = memRead(pc++); }
	int t = 4; // default

	// Helpers using prefix
	auto hl_val = [&]() -> uint16_t { return (prefix==0xDD)? ix : (prefix==0xFD)? iy : hl(); };
	auto set_hl_val = [&](uint16_t v) { if (prefix==0xDD) ix = v; else if (prefix==0xFD) iy = v; else set_hl(v); };
	auto get_r_by_code = [&](int code)->uint8_t {
		switch (code) {
			case 0: return b;
			case 1: return c;
			case 2: return d;
			case 3: return e;
			case 4: if (prefix==0xDD) return uint8_t(ix >> 8); if (prefix==0xFD) return uint8_t(iy >> 8); return h;
			case 5: if (prefix==0xDD) return uint8_t(ix & 0xFF); if (prefix==0xFD) return uint8_t(iy & 0xFF); return l;
			case 7: return a;
			default: return 0; // 6 handled separately as (HL)
		}
	};
	auto set_r_by_code = [&](int code, uint8_t v){
		switch (code) {
			case 0: b = v; break;
			case 1: c = v; break;
			case 2: d = v; break;
			case 3: e = v; break;
			case 4: if (prefix==0xDD) { ix = uint16_t((uint16_t(v) << 8) | (ix & 0x00FF)); } else if (prefix==0xFD) { iy = uint16_t((uint16_t(v) << 8) | (iy & 0x00FF)); } else { h = v; } break;
			case 5: if (prefix==0xDD) { ix = uint16_t((ix & 0xFF00) | v); } else if (prefix==0xFD) { iy = uint16_t((iy & 0xFF00) | v); } else { l = v; } break;
			case 7: a = v; break;
			default: break;
		}
	};
	auto read_hl_mem = [&]() -> uint8_t {
		if (prefix==0xDD || prefix==0xFD) {
			int8_t d = int8_t(memRead(pc++));
			uint16_t addr = uint16_t(hl_val() + d);
			return memRead(addr);
		} else {
			return memRead(hl());
		}
	};
	auto write_hl_mem = [&](uint8_t v) {
		if (prefix==0xDD || prefix==0xFD) {
			int8_t d = int8_t(memRead(pc++));
			uint16_t addr = uint16_t(hl_val() + d);
			memWrite(addr, v);
		} else {
			memWrite(hl(), v);
		}
	};
	auto add16 = [&](uint16_t x, uint16_t y)->uint16_t{
		uint32_t res = uint32_t(x) + y;
		uint8_t nf = (f & (FLAG_S|FLAG_Z|FLAG_PV)) | ((res >> 8) & (FLAG_X|FLAG_Y));
		if (((x & 0x0FFF) + (y & 0x0FFF)) & 0x1000) nf |= FLAG_H;
		if (res & 0x10000) nf |= FLAG_C;
		f = nf; return uint16_t(res);
	};

	// ED prefixed
	if (op == 0xED) {
		uint8_t op2 = memRead(pc++);
		switch (op2) {
			case 0x47: /* LD I,A */ i = a; t = 9; break;
			case 0x4F: /* LD R,A */ r = (r & 0x80) | (a & 0x7F); t = 9; break;
			case 0x57: /* LD A,I */ a = i; f = (f & FLAG_C) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)) ; t = 9; break;
			case 0x5F: /* LD A,R */ a = (r & 0x7F) | (r & 0x80); f = (f & FLAG_C) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)); t = 9; break;
			case 0xA0: /* LDI */ {
				uint8_t v = memRead(hl()); memWrite(de(), v); set_hl(hl()+1); set_de(de()+1); set_bc(bc()-1);
				uint8_t nf = (f & FLAG_C) | ( ( ( ( ( ( ( (v + a) & 0x02) ? FLAG_X:0) | (((v + a) & 0x08)?FLAG_Y:0) ) ) ) ) ); // XY from A+v
				if (bc()!=0) nf |= FLAG_PV; f = nf; t=16; break; }
			case 0xB0: /* LDIR */ {
				uint16_t count = bc();
				if (count == 0) { f = (f & (FLAG_C)) & ~FLAG_PV; t = 16; break; }
				uint8_t v = memRead(hl()); memWrite(de(), v); set_hl(hl()+1); set_de(de()+1); set_bc(count-1);
				f = (f & (FLAG_C)) | ((bc()!=0)?FLAG_PV:0);
				t = 21; if (bc()!=0) { pc -= 2; }
				break; }
			case 0xA8: /* LDD */ {
				uint8_t v = memRead(hl()); memWrite(de(), v); set_hl(hl()-1); set_de(de()-1); set_bc(bc()-1);
				uint8_t nf = (f & FLAG_C); if (bc()!=0) nf |= FLAG_PV; f = nf; t=16; break; }
			case 0xB8: /* LDDR */ {
				uint16_t count = bc();
				if (count == 0) { f = (f & (FLAG_C)) & ~FLAG_PV; t = 16; break; }
				uint8_t v = memRead(hl()); memWrite(de(), v); set_hl(hl()-1); set_de(de()-1); set_bc(count-1);
				f = (f & (FLAG_C)) | ((bc()!=0)?FLAG_PV:0);
				t = 21; if (bc()!=0) { pc -= 2; }
				break; }
			case 0xA1: /* CPI */ {
				uint8_t v = memRead(hl()); uint8_t res = uint8_t(a - v); set_hl(hl()+1); set_bc(bc()-1);
				uint8_t nf = FLAG_N | (res & (FLAG_S|FLAG_Z|FLAG_X|FLAG_Y));
				if (((a ^ v ^ res) & 0x10)) nf |= FLAG_H; if (bc()!=0) nf |= FLAG_PV;
				f = (f & FLAG_C) | nf; t = 16; break; }
			case 0xB1: /* CPIR */ {
				uint8_t v = memRead(hl()); uint8_t res = uint8_t(a - v); set_hl(hl()+1); set_bc(bc()-1);
				uint8_t nf = FLAG_N | (res & (FLAG_S|FLAG_Z|FLAG_X|FLAG_Y)); if (((a ^ v ^ res) & 0x10)) nf |= FLAG_H;
				if (res != 0 && bc()!=0) { nf |= FLAG_PV; f = (f & FLAG_C) | nf; pc -= 2; t = 21; }
				else { f = (f & FLAG_C) | nf; t = 16; }
				break; }
			case 0xA9: /* CPD */ {
				uint8_t v = memRead(hl()); uint8_t res = uint8_t(a - v); set_hl(hl()-1); set_bc(bc()-1);
				uint8_t nf = FLAG_N | (res & (FLAG_S|FLAG_Z|FLAG_X|FLAG_Y)); if (((a ^ v ^ res) & 0x10)) nf |= FLAG_H; if (bc()!=0) nf |= FLAG_PV; f = (f & FLAG_C) | nf; t = 16; break; }
			case 0xB9: /* CPDR */ {
				uint8_t v = memRead(hl()); uint8_t res = uint8_t(a - v); set_hl(hl()-1); set_bc(bc()-1);
				uint8_t nf = FLAG_N | (res & (FLAG_S|FLAG_Z|FLAG_X|FLAG_Y)); if (((a ^ v ^ res) & 0x10)) nf |= FLAG_H;
				if (res != 0 && bc()!=0) { nf |= FLAG_PV; f = (f & FLAG_C) | nf; pc -= 2; t = 21; }
				else { f = (f & FLAG_C) | nf; t = 16; }
				break; }
			case 0x44: case 0x4C: case 0x54: case 0x5C: case 0x64: case 0x6C: case 0x74: case 0x7C: /* NEG */ {
				uint8_t olda = a; uint8_t res = uint8_t(0 - a); a = res; uint8_t nf = FLAG_N; if (a == 0) nf |= FLAG_Z; if (res & 0x80) nf |= FLAG_S; if (olda != 0) nf |= FLAG_C; if (((0 ^ olda ^ res) & 0x10)) nf |= FLAG_H; if ((olda & 0x80) && (res & 0x80)) nf |= FLAG_PV; f = nf; t=8; break; }
			case 0x45: /* RETN */ iff1 = iff2; { uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); pc = make16(hi,lo); } t=14; break;
			case 0x4D: /* RETI */ { uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); pc = make16(hi,lo); } t=14; break;
			case 0x46: case 0x66: case 0x4E: case 0x6E: /* IM 0 */ im = 0; t=8; break;
			case 0x56: case 0x76: /* IM 1 */ im = 1; t=8; break;
			case 0x5E: case 0x7E: /* IM 2 */ im = 2; t=8; break;
			case 0x67: /* RRD */ { uint8_t v = memRead(hl()); uint8_t lo = v & 0x0F; uint8_t hi = (v >> 4) & 0x0F; uint8_t newv = uint8_t((a & 0x0F) << 4) | lo; a = (a & 0xF0) | hi; memWrite(hl(), newv); f = (f & FLAG_C) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)) | (a & (FLAG_X|FLAG_Y)); t=18; break; }
			case 0x6F: /* RLD */ { uint8_t v = memRead(hl()); uint8_t lo = v & 0x0F; uint8_t hi = (v >> 4) & 0x0F; uint8_t newv = uint8_t((v << 4) | (a & 0x0F)); a = (a & 0xF0) | hi; memWrite(hl(), newv); f = (f & FLAG_C) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)) | (a & (FLAG_X|FLAG_Y)); t=18; break; }
			// ADC HL,ss and SBC HL,ss
			case 0x4A: case 0x5A: case 0x6A: case 0x7A: { // ADC HL,ss
				uint16_t ss = (op2>>4)&3; uint16_t val = (ss==0)?bc(): (ss==1)?de(): (ss==2)?hl(): sp; uint32_t res = uint32_t(hl()) + val + ((f & FLAG_C)?1:0);
				uint8_t nf = 0; if (res & 0x10000) nf |= FLAG_C; if (((hl() ^ val ^ res) & 0x1000)) nf |= FLAG_H; uint16_t r16 = uint16_t(res);
				uint8_t ov = (~( (hl() ^ val) ) & ( (hl() ^ r16) ) & 0x8000) ? FLAG_PV : 0; if (r16 & 0x8000) nf |= FLAG_S; if (r16==0) nf |= FLAG_Z; f = (nf | ov) & ~(FLAG_N); set_hl(r16); t=15; break; }
			case 0x42: case 0x52: case 0x62: case 0x72: { // SBC HL,ss
				uint16_t ss = (op2>>4)&3; uint16_t val = (ss==0)?bc(): (ss==1)?de(): (ss==2)?hl(): sp; uint32_t res = uint32_t(hl()) - val - ((f & FLAG_C)?1:0);
				uint8_t nf = FLAG_N; if (res & 0x10000) nf |= FLAG_C; if (((hl() ^ val ^ res) & 0x1000)) nf |= FLAG_H; uint16_t r16 = uint16_t(res);
				uint8_t ov = (( (hl() ^ val) & (hl() ^ r16) ) & 0x8000) ? FLAG_PV : 0; if (r16 & 0x8000) nf |= FLAG_S; if (r16==0) nf |= FLAG_Z; f = (nf | ov); set_hl(r16); t=15; break; }
			// LD (nn),dd and LD dd,(nn)
			case 0x43: case 0x53: case 0x63: case 0x73: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t addr = make16(hi,lo); uint16_t val = ( ((op2>>4)&3)==0)? bc() : (((op2>>4)&3)==1)? de() : (((op2>>4)&3)==2)? hl() : sp; memWrite(addr, uint8_t(val & 0xFF)); memWrite(addr+1, uint8_t(val>>8)); t=20; break; }
			case 0x4B: case 0x5B: case 0x6B: case 0x7B: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t addr = make16(hi,lo); uint8_t lo2 = memRead(addr); uint8_t hi2 = memRead(addr+1); uint16_t val = make16(hi2, lo2); switch( (op2>>4)&3 ){ case 0: set_bc(val); break; case 1: set_de(val); break; case 2: set_hl(val); break; case 3: sp = val; break; } t=20; break; }
			// IN r,(C) and OUT (C),r
			case 0x40: case 0x48: case 0x50: case 0x58: case 0x60: case 0x68: case 0x78: { uint8_t rcode = (op2>>3)&7; uint16_t port = (uint16_t(b)<<8) | c; uint8_t val = ioRead(port); switch(rcode){ case 0:b=val;break; case 1:c=val;break; case 2:d=val;break; case 3:e=val;break; case 4:h=val;break; case 5:l=val;break; case 7:a=val;break; default: break; } f = (f & FLAG_C) | (szp_table(val) & (FLAG_S|FLAG_Z|FLAG_PV)) | (val & (FLAG_X|FLAG_Y)); t=12; break; }
			case 0x41: case 0x49: case 0x51: case 0x59: case 0x61: case 0x69: case 0x79: { uint8_t rcode = (op2>>3)&7; uint16_t port = (uint16_t(b)<<8) | c; uint8_t val = 0; switch(rcode){ case 0:val=b;break; case 1:val=c;break; case 2:val=d;break; case 3:val=e;break; case 4:val=h;break; case 5:val=l;break; case 7:val=a;break; default: break; } ioWrite(port, val); t=12; break; }
			default:
				// Treat unknown ED-prefixed as NOP timing 8
				t = 8; break;
		}
		tstates = t; return tstates;
	}

	if (op == 0xCB) {
		// CB group, possibly DD/FD CB with displacement handled below when prefix set
		if (prefix == 0xDD || prefix == 0xFD) {
			int8_t disp = int8_t(memRead(pc++));
			uint8_t op2 = memRead(pc++);
			int y = (op2 >> 3) & 7; int z = op2 & 7; int x = (op2 >> 6) & 3;
			uint16_t base = hl_val(); uint16_t addr = uint16_t(base + disp); uint8_t v = memRead(addr);
			uint8_t res = v; uint8_t cf = f & FLAG_C; uint8_t nf;
			if (x == 0) {
				switch(y){
					case 0: cf = (v>>7)&1; res = uint8_t((v<<1)|(v>>7)); break; // RLC
					case 1: cf = v & 1; res = uint8_t((v>>1)|(v<<7)); break; // RRC
					case 2: { uint8_t newc = (v>>7)&1; res = uint8_t((v<<1) | (f&FLAG_C?1:0)); cf = newc; break; } // RL
					case 3: { uint8_t newc = v & 1; res = uint8_t((v>>1) | (f&FLAG_C?0x80:0)); cf = newc; break; } // RR
					case 4: cf = (v>>7)&1; res = uint8_t(v<<1); break; // SLA
					case 5: cf = v & 1; res = uint8_t((v>>1) | (v & 0x80)); break; // SRA
					case 6: cf = (v>>7)&1; res = uint8_t((v<<1)|1); break; // SLL (undoc)
					case 7: cf = v & 1; res = uint8_t(v>>1); break; // SRL
				}
				memWrite(addr, res);
				nf = (res & (FLAG_S|FLAG_Z|FLAG_X|FLAG_Y)); if (parity(res)) nf |= FLAG_PV; if (cf) nf |= FLAG_C; f = nf;
				if (z != 6) set_r_by_code(z, res);
				t = 23;
			} else if (x == 1) {
				uint8_t mask = uint8_t(1 << y); nf = (f & FLAG_C) | FLAG_H | ( ( (addr>>8) & (FLAG_X|FLAG_Y) ) ); if ((v & mask) == 0) nf |= FLAG_Z | FLAG_PV; if (y==7 && (v&0x80)) nf |= FLAG_S; f = nf; t = 20;
			} else if (x == 2) {
				uint8_t nv = uint8_t(v & ~(1<<y)); memWrite(addr, nv); if (z != 6) set_r_by_code(z, nv); t = 23;
			} else {
				uint8_t nv = uint8_t(v | (1<<y)); memWrite(addr, nv); if (z != 6) set_r_by_code(z, nv); t = 23;
			}
			tstates = t; return tstates;
		}

		uint8_t op2 = memRead(pc++);
		int y = (op2 >> 3) & 7; int z = op2 & 7; int x = (op2 >> 6) & 3;
		auto getRef = [&](int idx, uint8_t **preg)->uint8_t{
			switch(idx){ case 0: *preg=&b; return b; case 1:*preg=&c; return c; case 2:*preg=&d; return d; case 3:*preg=&e; return e; case 4:*preg=&h; return h; case 5:*preg=&l; return l; case 6:*preg=nullptr; return memRead(hl()); case 7:*preg=&a; return a; }
			return 0; };
		auto writeRef = [&](int idx, uint8_t v){ switch(idx){ case 0:b=v;break; case 1:c=v;break; case 2:d=v;break; case 3:e=v;break; case 4:h=v;break; case 5:l=v;break; case 6:memWrite(hl(),v);break; case 7:a=v;break; } };
		uint8_t *preg=nullptr; uint8_t v = getRef(z, &preg);
		if (x == 0) {
			// Rotates/Shifts
			uint8_t res = v; uint8_t cf = f & FLAG_C;
			switch(y){
				case 0: /* RLC */ cf = (v>>7)&1; res = uint8_t((v<<1)|(v>>7)); break;
				case 1: /* RRC */ cf = v & 1; res = uint8_t((v>>1)|(v<<7)); break;
				case 2: /* RL */ { uint8_t newc = (v>>7)&1; res = uint8_t((v<<1) | (f&FLAG_C?1:0)); cf = newc; break; }
				case 3: /* RR */ { uint8_t newc = v & 1; res = uint8_t((v>>1) | (f&FLAG_C?0x80:0)); cf = newc; break; }
				case 4: /* SLA */ cf = (v>>7)&1; res = uint8_t(v<<1); break;
				case 5: /* SRA */ cf = v & 1; res = uint8_t((v>>1) | (v & 0x80)); break;
				case 6: /* SLL (undoc) */ cf = (v>>7)&1; res = uint8_t((v<<1)|1); break;
				case 7: /* SRL */ cf = v & 1; res = uint8_t(v>>1); break;
			}
			writeRef(z, res);
			uint8_t nf = (res & (FLAG_S|FLAG_Z|FLAG_X|FLAG_Y));
			{int bits=0; for(int b=0;b<8;++b) if(res&(1<<b))++bits; if((bits%2)==0) nf|=FLAG_PV;}
			if (cf) nf |= FLAG_C; f = nf; t = (z==6)?15:8;
		} else if (x == 1) {
			// BIT y, r
			uint8_t mask = uint8_t(1 << y);
			uint8_t nf = (f & FLAG_C) | FLAG_H | (v & (FLAG_X|FLAG_Y));
			if ((v & mask) == 0) nf |= FLAG_Z | FLAG_PV;
			if (y == 7 && (v & 0x80)) nf |= FLAG_S;
			f = nf; t = (z==6)?12:8;
		} else if (x == 2) {
			// RES y, r
			writeRef(z, uint8_t(v & ~(1<<y))); t = (z==6)?15:8;
		} else {
			// SET y, r
			writeRef(z, uint8_t(v | (1<<y))); t = (z==6)?15:8;
		}
		tstates = t; return tstates;
	}

	switch (op) {
		case 0x00: /* NOP */ t = 4; break;
		// 8-bit immediate loads
		case 0x3E: a = memRead(pc++); t = 7; break;
		case 0x06: b = memRead(pc++); t = 7; break;
		case 0x0E: c = memRead(pc++); t = 7; break;
		case 0x16: d = memRead(pc++); t = 7; break;
		case 0x1E: e = memRead(pc++); t = 7; break;
		case 0x26: if (prefix) { if (prefix==0xDD) ix = uint16_t((uint16_t(memRead(pc++))<<8) | (ix & 0x00FF)); else iy = uint16_t((uint16_t(memRead(pc++))<<8) | (iy & 0x00FF)); t=11; break; } else { h = memRead(pc++); t=7; break; }
		case 0x2E: if (prefix) { if (prefix==0xDD) ix = uint16_t((ix & 0xFF00) | memRead(pc++)); else iy = uint16_t((iy & 0xFF00) | memRead(pc++)); t=11; break; } else { l = memRead(pc++); t=7; break; }

		// LD (nn),A and LD A,(nn)
		case 0x32: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); memWrite(make16(hi,lo), a); t = 13; break; }
		case 0x3A: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); a = memRead(make16(hi,lo)); t = 13; f = (f & (FLAG_C|FLAG_N)) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)); break; }
		// LD A,(BC)/(DE) and LD (BC)/(DE),A
		case 0x0A: a = memRead(bc()); t=7; break;
		case 0x1A: a = memRead(de()); t=7; break;
		case 0x02: memWrite(bc(), a); t=7; break;
		case 0x12: memWrite(de(), a); t=7; break;

		// 16-bit immediate loads
		case 0x21: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); set_hl_val(make16(hi,lo)); t = 10; break; }
		case 0x31: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); sp = make16(hi,lo); t = 10; break; }
		case 0x11: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); set_de(make16(hi,lo)); t = 10; break; }
		case 0x01: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); set_bc(make16(hi,lo)); t = 10; break; }
		case 0x2A: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t a16 = make16(hi,lo); uint8_t lo2 = memRead(a16); uint8_t hi2 = memRead(a16+1); set_hl_val(make16(hi2,lo2)); t = 16; break; }
		case 0x22: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t a16 = make16(hi,lo); uint16_t v = hl_val(); memWrite(a16, uint8_t(v & 0xFF)); memWrite(a16+1, uint8_t(v>>8)); t = 16; break; }

		// 16-bit INC/DEC
		case 0x03: set_bc(bc()+1); t=6; break;
		case 0x13: set_de(de()+1); t=6; break;
		case 0x23: set_hl_val(hl_val()+1); t=6; break;
		case 0x33: sp = sp + 1; t=6; break;
		case 0x0B: set_bc(bc()-1); t=6; break;
		case 0x1B: set_de(de()-1); t=6; break;
		case 0x2B: set_hl_val(hl_val()-1); t=6; break;
		case 0x3B: sp = sp - 1; t=6; break;

		// ADD HL/IX/IY, ss
		case 0x09: set_hl_val(add16(hl_val(), bc())); t=11; break;
		case 0x19: set_hl_val(add16(hl_val(), de())); t=11; break;
		case 0x29: set_hl_val(add16(hl_val(), hl_val())); t=11; break;
		case 0x39: set_hl_val(add16(hl_val(), sp)); t=11; break;

		// PUSH/POP
		case 0xC5: { uint16_t sp2 = sp - 1; memWrite(sp2, b); sp = sp2; sp2 = sp - 1; memWrite(sp2, c); sp = sp2; t = 11; break; }
		case 0xD5: { uint16_t sp2 = sp - 1; memWrite(sp2, d); sp = sp2; sp2 = sp - 1; memWrite(sp2, e); sp = sp2; t = 11; break; }
		case 0xE5: { if (prefix) { uint16_t sp2 = sp - 1; memWrite(sp2, uint8_t(hl_val() >> 8)); sp = sp2; sp2 = sp - 1; memWrite(sp2, uint8_t(hl_val() & 0xFF)); sp = sp2; } else { uint16_t sp2 = sp - 1; memWrite(sp2, h); sp = sp2; sp2 = sp - 1; memWrite(sp2, l); sp = sp2; } t = 11; break; }
		case 0xF5: { uint16_t sp2 = sp - 1; memWrite(sp2, a); sp = sp2; sp2 = sp - 1; memWrite(sp2, f); sp = sp2; t = 11; break; }
		case 0xC1: { uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); set_bc(make16(hi,lo)); t = 10; break; }
		case 0xD1: { uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); set_de(make16(hi,lo)); t = 10; break; }
		case 0xE1: { if (prefix) { uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); set_hl_val(make16(hi,lo)); } else { uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); set_hl(make16(hi,lo)); } t = 10; break; }
		case 0xF1: { uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); set_af(make16(hi,lo)); t = 10; break; }

		// LD r,r' and through (HL) with index
		case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x47:
		case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4F:
		case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x57:
		case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5F:
		case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x67:
		case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6F:
		case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7F: {
			int y = (op >> 3) & 7; int z = op & 7; if (z == 6) { // LD r,(HL)
				uint8_t v = read_hl_mem(); set_r_by_code(y, v); t = (prefix?19:7); break;
			} else if (y == 6) { // LD (HL),r
				uint8_t v = get_r_by_code(z); write_hl_mem(v); t = (prefix?19:7); break;
			} else {
				uint8_t v = get_r_by_code(z); set_r_by_code(y, v); t = 4; break;
			}
		}
		case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: { uint8_t v = get_r_by_code(op & 7); write_hl_mem(v); t = (prefix?19:7); break; }
		case 0x36: { uint8_t n = memRead(pc++); write_hl_mem(n); t = (prefix?19:10); break; }

		// INC/DEC r and (HL)
		case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C: {
			int rcode = (op >> 3) & 7; if (rcode == 6) { uint8_t v = read_hl_mem(); uint8_t res = uint8_t(v + 1); uint8_t nf = (f & FLAG_C) | (res & (FLAG_X|FLAG_Y)); if ((res & 0x0F) == 0x00) nf |= FLAG_H; if (res == 0x80) nf |= FLAG_PV; if (res == 0x00) nf |= FLAG_Z; if (res & 0x80) nf |= FLAG_S; f = nf; write_hl_mem(res); t = (prefix?23:11); }
			else { uint8_t v = get_r_by_code(rcode); uint8_t res = uint8_t(v + 1); uint8_t nf = (f & FLAG_C) | (res & (FLAG_X|FLAG_Y)); if ((res & 0x0F) == 0x00) nf |= FLAG_H; if (res == 0x80) nf |= FLAG_PV; if (res == 0x00) nf |= FLAG_Z; if (res & 0x80) nf |= FLAG_S; f = nf; set_r_by_code(rcode, res); t = 4; } break; }
		case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D: {
			int rcode = (op >> 3) & 7; if (rcode == 6) { uint8_t v = read_hl_mem(); uint8_t res = uint8_t(v - 1); uint8_t nf = (f & FLAG_C) | FLAG_N | (res & (FLAG_X|FLAG_Y)); if ((res & 0x0F) == 0x0F) nf |= FLAG_H; if (res == 0x7F) nf |= FLAG_PV; if (res == 0x00) nf |= FLAG_Z; if (res & 0x80) nf |= FLAG_S; f = nf; write_hl_mem(res); t = (prefix?23:11); }
			else { uint8_t v = get_r_by_code(rcode); uint8_t res = uint8_t(v - 1); uint8_t nf = (f & FLAG_C) | FLAG_N | (res & (FLAG_X|FLAG_Y)); if ((res & 0x0F) == 0x0F) nf |= FLAG_H; if (res == 0x7F) nf |= FLAG_PV; if (res == 0x00) nf |= FLAG_Z; if (res & 0x80) nf |= FLAG_S; f = nf; set_r_by_code(rcode, res); t = 4; } break; }

		// ALU A,r and with (HL)
		case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87: {
			int z = op & 7; uint8_t s = (z==6)? read_hl_mem() : get_r_by_code(z);
			uint16_t res = uint16_t(a) + s;
			uint8_t aflags = 0; if (res & 0x100) aflags |= FLAG_C; if (((a ^ s ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res & 0xFF); uint8_t pv = (~(a ^ s) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = (z==6)? (prefix?19:7) : 4; break; }
		case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
			int z = op & 7; uint8_t s = (z==6)? read_hl_mem() : get_r_by_code(z);
			uint16_t res = uint16_t(a) + s + ((f & FLAG_C)?1:0);
			uint8_t aflags = 0; if (res & 0x100) aflags |= FLAG_C; if (((a ^ s ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res); uint8_t pv = (~(a ^ s) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = (z==6)? (prefix?19:7) : 4; break; }
		case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
			int z = op & 7; uint8_t s = (z==6)? read_hl_mem() : get_r_by_code(z);
			uint16_t res = uint16_t(a) - s; uint8_t aflags = FLAG_N; if (res & 0x100) aflags |= FLAG_C; if (((a ^ s ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res); uint8_t pv = ((a ^ s) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = (z==6)? (prefix?19:7) : 4; break; }
		case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
			int z = op & 7; uint8_t s = (z==6)? read_hl_mem() : get_r_by_code(z);
			uint16_t res = uint16_t(a) - s - ((f & FLAG_C)?1:0); uint8_t aflags = FLAG_N; if (res & 0x100) aflags |= FLAG_C; if (((a ^ s ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res); uint8_t pv = ((a ^ s) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = (z==6)? (prefix?19:7) : 4; break; }
		case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7: { int z = op & 7; uint8_t s = (z==6)? read_hl_mem() : get_r_by_code(z); a = a & s; f = FLAG_H | (a & (FLAG_S|FLAG_Z|FLAG_PV|FLAG_X|FLAG_Y)); if (a==0) f |= FLAG_Z; t = (z==6)? (prefix?19:7) : 4; break; }
		case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: { int z = op & 7; uint8_t s = (z==6)? read_hl_mem() : get_r_by_code(z); a = a ^ s; f = (a & (FLAG_S|FLAG_Z|FLAG_PV|FLAG_X|FLAG_Y)); if (a==0) f |= FLAG_Z; {int bits=0; for(int b=0;b<8;++b) if(a&(1<<b))++bits; if((bits%2)==0) f|=FLAG_PV;} t = (z==6)? (prefix?19:7) : 4; break; }
		case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: { int z = op & 7; uint8_t s = (z==6)? read_hl_mem() : get_r_by_code(z); a = a | s; f = (a & (FLAG_S|FLAG_Z|FLAG_PV|FLAG_X|FLAG_Y)); if (a==0) f |= FLAG_Z; {int bits=0; for(int b=0;b<8;++b) if(a&(1<<b))++bits; if((bits%2)==0) f|=FLAG_PV;} t = (z==6)? (prefix?19:7) : 4; break; }
		case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: { int z = op & 7; uint8_t s = (z==6)? read_hl_mem() : get_r_by_code(z); uint8_t res = uint8_t(a - s); uint8_t nf = FLAG_N | (res & (FLAG_S|FLAG_Z|FLAG_X|FLAG_Y)); if (((a ^ s ^ res) & 0x10)) nf |= FLAG_H; if (((a ^ s) & (a ^ res)) & 0x80) nf |= FLAG_PV; if (a < s) nf |= FLAG_C; f = nf; t = (z==6)? (prefix?19:7) : 4; break; }

		// Immediate ALU
		case 0xC6: { uint8_t n = memRead(pc++); uint16_t res = uint16_t(a) + n; uint8_t aflags = 0; if (res & 0x100) aflags |= FLAG_C; if (((a ^ n ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res); uint8_t pv = (~(a ^ n) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = 7; break; }
		case 0xCE: { uint8_t n = memRead(pc++); uint16_t res = uint16_t(a) + n + ((f & FLAG_C)?1:0); uint8_t aflags = 0; if (res & 0x100) aflags |= FLAG_C; if (((a ^ n ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res); uint8_t pv = (~(a ^ n) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = 7; break; }
		case 0xD6: { uint8_t n = memRead(pc++); uint16_t res = uint16_t(a) - n; uint8_t aflags = FLAG_N; if (res & 0x100) aflags |= FLAG_C; if (((a ^ n ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res); uint8_t pv = ((a ^ n) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = 7; break; }
		case 0xDE: { uint8_t n = memRead(pc++); uint16_t res = uint16_t(a) - n - ((f & FLAG_C)?1:0); uint8_t aflags = FLAG_N; if (res & 0x100) aflags |= FLAG_C; if (((a ^ n ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res); uint8_t pv = ((a ^ n) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = 7; break; }
		case 0xE6: { uint8_t n = memRead(pc++); a = a & n; f = FLAG_H | (a & (FLAG_S|FLAG_Z|FLAG_PV|FLAG_X|FLAG_Y)); if (a==0) f |= FLAG_Z; {int bits=0; for(int b=0;b<8;++b) if(a&(1<<b))++bits; if((bits%2)==0) f|=FLAG_PV;} t = 7; break; }
		case 0xEE: { uint8_t n = memRead(pc++); a = a ^ n; f = (a & (FLAG_S|FLAG_Z|FLAG_PV|FLAG_X|FLAG_Y)); if (a==0) f |= FLAG_Z; {int bits=0; for(int b=0;b<8;++b) if(a&(1<<b))++bits; if((bits%2)==0) f|=FLAG_PV;} t = 7; break; }
		case 0xF6: { uint8_t n = memRead(pc++); a = a | n; f = (a & (FLAG_S|FLAG_Z|FLAG_PV|FLAG_X|FLAG_Y)); if (a==0) f |= FLAG_Z; {int bits=0; for(int b=0;b<8;++b) if(a&(1<<b))++bits; if((bits%2)==0) f|=FLAG_PV;} t = 7; break; }
		case 0xFE: { uint8_t n = memRead(pc++); uint8_t res = uint8_t(a - n); uint8_t nf = FLAG_N | (res & (FLAG_S|FLAG_Z|FLAG_X|FLAG_Y)); if (((a ^ n ^ res) & 0x10)) nf |= FLAG_H; if (((a ^ n) & (a ^ res)) & 0x80) nf |= FLAG_PV; if (a < n) nf |= FLAG_C; f = nf; t = 7; break; }

		// Rotates, flags
		case 0x07: { uint8_t c = (a >> 7) & 1; a = uint8_t((a << 1) | c); f = (f & (FLAG_S|FLAG_Z|FLAG_PV)) | (a & (FLAG_X|FLAG_Y)); if (c) f |= FLAG_C; f &= ~FLAG_H; f &= ~FLAG_N; t=4; break; } // RLCA
		case 0x0F: { uint8_t c = a & 1; a = uint8_t((a >> 1) | (c << 7)); f = (f & (FLAG_S|FLAG_Z|FLAG_PV)) | (a & (FLAG_X|FLAG_Y)); if (c) f |= FLAG_C; f &= ~FLAG_H; f &= ~FLAG_N; t=4; break; } // RRCA
		case 0x17: { uint8_t c = (f & FLAG_C)?1:0; uint8_t newc = (a >> 7) & 1; a = uint8_t((a << 1) | c); f = (f & (FLAG_S|FLAG_Z|FLAG_PV)) | (a & (FLAG_X|FLAG_Y)); if (newc) f |= FLAG_C; f &= ~FLAG_H; f &= ~FLAG_N; t=4; break; } // RLA
		case 0x1F: { uint8_t c = (f & FLAG_C)?1:0; uint8_t newc = a & 1; a = uint8_t((a >> 1) | (c << 7)); f = (f & (FLAG_S|FLAG_Z|FLAG_PV)) | (a & (FLAG_X|FLAG_Y)); if (newc) f |= FLAG_C; f &= ~FLAG_H; f &= ~FLAG_N; t=4; break; } // RRA
		case 0x27: { // DAA
			uint8_t adj = 0; uint8_t cf = 0; if ((f & FLAG_H) || ((a & 0x0F) > 9)) adj |= 0x06; if ((f & FLAG_C) || (a > 0x99)) { adj |= 0x60; cf = FLAG_C; }
			if (f & FLAG_N) { uint8_t prev = a; a = uint8_t(a - adj); }
			else { a = uint8_t(a + adj); }
			f = (f & (FLAG_N)) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)) | (a & (FLAG_X|FLAG_Y)) | cf; t=4; break; }
		case 0x2F: a = ~a; f = (f & (FLAG_C|FLAG_PV|FLAG_S|FLAG_Z)) | FLAG_H | FLAG_N | (a & (FLAG_X|FLAG_Y)); t=4; break; // CPL
		case 0x37: f = (f & (FLAG_S|FLAG_Z|FLAG_PV)) | FLAG_C | FLAG_H | (a & (FLAG_X|FLAG_Y)); t=4; break; // SCF
		case 0x3F: { uint8_t c = (f & FLAG_C)?FLAG_C:0; f = (f & (FLAG_S|FLAG_Z|FLAG_PV)) | ((c)?0:FLAG_C) | (a & (FLAG_X|FLAG_Y)); t=4; break; } // CCF

		// LD SP,HL/IX/IY
		case 0xF9: sp = hl_val(); t=6; break;

		// Jumps and calls
		case 0xC3: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); pc = make16(hi,lo); t=10; break; }
		case 0xE9: pc = hl_val(); t = 4; break;
		case 0x18: { int8_t e = int8_t(memRead(pc++)); pc = uint16_t(pc + e); t = 12; break; }
		case 0x20: case 0x28: case 0x30: case 0x38: { int cc = (op>>3)&3; int8_t e = int8_t(memRead(pc++)); bool take = false; if (op==0x20) take = !((f & FLAG_Z)!=0); if (op==0x28) take = ((f & FLAG_Z)!=0); if (op==0x30) take = !((f & FLAG_C)!=0); if (op==0x38) take = ((f & FLAG_C)!=0); if (take) { pc = uint16_t(pc + e); t=12; } else { t=7; } break; }
		case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA: { int cc = (op>>3)&7; uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); if (cond_true(f,cc)) { pc = make16(hi,lo); t=10; } else { t=10; } break; }
		case 0xCD: { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t addr = make16(hi,lo); uint16_t sp2 = sp - 1; memWrite(sp2, uint8_t(pc >> 8)); sp = sp2; sp2 = sp - 1; memWrite(sp2, uint8_t(pc & 0xFF)); sp = sp2; pc = addr; t = 17; break; }
		case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC: { int cc = (op>>3)&7; uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); if (cond_true(f,cc)) { uint16_t addr = make16(hi,lo); uint16_t sp2 = sp - 1; memWrite(sp2, uint8_t(pc >> 8)); sp = sp2; sp2 = sp - 1; memWrite(sp2, uint8_t(pc & 0xFF)); sp = sp2; pc = addr; t=17; } else { t=10; } break; }
		case 0xC9: { uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); pc = make16(hi,lo); t = 10; break; }
		case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8: { int cc = (op>>3)&7; if (cond_true(f,cc)) { uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); pc = make16(hi,lo); t=11; } else { t=5; } break; }
		case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF: { uint16_t sp2 = sp - 1; memWrite(sp2, uint8_t(pc >> 8)); sp = sp2; sp2 = sp - 1; memWrite(sp2, uint8_t(pc & 0xFF)); sp = sp2; pc = uint16_t(op & 0x38); t=11; break; }

		// EX and variants
		case 0x08: { uint8_t ta=a, tf=f; a=a2; f=f2; a2=ta; f2=tf; t=4; break; } // EX AF,AF'
		case 0xEB: { uint8_t tb=b, tc=c; b=d; c=e; d=tb; e=tc; t=4; break; } // EX DE,HL
		case 0xE3: { uint8_t lo = memRead(sp); uint8_t hi = memRead(sp+1); uint16_t tmp = make16(hi,lo); memWrite(sp, uint8_t(hl_val() & 0xFF)); memWrite(sp+1, uint8_t(hl_val()>>8)); set_hl_val(tmp); t=19; break; } // EX (SP),HL/IX/IY
		case 0xD9: { // EXX
			uint8_t tb=b,tc=c,td=d,te=e,th=h,tl=l; b=b2; c=c2; d=d2; e=e2; h=h2; l=l2; b2=tb; c2=tc; d2=td; e2=te; h2=th; l2=tl; t=4; break; }

		// I/O immediate
		case 0xDB: { uint8_t n = memRead(pc++); uint16_t port = (uint16_t(a)<<8) | n; a = ioRead(port); f = (f & FLAG_C) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)) | (a & (FLAG_X|FLAG_Y)); t=11; break; }
		case 0xD3: { uint8_t n = memRead(pc++); uint16_t port = (uint16_t(a)<<8) | n; ioWrite(port, a); t=11; break; }

		// HALT / EI / DI handled
		case 0xF3: iff1 = false; iff2 = false; t = 4; break;
		case 0xFB: iff1 = true; iff2 = true; t = 4; break;
		case 0x76: halted = true; t = 4; break;

		default:
			// Unimplemented prefixes for IX/IY specific ops
			if (prefix) {
				// Handle selected IX/IY ops: LD IX,nn; LD (nn),IX; LD IX,(nn); INC/DEC IX; ADD IX,rr; JP (IX) via 0xE9 handled; LD SP,IX via 0xF9 handled
				if (op == 0x21) { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); set_hl_val(make16(hi,lo)); t=14; }
				else if (op == 0x22) { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t a16 = make16(hi,lo); uint16_t v = hl_val(); memWrite(a16, uint8_t(v & 0xFF)); memWrite(a16+1, uint8_t(v>>8)); t=20; }
				else if (op == 0x2A) { uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t a16 = make16(hi,lo); uint8_t lo2 = memRead(a16); uint8_t hi2 = memRead(a16+1); set_hl_val(make16(hi2,lo2)); t=20; }
				else if (op == 0x23) { set_hl_val(hl_val()+1); t=10; }
				else if (op == 0x2B) { set_hl_val(hl_val()-1); t=10; }
				else if (op == 0x09) { set_hl_val(add16(hl_val(), bc())); t=15; }
				else if (op == 0x19) { set_hl_val(add16(hl_val(), de())); t=15; }
				else if (op == 0x29) { set_hl_val(add16(hl_val(), hl_val())); t=15; }
				else if (op == 0x39) { set_hl_val(add16(hl_val(), sp)); t=15; }
				else {
					// Unknown IX/IY op: treat as NOP for now
					t = 4;
				}
				prefix = 0; // clear prefix after handling
				break;
			}
			// Unimplemented: treat as NOP
			t = 4; break;
	}
	
	tstates = t;
	return tstates;
}