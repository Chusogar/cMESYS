// tf_core.c - Track & Field core (port of zxtiny/zxm/trackfld.c and
// trackfld_audio.c, no SDL). Same memory maps, Konami-1 opcode decoding,
// interrupt scheme, sound hardware glue and renderer as the desktop driver.
#include "tf_core.h"
#include "rom_data.h"
#include "mc6809.h"
#include "z80/z80.h"
#include "sn76489.h"
#include "vlm5030.h"
#include <string.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define HOT IRAM_ATTR
#else
#define HOT
#endif

volatile tf_inputs_t tf_in;

// ---------------------------------------------------------------------------
// Machine state (TrackFld / TrackFldAudio)
// ---------------------------------------------------------------------------
typedef struct {
    mc6809__t cpu;
    uint8_t ram[0x1800];
    uint8_t sprram1[0x40];
    uint8_t sprram2[0x40];
    uint8_t scroll[0x20];
    uint8_t scroll2[0x20];
    uint8_t ram1800[0x800];
    uint8_t ram2800[0x400];
    uint8_t nvram[0x400];
    uint8_t videoram[0x800];
    uint8_t colorram[0x800];
    uint8_t sound_latch;
    bool irq_enable;
    bool flipscreen;
    volatile bool fault;

    // sound board
    z80 zcpu;
    uint8_t sram[0x400];
    uint8_t latch;
    uint8_t sn_latch;
    uint32_t cycle_accum;
    uint16_t vlm_last_offset;
    SN76489 sn;
    VLM5030 vlm;
    int16_t dac;
    uint32_t zfrac;             // fractional Z80 cycles per sample (16.16)
    int32_t zover;              // cycles run past the previous sample
} tf_t;

static tf_t *T;
// Program ROM (0x6000-0xFFFF): copied to RAM on ESP32
static const uint8_t *mrom = tf_main_rom;

// ---------------------------------------------------------------------------
// Main CPU (Konami-1 = 6809 with encrypted opcodes)
// ---------------------------------------------------------------------------
static inline uint8_t konami1_decode(uint8_t v, uint16_t a)
{
    return v ^ ((a & 0x0002) ? 0x80 : 0x20) ^ ((a & 0x0008) ? 0x08 : 0x02);
}

static mc6809byte__t HOT cpu_read(mc6809__t *cpu, mc6809addr__t a, bool ifetch)
{
    tf_t *t = T;
    if (a >= 0x6000) {
        uint8_t v = mrom[a - 0x6000];
        if (ifetch && a == cpu->instpc)
            return konami1_decode(v, a);
        if (ifetch && a == (uint16_t)(cpu->instpc + 1) && cpu->instpc >= 0x6000) {
            uint8_t p = konami1_decode(mrom[cpu->instpc - 0x6000], cpu->instpc);
            if (p == 0x10 || p == 0x11)
                return konami1_decode(v, a);
        }
        return v;
    }
    switch (a) {
    case 0x1200: return tf_in.dsw2;
    case 0x1280: return tf_in.in0;
    case 0x1281: return tf_in.in1;
    case 0x1282: return tf_in.in2;
    case 0x1283: return tf_in.dsw1;
    default: break;
    }
    if (a <= 0x17ff) return t->ram[a];
    if (a >= 0x1800 && a <= 0x183f) return t->sprram2[a - 0x1800];
    if (a >= 0x1840 && a <= 0x185f) return t->scroll[a - 0x1840];
    if (a >= 0x1860 && a <= 0x1bff) return t->ram1800[a - 0x1800];
    if (a >= 0x1c00 && a <= 0x1c3f) return t->sprram1[a - 0x1c00];
    if (a >= 0x1c40 && a <= 0x1c5f) return t->scroll2[a - 0x1c40];
    if (a >= 0x1c60 && a <= 0x1fff) return t->ram1800[a - 0x1800];
    if (a >= 0x2800 && a <= 0x2bff) return t->ram2800[a - 0x2800];
    if (a >= 0x2c00 && a <= 0x2fff) return t->nvram[a - 0x2c00];
    if (a >= 0x3000 && a <= 0x37ff) return t->videoram[a - 0x3000];
    if (a >= 0x3800 && a <= 0x3fff) return t->colorram[a - 0x3800];
    return 0xff;
}

static void HOT cpu_write(mc6809__t *cpu, mc6809addr__t a, mc6809byte__t v)
{
    tf_t *t = T;
    (void)cpu;
    switch (a) {
    case 0x1000: return;
    case 0x1080: t->flipscreen = v != 0; return;
    case 0x1081: z80_pulse_irq(&t->zcpu, 0xff); return;
    case 0x1083:
    case 0x1084: return;
    case 0x1087: t->irq_enable = (v & 1) != 0; return;
    case 0x1100: t->sound_latch = v; t->latch = v; return;
    default: break;
    }
    if (a <= 0x17ff) { t->ram[a] = v; return; }
    if (a >= 0x1800 && a <= 0x183f) { t->ram1800[a - 0x1800] = v; t->sprram2[a - 0x1800] = v; return; }
    if (a >= 0x1840 && a <= 0x185f) { t->ram1800[a - 0x1800] = v; t->scroll[a - 0x1840] = v; return; }
    if (a >= 0x1860 && a <= 0x1bff) { t->ram1800[a - 0x1800] = v; return; }
    if (a >= 0x1c00 && a <= 0x1c3f) { t->ram1800[a - 0x1800] = v; t->sprram1[a - 0x1c00] = v; return; }
    if (a >= 0x1c40 && a <= 0x1c5f) { t->ram1800[a - 0x1800] = v; t->scroll2[a - 0x1c40] = v; return; }
    if (a >= 0x1c60 && a <= 0x1fff) { t->ram1800[a - 0x1800] = v; return; }
    if (a >= 0x2800 && a <= 0x2bff) { t->ram2800[a - 0x2800] = v; return; }
    if (a >= 0x2c00 && a <= 0x2fff) { t->nvram[a - 0x2c00] = v; return; }
    if (a >= 0x3000 && a <= 0x37ff) { t->videoram[a - 0x3000] = v; return; }
    if (a >= 0x3800 && a <= 0x3fff) { t->colorram[a - 0x3800] = v; return; }
}

// The desktop version longjmp()s out of mc6809_step() and quits; here the
// core is built without setjmp (too slow per instruction on Xtensa), so a
// fault is flagged and the frame loop resets the machine.
static void cpu_fault(mc6809__t *cpu, mc6809fault__t fault)
{
    (void)cpu; (void)fault;
    T->fault = true;
}

// ---------------------------------------------------------------------------
// Sound board (identical to trackfld_audio.c)
// ---------------------------------------------------------------------------
static uint8_t HOT snd_read(void *ud, uint16_t addr)
{
    tf_t *a = ud;
    if (addr < 0x4000) return tf_audio_rom[addr & (0x4000 - 1)];
    if (addr >= 0x4000 && addr <= 0x43ff) return a->sram[addr & 0x03ff];
    if (addr == 0x6000) return a->latch;
    if (addr == 0x8000) return (uint8_t)((a->cycle_accum >> 10) & 0x0f);
    if (addr == 0xe002) return vlm5030_busy(&a->vlm) ? 0x10 : 0x00;
    return 0xff;
}

static void HOT snd_write(void *ud, uint16_t addr, uint8_t data)
{
    tf_t *a = ud;
    if (addr >= 0x4000 && addr <= 0x43ff) { a->sram[addr & 0x03ff] = data; return; }
    if (addr == 0xa000) { a->sn_latch = data; return; }
    if (addr == 0xc000) { sn76489_write(&a->sn, a->sn_latch); return; }
    if (addr == 0xe000) { a->dac = (int16_t)(((int)data - 128) << 7); return; }
    if (addr == 0xe004) { vlm5030_write_data(&a->vlm, data); return; }
    if (addr >= 0xe000 && addr <= 0xefff) {
        uint16_t offset = addr - 0xe000;
        if ((offset & 0x07) == 0x03) {
            uint16_t changes = offset ^ a->vlm_last_offset;
            uint8_t st = (offset & 0x100) ? 1 : 0;
            uint8_t rst = (offset & 0x200) ? 1 : 0;
            uint8_t ctrl = st | (rst << 1);
            if (changes & (0x100 | 0x200))
                vlm5030_write_ctrl(&a->vlm, ctrl);
        }
        a->vlm_last_offset = offset;
        return;
    }
}

static uint8_t snd_port_in(z80 *cpu, uint16_t port) { (void)cpu; (void)port; return 0xff; }
static void snd_port_out(z80 *cpu, uint16_t port, uint8_t data) { (void)cpu; (void)port; (void)data; }

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void tf_init(void)
{
    if (!T) {
        T = calloc(1, sizeof(tf_t));
#ifdef ESP_PLATFORM
        uint8_t *r = malloc(sizeof tf_main_rom);
        if (r) { memcpy(r, tf_main_rom, sizeof tf_main_rom); mrom = r; }
#endif
    }
    tf_in.in0 = tf_in.in1 = tf_in.in2 = 0xff;
    tf_in.dsw1 = tf_in.dsw2 = 0xff;
    tf_reset();
}

void tf_reset(void)
{
    tf_t *t = T;
    memset(t, 0, sizeof *t);
    t->cpu.user = t;
    t->cpu.read = cpu_read;
    t->cpu.write = cpu_write;
    t->cpu.fault = cpu_fault;
    // sound board (trackfld_audio_init + reset)
    z80_init(&t->zcpu);
    t->zcpu.read_byte = snd_read;
    t->zcpu.write_byte = snd_write;
    t->zcpu.port_in = snd_port_in;
    t->zcpu.port_out = snd_port_out;
    t->zcpu.userdata = t;
    sn76489_init(&t->sn, TF_SN_CLOCK, TF_AUDIO_RATE);
    sn76489_reset(&t->sn);
    vlm5030_init(&t->vlm, tf_vlm_rom, 0x4000);
    z80_reset(&t->zcpu);
    z80_set_pc(&t->zcpu, 0x0000);
    mc6809_reset(&t->cpu);
}

static inline bool idle_6160_spins(tf_t *t)
{
    static const uint8_t pat[7] = { 0xbc, 0x1a, 0x6e, 0x84, 0x6a, 0x07, 0xf9 };   // encrypted bytes
    for (int i = 0; i < 7; i++) if (mrom[0x160 + i] != pat[i]) return false;
    mc6809__t *c = &t->cpu;
    if (c->cwai || c->sync || c->irq || c->firq || c->nmi) return false;
    uint16_t ea = (uint16_t)((c->dp << 8) | 0x1a);
    uint16_t x = (uint16_t)((cpu_read(c, ea, false) << 8) | cpu_read(c, (uint16_t)(ea + 1), false));
    bool ram = x < 0x1000 || (x >= 0x1800 && x < 0x2000) || (x >= 0x2800 && x < 0x4000);
    if (!ram) return false;                                  // only plain RAM (no I/O)
    return (cpu_read(c, x, false) & 0x80) != 0;
}

bool HOT tf_run_frame(int16_t *audio)
{
    tf_t *t = T;
    const int per_slice = (TF_CPU_CLOCK / TF_FPS) / 16;

    for (int s = 0; s < 16; ++s) {
        int ran = 0;
        while (ran < per_slice) {
            // Idle skip: "6160: LDX <$1A / LDD ,X / ASLA / BCS 6160" (5+5+2+3 =
            // 15 cycles) spins while bit 7 of the flag is set; only the vblank
            // IRQ (end of frame) changes it. Whole passes that fit in the slice
            // are skipped with the same cycle count; output is bit-identical.
            if (t->cpu.pc.w == 0x6160 && per_slice - ran >= 15 && idle_6160_spins(t)) {
                t->cpu.cycles += 15;
                ran += 15;
                continue;
            }
            unsigned long before = t->cpu.cycles;
            mc6809_step(&t->cpu);
            unsigned long after = t->cpu.cycles;
            ran += (after > before) ? (int)(after - before) : 1;
        }
        if (t->fault) { tf_reset(); memset(audio, 0, TF_SAMPLES_FRAME * 2); return false; }
    }
    if (t->irq_enable) {
        t->cpu.irq = true;
        mc6809_step(&t->cpu);
        t->cpu.irq = false;
    }

    // Sound CPU for one frame (TF_SOUND_CLOCK / TF_FPS cycles, one z80_step
    // at a time as in trackfld_audio_run), interleaved with sample output so
    // SN76489 / DAC / VLM5030 writes land on the right sample.
    const uint32_t per_sample_fp = (uint32_t)(((uint64_t)(TF_SOUND_CLOCK / TF_FPS) << 16) / TF_SAMPLES_FRAME);
    for (int i = 0; i < TF_SAMPLES_FRAME; i++) {
        t->zfrac += per_sample_fp;
        int32_t want = (int32_t)(t->zfrac >> 16) - t->zover;
        t->zfrac &= 0xffff;
        while (want > 0) {
            // Idle skip: the sound program busy-waits on bit 1 of the 0x8000
            // timer ("LD A,(8000h) / AND 2 / JR NZ|Z,-7", 13+7+12 = 32 T).
            // Whole passes are skipped with the same cycle count, only when the
            // pass fits in this sample's budget; the pass that sees the bit
            // change is executed normally. Output is bit-identical.
            uint16_t pc = t->zcpu.pc;
            if (want >= 32 && (pc == 0x01a9 || pc == 0x01b0) && !t->zcpu.irq_pending && !t->zcpu.nmi_pending &&
                tf_audio_rom[pc] == 0x3a && tf_audio_rom[pc + 1] == 0x00 && tf_audio_rom[pc + 2] == 0x80 &&
                tf_audio_rom[pc + 3] == 0xe6 && tf_audio_rom[pc + 4] == 0x02 &&
                tf_audio_rom[pc + 5] == (pc == 0x01a9 ? 0x20 : 0x28) && tf_audio_rom[pc + 6] == 0xf9) {
                bool loops_while_set = (pc == 0x01a9);
                if ((((t->cycle_accum >> 10) & 2) != 0) == loops_while_set) {
                    t->cycle_accum += 32;
                    want -= 32;
                    continue;
                }
            }
            unsigned c = z80_step(&t->zcpu);
            if (c == 0) c = 1;
            want -= (int32_t)c;
            t->cycle_accum += c;
        }
        t->zover = -want;                         // carry overshoot into next sample
        // trackfld_audio_update(): SN76489 + DAC/2 + VLM5030
        int16_t smp = 0;
        sn76489_update(&t->sn, &smp, 1);
        int v = smp + (t->dac / 2);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        smp = (int16_t)v;
        vlm5030_update(&t->vlm, &smp, 1);
        audio[i] = smp;
    }
    return true;
}

uint16_t tf_dbg_pc(void) { return T->cpu.pc.w; }
unsigned long tf_dbg_cycles(void) { return T->cpu.cycles; }
uint16_t tf_dbg_zpc(void) { return T->zcpu.pc; }
void tf_snapshot(tf_video_t *v)
{
    tf_t *t = T;
    memcpy(v->videoram, t->videoram, sizeof v->videoram);
    memcpy(v->colorram, t->colorram, sizeof v->colorram);
    memcpy(v->sprram1, t->sprram1, sizeof v->sprram1);
    memcpy(v->sprram2, t->sprram2, sizeof v->sprram2);
    memcpy(v->scroll, t->scroll, sizeof v->scroll);
    memcpy(v->scroll2, t->scroll2, sizeof v->scroll2);
    v->flip = t->flipscreen;
}

void tf_palette_rgb(uint8_t rgb[32][3])
{
    for (int i = 0; i < 0x20; ++i) {
        uint8_t v = tf_prom[i];
        rgb[i][0] = (uint8_t)(0x21 * ((v >> 0) & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1));
        rgb[i][1] = (uint8_t)(0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1));
        rgb[i][2] = (uint8_t)(0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1));
    }
}

// ---------------------------------------------------------------------------
// Video (same as video() in trackfld.c, palette indices instead of ARGB)
// ---------------------------------------------------------------------------
static inline void put_px(uint8_t *fb, int x, int y, uint8_t c)
{
    y -= TF_VISIBLE_Y;
    if ((unsigned)x >= TF_W || (unsigned)y >= TF_H) return;
    fb[y * TF_W + x] = c;
}

static void HOT draw_one_sprite(uint8_t *fb, int code, int color, bool flipx, bool flipy, int sx, int sy)
{
    code &= 0xff;
    if (sx <= -16 || sx >= TF_W || sy <= TF_VISIBLE_Y - 16 || sy >= TF_VISIBLE_Y + TF_H) return;
    const uint8_t *spr = &tf_spr_pix[code * 256];
    const uint8_t *lutp = &tf_prom[0x20 + ((color & 0x0f) << 4)];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            uint8_t pen = spr[(flipy ? 15 - y : y) * 16 + (flipx ? 15 - x : x)];
            uint8_t lut = lutp[pen & 0x0f] & 0x0f;
            if (lut == 0) continue;
            put_px(fb, sx + x, sy + y, lut);
        }
}

void HOT tf_render(const tf_video_t *v, uint8_t *fb)
{
    memset(fb, 0, TF_W * TF_H);                 // palette[0]
    // Tiles: 64 x 32, per-row scroll
    for (int row = 0; row < 32; ++row) {
        int sy = row * 8;
        if (sy + 8 <= TF_VISIBLE_Y || sy >= TF_VISIBLE_Y + TF_H) continue;   // fully clipped rows
        int scroll = v->scroll[row] | ((v->scroll2[row] & 1) << 8);
        if (v->flip) scroll = -scroll;
        for (int col = 0; col < 64; ++col) {
            int off = row * 64 + col;
            uint8_t attr = v->colorram[off];
            int code = v->videoram[off] + 4 * (attr & 0xc0);
            int color = attr & 0x0f;
            bool flipx = (attr & 0x10) != 0;
            bool flipy = (attr & 0x20) != 0;
            int sx = (col * 8 - scroll) & 0x1ff;
            if (sx >= 256) sx -= 512;
            if (sx <= -8 || sx >= TF_W) continue;                           // fully clipped
            code %= 768;
            const uint8_t *chr = &tf_chr_pix[code * 64];
            const uint8_t *lut = &tf_prom[0x120 + (color << 4)];
            for (int y = 0; y < 8; ++y) {
                int yy = flipy ? 7 - y : y;
                for (int x = 0; x < 8; ++x) {
                    int xx = flipx ? 7 - x : x;
                    uint8_t pen = chr[yy * 8 + xx];
                    put_px(fb, sx + x, sy + y, (uint8_t)((lut[pen & 0x0f] & 0x0f) + 0x10));
                }
            }
        }
    }
    // Sprites
    for (int offs = 0x3e; offs >= 0; offs -= 2) {
        int sx = v->sprram1[offs] - 1;
        int sy = 240 - v->sprram2[offs + 1];
        int code = v->sprram1[offs + 1];
        int attr = v->sprram2[offs];
        int color = attr & 0x0f;
        bool flipx = ((~attr) & 0x40) != 0;
        bool flipy = (attr & 0x80) != 0;
        if (v->flip) { sy = 240 - sy; flipy = !flipy; }
        sy += 1;
        draw_one_sprite(fb, code, color, flipx, flipy, sx, sy);
        draw_one_sprite(fb, code, color, flipx, flipy, sx - 256, sy);
    }
}
