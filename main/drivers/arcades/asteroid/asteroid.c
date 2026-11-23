// asteroid.c - Atari's Asteroids (1979) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/asteroid and video/dvg (after
// dsp-emulator's asteroids_hw.pas / avg_dvg.pas and MAME's avgdvg.cpp). A
// 6502 at 1.512 MHz with an NMI every 6144 cycles, the Digital Vector
// Generator's state machine run to its halt on GO, its vectors drawn on a
// 400x320 picture that fades to 60% a frame, and dsp-cpp's square-wave and
// noise stand-ins for the discrete sound.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6502/m65xx.h"

#define W 400
#define H 320
#define CPU_CLOCK 1512000
#define SCANLINES 300
#define FPS (12096000.0 / 4096.0 / 12.0 / 4.0)
#define NMI_PERIOD 6144
#define RATE 44100
#define MAX_VECT 10000

static const char *const kSets[] = { "asteroid", NULL };
const arc_info arc_game = { "asteroid", "Asteroids", "Atari", 1979, W, H, FPS, RATE, kSets,
                            { "FUEGO", "HIPERESPACIO", "EMPUJE", NULL }, ARC_JOY_2WAY };

static const arc_rom kProgram[] = {
    {"035145-04e.ef2", 0x0800, 0x6800, 0xb503eaf7},
    {"035144-04e.h2", 0x0800, 0x7000, 0x25233192},
    {"035143-02.j2", 0x0800, 0x7800, 0x312caa02},
    {"035127-02.np3", 0x0800, 0x5000, 0x8b71fd9e},
};
static const arc_rom kDvgProm[] = {{"034602-01.c8", 0x0100, 0, 0x97953db8}};

typedef struct { int16_t x, y; uint8_t i; } vect_t;

static struct state {
    m65xx cpu;
    uint8_t mem[0x8000], ram[2][0x100], bank, in0, in1, dsw;
    uint64_t total, next_nmi;
    double leftover;
    // the DVG
    uint8_t prom[0x100];
    uint16_t pc, dvx, dvy, stack[4];
    uint8_t sp, data, latch, scale, intensity, op, halt, sync_halt;
    int xpos, ypos, nvect, nshow;
    vect_t vect[MAX_VECT];
    // the sound
    uint8_t explode, thump;
    bool bit[8];
    uint32_t lfsr, phase[8];
    int explode_remain, shot_remain;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
    uint8_t lum[W * H];
    uint16_t fb[W * H];
} *A_;
#define A (*A_)

// ---------------------------------------------------------------------------
// The DVG

static uint8_t state_addr(void)
{
    uint8_t a = (uint8_t)(((((A.latch >> 4) ^ 1) & 1) << 7) | (A.latch & 0x0f));
    if ((A.op >> 3) & 1) a |= (uint8_t)((A.op & 7) << 4);
    return a;
}

static void draw_to(int x, int y, int i)
{
    if (((x | y) & 0x400) == 0 && A.nvect < MAX_VECT) A.vect[A.nvect++] = (vect_t){(int16_t)x, (int16_t)y, (uint8_t)i};
}

static void latch3(void)
{
    A.dvx = (uint16_t)((A.dvx & 0xff) | ((A.data & 0x0f) << 8));
    A.intensity = A.data >> 4;
}

static void gostrobe(void)
{
    int scale;
    if (A.op == 0x0f) {
        scale = (A.scale + (((A.dvy & 0x800) >> 11) | (((A.dvx & 0x800) ^ 0x800) >> 10) | ((A.dvx & 0x800) >> 9))) & 0x0f;
        A.dvy &= 0x0f00;
        A.dvx &= 0x0f00;
    } else scale = (A.scale + A.op) & 0x0f;
    int fin = 0x0fff - (((2 << scale) & 0x07ff) ^ 0x0fff);
    const int dx = (A.dvx & 0x400) ? -1 : 1, dy = (A.dvy & 0x400) ? -1 : 1;
    const int mx = (A.dvx << 2) & 0x0fff, my = (A.dvy << 2) & 0x0fff;
    int c = 0;
    while (fin--) {
        int countx = 0, county = 0;
        for (int bit = 0; bit < 12; bit++)
            if ((c & ((1 << (bit + 1)) - 1)) == ((1 << bit) - 1)) {
                if (mx & (1 << (11 - bit))) countx = 1;
                if (my & (1 << (11 - bit))) county = 1;
            }
        c = (c + 1) & 0x0fff;
        if (countx) {
            if (!(A.ypos & 0x400) && ((A.xpos ^ (A.xpos + dx)) & 0x400)) {
                if ((A.xpos + dx) & 0x400) draw_to(A.xpos, A.ypos, A.intensity);
                else draw_to((A.xpos + dx) & 0x0fff, A.ypos, 0);
            }
            A.xpos = (A.xpos + dx) & 0x0fff;
        }
        if (county) {
            if (!(A.xpos & 0x400) && ((A.ypos ^ (A.ypos + dy)) & 0x400)) {
                if ((A.ypos + dy) & 0x400) draw_to(A.xpos, A.ypos, A.intensity);
                else draw_to(A.xpos, (A.ypos + dy) & 0x0fff, 0);
            }
            A.ypos = (A.ypos + dy) & 0x0fff;
        }
    }
    draw_to(A.xpos, A.ypos, A.intensity);
}

static void dvg_go(void)
{
    A.dvy = 0;
    A.op = 0;
    A.nvect = 0;
    A.halt = 0;
    A.sync_halt = 0;
    int extra = 0;
    for (int step = 0; step < 200000; step++) {
        A.latch = (uint8_t)((A.latch & 0x10) | (A.prom[state_addr()] & 0x0f));
        if (A.latch & 0x08) {
            A.data = A.mem[(0x4000 + (A.pc << 1) + (A.latch & 1)) & 0x7fff];
            const bool op0 = A.op & 1;
            switch (A.latch & 7) {
            case 0:                                     // dmapush
                if (!op0) {
                    A.sp = (A.sp + 1) & 0x0f;
                    A.stack[A.sp & 3] = A.pc;
                }
                break;
            case 1:                                     // dmald
                if (op0) {
                    A.pc = A.stack[A.sp & 3];
                    A.sp = (A.sp - 1) & 0x0f;
                } else A.pc = A.dvy;
                break;
            case 2: gostrobe(); break;
            case 3:                                     // haltstrobe
                A.halt = op0;
                if (!op0) {
                    A.xpos = A.dvx & 0x0fff;
                    A.ypos = A.dvy & 0x0fff;
                    draw_to(A.xpos, A.ypos, 0);
                }
                break;
            case 4:                                     // latch0
                A.dvy &= 0x0f00;
                if (A.op == 0x0f) latch3();
                else A.dvy = (uint16_t)((A.dvy & 0x0f00) | A.data);
                A.pc++;
                break;
            case 5:                                     // latch1
                A.dvy = (uint16_t)((A.dvy & 0xff) | ((A.data & 0x0f) << 8));
                A.op = A.data >> 4;
                if (A.op == 0x0f) {
                    A.dvx &= 0x0f00;
                    A.dvy &= 0x0f00;
                }
                break;
            case 6:                                     // latch2
                A.dvx &= 0x0f00;
                if (A.op != 0x0f) A.dvx = (uint16_t)((A.dvx & 0x0f00) | A.data);
                if (((A.op >> 1) & 1) && ((A.op >> 3) & 1)) A.scale = A.intensity;
                A.pc++;
                break;
            default: latch3(); break;
            }
        }
        if (A.halt && (A.latch & 0x10) == 0) A.sync_halt = 1;
        A.latch = (uint8_t)((A.halt << 4) | (A.latch & 0x0f));
        if (A.sync_halt && ++extra > 24) break;
    }
    A.nshow = A.nvect;                              // (the vectors stay for the frame's picture)
}

static int map_x(int x) { return x > 1024 ? 400 : x < 0 ? 0 : (int)(x / 2.56); }
static int map_y(int y) { return y > 1024 ? 400 : y < 0 ? 0 : (int)((1024 - y) / 2.56); }

// ---------------------------------------------------------------------------
// The picture: white lines with a glow, kept as one brightness a pixel

static inline void plot(int x, int y, int i)
{
    if (x >= 0 && x < W && y >= 0 && y < H && i > 0) {
        if (i > 255) i = 255;
        uint8_t *p = &A.lum[y * W + x];
        if (i > *p) *p = (uint8_t)i;
    }
}

static void draw_line(int x0, int y0, int x1, int y1, int intensity)
{
    if (intensity <= 0) return;
    int level = intensity * 17;
    if (level > 255) level = 255;
    const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, glow = level / 3;
    int err = dx + dy, x = x0, y = y0;
    for (;;) {
        plot(x, y, level);
        if (glow > 0) {
            plot(x - 1, y, glow);
            plot(x + 1, y, glow);
            plot(x, y - 1, glow);
            plot(x, y + 1, glow);
        }
        if (x == x1 && y == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

static void update_video(void)
{
    for (int i = 0; i < W * H; i++) A.lum[i] = (uint8_t)(A.lum[i] * 6 / 10);
    if (A.nshow > 0) {
        int xs = A.vect[0].x, ys = A.vect[0].y;
        for (int i = 0; i < A.nshow; i++) {
            const int xe = A.vect[i].x, ye = A.vect[i].y;
            if (A.vect[i].i > 0) draw_line(map_x(xs), map_y(ys) - 40, map_x(xe), map_y(ye) - 40, A.vect[i].i);
            xs = xe;
            ys = ye;
        }
    }
    for (int i = 0; i < W * H; i++) {
        const unsigned v = A.lum[i];
        A.fb[i] = (uint16_t)(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
    }
}

// ---------------------------------------------------------------------------
// The sound (dsp-cpp's stand-ins), a sample per 1512000 / 44100 cycles

static void mix_audio(int cycles)
{
    A.audio_acc += (int64_t)cycles * RATE;
    while (A.audio_acc >= CPU_CLOCK) {
        A.audio_acc -= CPU_CLOCK;
        const uint32_t b = (A.lfsr ^ (A.lfsr >> 1) ^ (A.lfsr >> 4) ^ (A.lfsr >> 15)) & 1u;
        A.lfsr = (A.lfsr >> 1) | (b << 15);
        int s = 0;
        if (A.bit[3]) s += (int)(A.lfsr & 1) * 5000 - 2500;
        if (A.explode_remain > 0) {
            s += ((int)(A.lfsr & 0xff) - 128) * ((int)((A.explode >> 2) & 0x0f) + 4) * 6;
            A.explode_remain--;
        }
        if (A.thump & 0x10) {
            A.phase[6] += (uint32_t)(40 + ((A.thump & 0x0f) + 1) * 12);
            s += (A.phase[6] & 0x8000) ? 4500 : -4500;
        }
        if (A.bit[0]) {
            A.phase[0] += (uint32_t)((A.bit[2] ? 737 : 441) * 65536.0 / RATE);
            s += (A.phase[0] & 0x8000) ? 2800 : -2800;
        }
        if (A.bit[1]) {
            A.phase[1] += (uint32_t)(2800 * 65536.0 / RATE);
            s += (A.phase[1] & 0x8000) ? 2200 : -2200;
        }
        if (A.shot_remain > 0) {
            s += ((int)(A.lfsr & 1) * 2 - 1) * 5500;
            A.shot_remain--;
        }
        if (A.bit[5]) {
            A.phase[5] += (uint32_t)(1500 * 65536.0 / RATE);
            s += (A.phase[5] & 0x8000) ? 3500 : -3500;
        }
        if (A.naudio < 1024) A.audio[A.naudio++] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}

static void on_cycles(void *u, int cycles)
{
    (void)u;
    if (cycles <= 0) return;
    A.total += (uint64_t)cycles;
    while (A.total >= A.next_nmi) {
        m65xx_set_nmi(&A.cpu, M65_PULSE);
        A.next_nmi += NMI_PERIOD;
    }
    mix_audio(cycles);
}

// ---------------------------------------------------------------------------
// Memory

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    a &= 0x7fff;
    if (a <= 0x01ff || (a >= 0x4000 && a <= 0x47ff) || (a >= 0x5000 && a <= 0x57ff) || a >= 0x6800) return A.mem[a];
    if (a <= 0x02ff) return A.ram[A.bank][a & 0xff];
    if (a <= 0x03ff) return A.ram[1 - A.bank][a & 0xff];
    if (a >= 0x2000 && a <= 0x2007) {
        if (a == 0x2001) return (A.total & 0x100) ? 0x80 : 0x7f;   // the 3 kHz clock
        if (a == 0x2002) return A.sync_halt ? 0x7f : 0x80;        // the DVG's halt
        return (A.in0 & (1u << (a & 7))) ? 0x80 : 0x7f;
    }
    if (a >= 0x2400 && a <= 0x2407) return (A.in1 & (1u << (a & 7))) ? 0x80 : 0x7f;
    if (a >= 0x2800 && a <= 0x2803) return (uint8_t)(0xfc | ((A.dsw >> (6 - 2 * (a & 3))) & 3));
    return 0;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    a &= 0x7fff;
    if (a <= 0x01ff || (a >= 0x4000 && a <= 0x47ff)) A.mem[a] = v;
    else if (a <= 0x02ff) A.ram[A.bank][a & 0xff] = v;
    else if (a <= 0x03ff) A.ram[1 - A.bank][a & 0xff] = v;
    else if (a == 0x3000) dvg_go();
    else if (a == 0x3200) A.bank = (v & 4) >> 2;
    else if (a == 0x3600) {
        A.explode = v;
        A.explode_remain = RATE / 3;
    } else if (a == 0x3a00) A.thump = v;
    else if (a >= 0x3c00 && a <= 0x3c05) {
        const int bit = a & 7;
        A.bit[bit] = (v & 0x80) || (v & 0x01);
        if (bit == 4 && A.bit[4]) A.shot_remain = RATE / 18;
    }
}

bool arc_init(void)
{
    if (!(A_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    if (!ARC_LOAD(kProgram, A.mem, sizeof A.mem) || !ARC_LOAD(kDvgProm, A.prom, sizeof A.prom)) return false;
    A.dsw = 0x84;
    A.cpu.read = rd;
    A.cpu.write = wr;
    A.cpu.cycles = on_cycles;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(A.mem, 0, 0x5000);
    memset(A.ram, 0, sizeof A.ram);
    A.bank = A.in0 = A.in1 = 0;
    A.leftover = 0;
    A.total = 0;
    A.next_nmi = NMI_PERIOD;
    A.explode = A.thump = 0;
    memset(A.bit, 0, sizeof A.bit);
    A.lfsr = 1;
    memset(A.phase, 0, sizeof A.phase);
    A.explode_remain = A.shot_remain = 0;
    A.audio_acc = 0;
    A.latch = A.op = 0;                             // the DVG's reset
    A.halt = A.sync_halt = 1;
    A.pc = 0;
    A.data = A.sp = 0;
    memset(A.stack, 0, sizeof A.stack);
    A.dvx = A.dvy = 0;
    A.scale = A.intensity = 0;
    A.xpos = A.ypos = 0;
    A.nvect = A.nshow = 0;
    m65xx_reset(&A.cpu);
    memset(A.lum, 0, sizeof A.lum);
    memset(A.fb, 0, sizeof A.fb);
}

int arc_run_frame(const int16_t **audio)
{
    const double per_line = (double)CPU_CLOCK / FPS / (double)SCANLINES;
    A.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        A.leftover += per_line;
        const int run = (int)A.leftover;
        A.leftover -= run;
        if (run > 0) m65xx_run(&A.cpu, run);
    }
    update_video();
    if (audio) *audio = A.audio;
    return A.naudio;
}

const uint16_t *arc_frame(void) { return A.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p = in->p[0];
    A.in0 = A.in1 = 0;
    if (p & AP_B2) A.in0 |= 0x08;                   // hyperspace
    if (p & AP_B1) A.in0 |= 0x10;                   // fire
    if (in->coin1) A.in1 |= 0x01;
    if (in->coin2) A.in1 |= 0x02;
    if (p & AP_START) A.in1 |= 0x08;
    if (in->p[1] & AP_START) A.in1 |= 0x10;
    if (p & (AP_B3 | AP_UP)) A.in1 |= 0x20;         // thrust
    if (p & AP_RIGHT) A.in1 |= 0x40;
    if (p & AP_LEFT) A.in1 |= 0x80;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) A.dsw = v;
}
