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

int Z80Cpu::step() {
	if (halted) {
		// HALT burns 4 T-states until interrupt
		tstates = 4;
		return tstates;
	}

	r = (r & 0x80) | ((r + 1) & 0x7F);
	uint8_t op = memRead(pc++);
	int t = 4; // default
	switch (op) {
		case 0x00: /* NOP */ t = 4; break;
		case 0x3E: /* LD A,n */ a = memRead(pc++); t = 7; break;
		case 0x06: /* LD B,n */ b = memRead(pc++); t = 7; break;
		case 0x0E: /* LD C,n */ c = memRead(pc++); t = 7; break;
		case 0x16: /* LD D,n */ d = memRead(pc++); t = 7; break;
		case 0x1E: /* LD E,n */ e = memRead(pc++); t = 7; break;
		case 0x26: /* LD H,n */ h = memRead(pc++); t = 7; break;
		case 0x2E: /* LD L,n */ l = memRead(pc++); t = 7; break;
		case 0x32: { /* LD (nn),A */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); memWrite(make16(hi,lo), a); t = 13; break; }
		case 0x3A: { /* LD A,(nn) */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); a = memRead(make16(hi,lo)); t = 13; f = (f & (FLAG_C|FLAG_N)) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)); break; }
		case 0x21: { /* LD HL,nn */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); set_hl(make16(hi,lo)); t = 10; break; }
		case 0x31: { /* LD SP,nn */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); sp = make16(hi,lo); t = 10; break; }
		case 0x11: { /* LD DE,nn */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); set_de(make16(hi,lo)); t = 10; break; }
		case 0x01: { /* LD BC,nn */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); set_bc(make16(hi,lo)); t = 10; break; }
		case 0x2A: { /* LD HL,(nn) */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t a16 = make16(hi,lo); uint8_t lo2 = memRead(a16); uint8_t hi2 = memRead(a16+1); set_hl(make16(hi2,lo2)); t = 16; break; }
		case 0x22: { /* LD (nn),HL */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t a16 = make16(hi,lo); memWrite(a16, l); memWrite(a16+1, h); t = 16; break; }
		/* removed invalid duplicate case */
		case 0xCD: { /* CALL nn */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); uint16_t addr = make16(hi,lo); uint16_t sp2 = sp - 1; memWrite(sp2, uint8_t(pc >> 8)); sp = sp2; sp2 = sp - 1; memWrite(sp2, uint8_t(pc & 0xFF)); sp = sp2; pc = addr; t = 17; break; }
		case 0xC9: { /* RET */ uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); pc = make16(hi,lo); t = 10; break; }
		case 0xC3: { /* JP nn */ uint8_t lo = memRead(pc++); uint8_t hi = memRead(pc++); pc = make16(hi,lo); t = 10; break; }
		case 0x18: { /* JR e */ int8_t e = int8_t(memRead(pc++)); pc = uint16_t(pc + e); t = 12; break; }
		case 0x20: { /* JR NZ,e */ int8_t e = int8_t(memRead(pc++)); if (!(f & FLAG_Z)) { pc = uint16_t(pc + e); t = 12; } else { t = 7; } break; }
		case 0x28: { /* JR Z,e */ int8_t e = int8_t(memRead(pc++)); if (f & FLAG_Z) { pc = uint16_t(pc + e); t = 12; } else { t = 7; } break; }
		case 0x30: { /* JR NC,e */ int8_t e = int8_t(memRead(pc++)); if (!(f & FLAG_C)) { pc = uint16_t(pc + e); t = 12; } else { t = 7; } break; }
		case 0x38: { /* JR C,e */ int8_t e = int8_t(memRead(pc++)); if (f & FLAG_C) { pc = uint16_t(pc + e); t = 12; } else { t = 7; } break; }
		case 0xF3: /* DI */ iff1 = false; iff2 = false; t = 4; break;
		case 0xFB: /* EI */ iff1 = true; iff2 = true; t = 4; break;
		case 0x76: /* HALT */ halted = true; t = 4; break;
		case 0xAF: { /* XOR A */ a ^= a; f = FLAG_Z | FLAG_PV; t = 4; break; }
		/* removed placeholder duplicate conflicting with OR A (0xB7) */
		case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87: {
			// ADD A, r/(HL)
			auto getSrc = [&](int idx)->uint8_t{
				switch(idx){
					case 0: return b; case 1: return c; case 2: return d; case 3: return e; case 4: return h; case 5: return l; case 6: return memRead(hl()); case 7: return a;
				}
				return 0; };
			int idx = op & 0x07; uint8_t s = getSrc(idx);
			uint16_t res = uint16_t(a) + s;
			uint8_t aflags = 0;
			if (res & 0x100) aflags |= FLAG_C;
			if (((a ^ s ^ res) & 0x10)) aflags |= FLAG_H;
			uint8_t r8 = uint8_t(res & 0xFF);
			uint8_t pv = (~(a ^ s) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; // overflow
			f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags;
			a = r8; t = (idx==6)?7:4; break; }
		case 0xC6: { /* ADD A,n */ uint8_t n = memRead(pc++); uint16_t res = uint16_t(a) + n; uint8_t aflags = 0; if (res & 0x100) aflags |= FLAG_C; if (((a ^ n ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res); uint8_t pv = (~(a ^ n) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = 7; break; }
		case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
			// SUB A, r/(HL)
			auto getSrc = [&](int idx)->uint8_t{
				switch(idx){ case 0: return b; case 1: return c; case 2: return d; case 3: return e; case 4: return h; case 5: return l; case 6: return memRead(hl()); case 7: return a; } return 0; };
			int idx = op & 0x07; uint8_t s = getSrc(idx);
			uint16_t res = uint16_t(a) - s;
			uint8_t aflags = FLAG_N;
			if (res & 0x100) aflags |= FLAG_C;
			if (((a ^ s ^ res) & 0x10)) aflags |= FLAG_H;
			uint8_t r8 = uint8_t(res);
			uint8_t pv = ((a ^ s) & (a ^ r8)) & 0x80 ? FLAG_PV : 0;
			f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags;
			a = r8; t = (idx==6)?7:4; break; }
		case 0xD6: { /* SUB n */ uint8_t n = memRead(pc++); uint16_t res = uint16_t(a) - n; uint8_t aflags = FLAG_N; if (res & 0x100) aflags |= FLAG_C; if (((a ^ n ^ res) & 0x10)) aflags |= FLAG_H; uint8_t r8 = uint8_t(res); uint8_t pv = ((a ^ n) & (a ^ r8)) & 0x80 ? FLAG_PV : 0; f = (szp_table(r8) & (FLAG_S|FLAG_Z)) | (r8 & (FLAG_X|FLAG_Y)) | pv | aflags; a = r8; t = 7; break; }
		case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C: {
			// INC r
			uint8_t *reg = nullptr; switch((op>>3)&7){ case 0: reg=&b; break; case 1: reg=&c; break; case 2: reg=&d; break; case 3: reg=&e; break; case 4: reg=&h; break; case 5: reg=&l; break; case 7: reg=&a; break; default: break; }
			if (reg) {
				uint8_t res = uint8_t(*reg + 1);
				uint8_t nf = (f & FLAG_C) | (res & (FLAG_X|FLAG_Y));
				if ((res & 0x0F) == 0x00) nf |= FLAG_H;
				if (res == 0x80) nf |= FLAG_PV;
				if (res == 0x00) nf |= FLAG_Z;
				if (res & 0x80) nf |= FLAG_S;
				f = nf;
				*reg = res; t = 4;
			} else {
				// INC (HL)
				uint8_t v = memRead(hl());
				uint8_t res = uint8_t(v + 1);
				uint8_t nf = (f & FLAG_C) | (res & (FLAG_X|FLAG_Y));
				if ((res & 0x0F) == 0x00) nf |= FLAG_H;
				if (res == 0x80) nf |= FLAG_PV;
				if (res == 0x00) nf |= FLAG_Z;
				if (res & 0x80) nf |= FLAG_S;
				f = nf; memWrite(hl(), res); t = 11;
			}
			break; }
		case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D: {
			// DEC r
			uint8_t *reg = nullptr; switch((op>>3)&7){ case 0: reg=&b; break; case 1: reg=&c; break; case 2: reg=&d; break; case 3: reg=&e; break; case 4: reg=&h; break; case 5: reg=&l; break; case 7: reg=&a; break; default: break; }
			if (reg) {
				uint8_t res = uint8_t(*reg - 1);
				uint8_t nf = (f & FLAG_C) | FLAG_N | (res & (FLAG_X|FLAG_Y));
				if ((res & 0x0F) == 0x0F) nf |= FLAG_H;
				if (res == 0x7F) nf |= FLAG_PV;
				if (res == 0x00) nf |= FLAG_Z;
				if (res & 0x80) nf |= FLAG_S;
				f = nf; *reg = res; t = 4;
			} else {
				uint8_t v = memRead(hl()); uint8_t res = uint8_t(v - 1);
				uint8_t nf = (f & FLAG_C) | FLAG_N | (res & (FLAG_X|FLAG_Y));
				if ((res & 0x0F) == 0x0F) nf |= FLAG_H;
				if (res == 0x7F) nf |= FLAG_PV;
				if (res == 0x00) nf |= FLAG_Z;
				if (res & 0x80) nf |= FLAG_S;
				f = nf; memWrite(hl(), res); t = 11;
			}
			break; }
		case 0x7F: case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: {
			// LD A, r/(HL)
			uint8_t v = 0; switch(op & 7){ case 7: v=a; break; case 0: v=b; break; case 1: v=c; break; case 2: v=d; break; case 3: v=e; break; case 4: v=h; break; case 5: v=l; break; case 6: v=memRead(hl()); break; }
			a = v; t = ((op & 7)==6)?7:4; break; }
		case 0x47: case 0x4F: case 0x57: case 0x5F: case 0x67: case 0x6F: { // LD r, A
			uint8_t *reg = nullptr; switch((op-0x47)/8){ case 0: reg=&b; break; case 1: reg=&c; break; case 2: reg=&d; break; case 3: reg=&e; break; case 4: reg=&h; break; case 5: reg=&l; break; }
			if (reg) *reg = a; t = 4; break; }
		case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: /* LD (HL),r */ {
			uint8_t v = 0; switch(op & 7){ case 0: v=b; break; case 1: v=c; break; case 2: v=d; break; case 3: v=e; break; case 4: v=h; break; case 5: v=l; break; }
			memWrite(hl(), v); t = 7; break; }
		case 0x36: { /* LD (HL),n */ uint8_t n = memRead(pc++); memWrite(hl(), n); t = 10; break; }
		case 0xE6: { /* AND n */ uint8_t n = memRead(pc++); a = a & n; f = FLAG_H | (a & (FLAG_S|FLAG_Z|FLAG_PV|FLAG_X|FLAG_Y)); if (a==0) f |= FLAG_Z; {int bits=0; for(int b=0;b<8;++b) if(a&(1<<b))++bits; if((bits%2)==0) f|=FLAG_PV;} t = 7; break; }
		case 0xB7: { /* OR A */ f = 0; int bits=0; for(int b=0;b<8;++b) if(a&(1<<b))++bits; if((bits%2)==0) f|=FLAG_PV; if(a==0) f|=FLAG_Z; if(a&0x80) f|=FLAG_S; f |= (a & (FLAG_X|FLAG_Y)); t=4; break; }
		case 0xC5: { /* PUSH BC */ uint16_t sp2 = sp - 1; memWrite(sp2, b); sp = sp2; sp2 = sp - 1; memWrite(sp2, c); sp = sp2; t = 11; break; }
		case 0xD5: { /* PUSH DE */ uint16_t sp2 = sp - 1; memWrite(sp2, d); sp = sp2; sp2 = sp - 1; memWrite(sp2, e); sp = sp2; t = 11; break; }
		case 0xE5: { /* PUSH HL */ uint16_t sp2 = sp - 1; memWrite(sp2, h); sp = sp2; sp2 = sp - 1; memWrite(sp2, l); sp = sp2; t = 11; break; }
		case 0xF5: { /* PUSH AF */ uint16_t sp2 = sp - 1; memWrite(sp2, a); sp = sp2; sp2 = sp - 1; memWrite(sp2, f); sp = sp2; t = 11; break; }
		case 0xC1: { /* POP BC */ uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); set_bc(make16(hi,lo)); t = 10; break; }
		case 0xD1: { /* POP DE */ uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); set_de(make16(hi,lo)); t = 10; break; }
		case 0xE1: { /* POP HL */ uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); set_hl(make16(hi,lo)); t = 10; break; }
		case 0xF1: { /* POP AF */ uint8_t lo = memRead(sp++); uint8_t hi = memRead(sp++); set_af(make16(hi,lo)); t = 10; break; }
		case 0x2C + 0x40: break; // placeholder
		case 0xED: {
			uint8_t op2 = memRead(pc++);
			switch (op2) {
				case 0x47: /* LD I,A */ i = a; t = 9; break;
				case 0x4F: /* LD R,A */ r = (r & 0x80) | (a & 0x7F); t = 9; break;
				case 0x57: /* LD A,I */ a = i; f = (f & FLAG_C) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)) ; t = 9; break;
				case 0x5F: /* LD A,R */ a = (r & 0x7F) | (r & 0x80); f = (f & FLAG_C) | (szp_table(a) & (FLAG_S|FLAG_Z|FLAG_PV)); t = 9; break;
				case 0xB0: /* LDIR */ {
					uint16_t count = bc();
					if (count == 0) { f = (f & (FLAG_C|FLAG_Z|FLAG_S|FLAG_PV|FLAG_H|FLAG_N|FLAG_X|FLAG_Y)) & ~FLAG_PV; t = 16; break; }
					uint8_t v = memRead(hl()); memWrite(de(), v); set_hl(hl()+1); set_de(de()+1); set_bc(count-1);
					f = (f & (FLAG_C|FLAG_Z|FLAG_S|FLAG_H|FLAG_N)) | ((bc()!=0)?FLAG_PV:0);
					t = 21; if (bc()!=0) { pc -= 2; } // repeat by re-executing this instruction
					break; }
				default:
					// Treat unknown ED-prefixed as NOP timing 8
					t = 8; break;
			}
			break; }
		case 0xCB: {
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
			break; }
		default:
			// Unimplemented: treat as NOP
			t = 4; break;
	}
	tstates = t;
	return tstates;
}