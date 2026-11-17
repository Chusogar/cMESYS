// md_machine.c - the Sega Mega Drive / Genesis machine (see md_machine.h).
#include <stdlib.h>
#include <string.h>
#include "md_machine.h"
#include "../common/cons.h"
#include "m68k.h"

md_machine *md_p;

bool md_alloc(void)
{
    if (!md_p) md_p = (md_machine *)calloc(1, sizeof *md_p);
    return md_p != NULL;
}

// ---- cartridge formats ----
static bool looks_like_genesis(const uint8_t *d, size_t n)
{
    if (n < 0x110) return false;
    return memcmp(d + 0x100, "SEGA", 4) == 0 || memcmp(d + 0x100, " SEGA", 5) == 0;
}

static void byteswap_words(uint8_t *d, size_t n)
{
    for (size_t i = 0; i + 1 < n; i += 2) {
        const uint8_t t = d[i];
        d[i] = d[i + 1];
        d[i + 1] = t;
    }
}

static bool deinterleave_smd(uint8_t *d, size_t n)
{
    if (n < 0x4000) return true;
    uint8_t *blk = (uint8_t *)malloc(0x4000);
    if (!blk) return false;
    for (size_t b = 0; b + 0x4000 <= n; b += 0x4000) {
        for (int i = 0; i < 0x2000; i++) {
            blk[i * 2] = d[b + 0x2000 + i];
            blk[i * 2 + 1] = d[b + i];
        }
        memcpy(d + b, blk, 0x4000);
    }
    free(blk);
    return true;
}

static void interleave_smd(uint8_t *d, size_t n)    // the inverse (to undo a wrong guess)
{
    if (n < 0x4000) return;
    uint8_t *blk = (uint8_t *)malloc(0x4000);
    if (!blk) return;
    for (size_t b = 0; b + 0x4000 <= n; b += 0x4000) {
        for (int i = 0; i < 0x2000; i++) {
            blk[0x2000 + i] = d[b + i * 2];
            blk[i] = d[b + i * 2 + 1];
        }
        memcpy(d + b, blk, 0x4000);
    }
    free(blk);
}

static bool normalize_cartridge(uint8_t *d, size_t n)
{
    if (looks_like_genesis(d, n)) return true;
    byteswap_words(d, n);
    if (looks_like_genesis(d, n)) return true;
    byteswap_words(d, n);
    deinterleave_smd(d, n);
    if (looks_like_genesis(d, n)) return true;
    byteswap_words(d, n);
    if (looks_like_genesis(d, n)) return true;
    byteswap_words(d, n);
    interleave_smd(d, n);
    return false;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void parse_header(size_t logical)
{
    MD.sram_present = false;
    MD.sram_start = 0x200000;
    MD.sram_end = 0x20ffff;
    if (MD.rom_size > 0x1b8 && MD.rom[0x1b0] == 'R' && MD.rom[0x1b1] == 'A') {
        MD.sram_start = be32(&MD.rom[0x1b4]) & 0xffffff;
        MD.sram_end = be32(&MD.rom[0x1b8]) & 0xffffff;
        if (MD.sram_end < MD.sram_start) MD.sram_end = MD.sram_start;
        MD.sram_present = true;
    } else if (MD.rom_size <= 0x200000) {
        // backup RAM at $200000 without a complete header: a 64 KB window
        MD.sram_present = true;
    }
    (void)logical;
    memset(MD.sram, 0xff, sizeof MD.sram);
    MD.sram_enabled = MD.sram_present;
}

// ---- interrupts ----
static void update_ipl(void) { m68k_set_irq(MD.vint_on ? 6 : MD.hint_on ? 4 : 0); }
static void vdp_hint(void *u, bool on) { (void)u; MD.hint_on = on; update_ipl(); }
static void vdp_vint(void *u, bool on) { (void)u; MD.vint_on = on; update_ipl(); }
static void vdp_z80_irq(void *u, bool on) { (void)u; zd_set_irq(&MD.z80, on ? ZD_HOLD : ZD_CLEAR, 0xff); }
static uint16_t vdp_dma_read(void *u, uint32_t a) { (void)u; return md_read_word(a); }

// ---- the 68000's bus ----
static void check_z80_bus_reset(void)
{
    if (MD.z80_is_reset) {
        zd_reset(&MD.z80);
        ym2612_reset(&MD.ym);
        MD.z80.halted = true;
        return;
    }
    MD.z80.halted = !MD.z80_has_bus;
}

static uint8_t read_pad(int port)
{
    const unsigned b = MD.pad[port - 1];
    const uint8_t ctrl = MD.io_ctrl[port], data = MD.io_data[port];
    const bool th = (ctrl & 0x40) ? (data & 0x40) != 0 : true;
    uint8_t v = 0x3f;
    if (b & CP_UP) v &= (uint8_t)~0x01;
    if (b & CP_DOWN) v &= (uint8_t)~0x02;
    if (th) {
        if (b & CP_LEFT) v &= (uint8_t)~0x04;
        if (b & CP_RIGHT) v &= (uint8_t)~0x08;
        if (b & (CP_B2 | CP_B1)) v &= (uint8_t)~0x10;     // B (A also taps it, as dsp-cpp)
        if (b & CP_B3) v &= (uint8_t)~0x20;               // C
    } else {
        v &= (uint8_t)~0x0c;
        if (b & CP_B1) v &= (uint8_t)~0x10;               // A
        if (b & CP_START) v &= (uint8_t)~0x20;            // START
    }
    const uint8_t pv = (uint8_t)(0x40 | v);
    return (uint8_t)((data & ctrl) | (pv & ~ctrl) | 0x80);
}

static uint8_t read_io(uint32_t a)
{
    const int i = (int)((a >> 1) & 0x0f);
    switch (i) {
    case 0: return MD.version_reg;
    case 1: case 2: return read_pad(i);
    case 3: return (uint8_t)(MD.io_data[3] | 0x7f);
    default: return MD.io_data[i];
    }
}

static void write_io(uint32_t a, uint8_t v)
{
    const int i = (int)((a >> 1) & 0x0f);
    if (i == 0) return;
    if (i <= 3) MD.io_data[i] = v;
    else if (i <= 6) MD.io_ctrl[i - 3] = v;
    else MD.io_data[i] = v;
}

static uint8_t cart_read(uint32_t a)
{
    a &= 0xffffff;
    if (MD.sram_present && MD.sram_enabled && a >= MD.sram_start && a <= MD.sram_end)
        return MD.sram[(a - MD.sram_start) & 0xffff];
    if (!MD.rom) return 0xff;
    uint32_t off = a;
    if (MD.ssf2_mapper) off = MD.rom_banks[(a >> 19) & 7] + (a & 0x7ffff);
    else if (a >= MD.rom_size) off = a & MD.rom_mask;
    if (off >= MD.rom_size) return 0xff;
    return MD.rom[off];
}

static void cart_write(uint32_t a, uint8_t v)
{
    a &= 0xffffff;
    if (a >= 0xa13000 && a <= 0xa130ff) {
        if ((a & 0xff) == 0xf1) { MD.sram_enabled = (v & 1) != 0; return; }
        if (MD.ssf2_mapper && (a & 1) && a >= 0xa130f3) {
            const int slot = (int)((a - 0xa130f3) >> 1) + 1;
            if (slot >= 1 && slot < 8) MD.rom_banks[slot] = (uint32_t)(v & 0x3f) << 19;
        }
        return;
    }
    if (MD.sram_present && MD.sram_enabled && a >= MD.sram_start && a <= MD.sram_end)
        MD.sram[(a - MD.sram_start) & 0xffff] = v;
}

static void vdp_hpos(void)
{
    md_vdp_set_hpos(&MD.vdp, MD.cycles_on_line + (MD.in_cpu ? m68k_cycles_run() : 0));
}

static uint8_t z80_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x4000) return MD.z80_ram[a & 0x1fff];
    if (a < 0x6000) return ym2612_read(&MD.ym);
    if (a < 0x8000) return 0xff;
    return md_read_byte(((uint32_t)MD.z80_bank << 15) | (a & 0x7fff));
}

static void z80_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x4000) { MD.z80_ram[a & 0x1fff] = v; return; }
    if (a < 0x6000) { ym2612_write(&MD.ym, a & 3, v); return; }
    if (a < 0x6100) { MD.z80_bank = (uint16_t)(((MD.z80_bank >> 1) | ((v & 1) << 8)) & 0x1ff); return; }
    if (a >= 0x7f00 && a < 0x7f20) { md_vdp_write_byte(&MD.vdp, (uint8_t)a, v); return; }
    if (a >= 0x8000) md_write_byte(((uint32_t)MD.z80_bank << 15) | (a & 0x7fff), v);
}

static uint8_t z80_in(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void z80_out(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

uint16_t md_read_word(uint32_t a)
{
    a &= 0xfffffe;
    if (MD.ext_read16) {
        uint16_t v;
        if (MD.ext_read16(a, &v)) return v;
    }
    if (a <= 0x3fffff) return (uint16_t)((cart_read(a) << 8) | cart_read(a + 1));
    if (a >= 0xa00000 && a <= 0xa0ffff) {
        if (!MD.z80_has_bus && !MD.z80_is_reset)
            return (uint16_t)((z80_read(NULL, (uint16_t)(a & 0x7fff)) << 8) | z80_read(NULL, (uint16_t)((a + 1) & 0x7fff)));
        return 0xffff;
    }
    if (a >= 0xa10000 && a <= 0xa1001f) {
        const uint8_t v = read_io(a);
        return (uint16_t)((v << 8) | v);
    }
    if (a == 0xa11100) return (uint16_t)(0xfeff | ((MD.z80_has_bus || MD.z80_is_reset) ? 0x0100 : 0));
    if (a >= 0xc00000 && a <= 0xc0001f) {
        vdp_hpos();
        return md_vdp_read(&MD.vdp, (uint8_t)a);
    }
    if (a >= 0xe00000) return (uint16_t)((MD.ram[a & 0xfffe] << 8) | MD.ram[(a & 0xfffe) + 1]);
    return 0xffff;
}

void md_write_word(uint32_t a, uint16_t v)
{
    a &= 0xfffffe;
    if (MD.ext_write16 && MD.ext_write16(a, v)) return;
    if (a <= 0x3fffff) { cart_write(a, (uint8_t)(v >> 8)); cart_write(a + 1, (uint8_t)v); return; }
    if (a >= 0xa00000 && a <= 0xa0ffff) {
        if (!MD.z80_has_bus && !MD.z80_is_reset) {
            z80_write(NULL, (uint16_t)(a & 0x7fff), (uint8_t)(v >> 8));
            z80_write(NULL, (uint16_t)((a + 1) & 0x7fff), (uint8_t)v);
        }
        return;
    }
    if (a >= 0xa10000 && a <= 0xa1001f) { write_io(a, (uint8_t)(v >> 8)); return; }
    if (a == 0xa11100) { MD.z80_has_bus = (v & 0x0100) == 0; check_z80_bus_reset(); return; }
    if (a == 0xa11200) { MD.z80_is_reset = (v & 0x0100) == 0; check_z80_bus_reset(); return; }
    if (a >= 0xa13000 && a <= 0xa130ff) { cart_write(a, (uint8_t)(v >> 8)); cart_write(a + 1, (uint8_t)v); return; }
    if (a >= 0xa14000 && a <= 0xa14fff) return;     // TMSS
    if (a >= 0xc00000 && a <= 0xc0001f) { vdp_hpos(); md_vdp_write(&MD.vdp, (uint8_t)a, v); return; }
    if (a >= 0xe00000) {
        MD.ram[a & 0xfffe] = (uint8_t)(v >> 8);
        MD.ram[(a & 0xfffe) + 1] = (uint8_t)v;
    }
}

uint8_t md_read_byte(uint32_t a)
{
    a &= 0xffffff;
    if (MD.ext_read8) {
        uint8_t v;
        if (MD.ext_read8(a, &v)) return v;
    }
    if (a <= 0x3fffff) return cart_read(a);
    if (a >= 0xa00000 && a <= 0xa0ffff)
        return (!MD.z80_has_bus && !MD.z80_is_reset) ? z80_read(NULL, (uint16_t)(a & 0x7fff)) : 0xff;
    if (a >= 0xa10000 && a <= 0xa1001f) return read_io(a);
    if ((a & 0xfffffe) == 0xa11100) return (MD.z80_has_bus || MD.z80_is_reset) ? 1 : 0;
    if (a >= 0xc00000 && a <= 0xc0001f) {
        vdp_hpos();
        const uint16_t v = md_vdp_read(&MD.vdp, (uint8_t)(a & 0x1e));
        return (a & 1) ? (uint8_t)v : (uint8_t)(v >> 8);
    }
    if (a >= 0xe00000) return MD.ram[a & 0xffff];
    return 0xff;
}

void md_write_byte(uint32_t a, uint8_t v)
{
    a &= 0xffffff;
    if (MD.ext_write8 && MD.ext_write8(a, v)) return;
    if (a <= 0x3fffff) { cart_write(a, v); return; }
    if (a >= 0xa00000 && a <= 0xa0ffff) {
        if (!MD.z80_has_bus && !MD.z80_is_reset) z80_write(NULL, (uint16_t)(a & 0x7fff), v);
        return;
    }
    if (a >= 0xa10000 && a <= 0xa1001f) { write_io(a, v); return; }
    if ((a & 0xfffffe) == 0xa11100) {
        if (!(a & 1)) { MD.z80_has_bus = (v & 1) == 0; check_z80_bus_reset(); }
        return;
    }
    if ((a & 0xfffffe) == 0xa11200) {
        if (!(a & 1)) { MD.z80_is_reset = (v & 1) == 0; check_z80_bus_reset(); }
        return;
    }
    if (a >= 0xa13000 && a <= 0xa130ff) { cart_write(a, v); return; }
    if (a >= 0xc00000 && a <= 0xc0001f) { vdp_hpos(); md_vdp_write_byte(&MD.vdp, (uint8_t)a, v); return; }
    if (a >= 0xe00000) MD.ram[a & 0xffff] = v;
}

// ---- Musashi's callbacks ----
unsigned int cpu_read_byte(unsigned int a) { return md_read_byte(a); }
unsigned int cpu_read_word(unsigned int a) { return md_read_word(a); }
unsigned int cpu_read_long(unsigned int a) { return ((uint32_t)md_read_word(a) << 16) | md_read_word(a + 2); }
unsigned int cpu_read_word_dasm(unsigned int a) { return md_read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return cpu_read_long(a); }
void cpu_write_byte(unsigned int a, unsigned int v) { md_write_byte(a, (uint8_t)v); }
void cpu_write_word(unsigned int a, unsigned int v) { md_write_word(a, (uint16_t)v); }
void cpu_write_long(unsigned int a, unsigned int v)
{
    md_write_word(a, (uint16_t)(v >> 16));
    md_write_word(a + 2, (uint16_t)v);
}
void cpu_write_long_pd(unsigned int a, unsigned int v)
{
    md_write_word(a + 2, (uint16_t)v);
    md_write_word(a, (uint16_t)(v >> 16));
}
int cpu_irq_ack(int level)
{
    md_vdp_acknowledge(&MD.vdp, level);
    return M68K_INT_ACK_AUTOVECTOR;
}
void cpu_set_fc(int fc) { (void)fc; }
void cpu_instr_callback(unsigned int pc) { (void)pc; }
void cpu_pulse_reset(void) {}

// ---- the machine ----
void md_set_region(int region)
{
    MD.region = region;
    MD.pal = region == MD_EUROPE;
    MD.version_reg = region == MD_JAPAN ? 0x80 : region == MD_EUROPE ? 0xc1 : 0xa1;
    MD.io_data[0] = MD.version_reg;
}

void md_init(int region)
{
    md_set_region(region);
    for (int i = 0; i < 8; i++) MD.rom_banks[i] = (uint32_t)i << 19;
    MD.vdp.hint = vdp_hint;
    MD.vdp.vint = vdp_vint;
    MD.vdp.z80_irq = vdp_z80_irq;
    MD.vdp.dma_read = vdp_dma_read;
    md_vdp_init(&MD.vdp, MD.pal, MD_RATE);
    ym2612_init(&MD.ym, md_m68k_clock(), 1.6f);
    MD.z80.read = z80_read;
    MD.z80.write = z80_write;
    MD.z80.in = z80_in;
    MD.z80.out = z80_out;
    zd_init(&MD.z80);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
}

void md_reset(void)
{
    if (MD.ext_reset) MD.ext_reset();
    memset(MD.ram, 0, sizeof MD.ram);
    memset(MD.z80_ram, 0, sizeof MD.z80_ram);
    memset(MD.io_data, 0, sizeof MD.io_data);
    memset(MD.io_ctrl, 0, sizeof MD.io_ctrl);
    MD.io_data[0] = MD.version_reg;
    MD.io_data[1] = 0x7f;
    MD.io_data[2] = 0x7f;
    MD.io_data[3] = 0x40;
    MD.z80_has_bus = true;
    MD.z80_is_reset = true;
    MD.z80_bank = 0;
    MD.audio_acc = 0;
    MD.cycles_on_line = 0;
    MD.m68k_debt = MD.z80_debt = 0;
    MD.hint_on = MD.vint_on = false;
    MD.vdp.pal = MD.pal;
    ym2612_reset(&MD.ym);
    md_vdp_reset(&MD.vdp);
    zd_reset(&MD.z80);
    m68k_set_irq(0);
    if (MD.loaded) m68k_pulse_reset();
    check_z80_bus_reset();
}

static uint32_t next_pow2(uint32_t v)
{
    uint32_t m = 1;
    while (m < v) m <<= 1;
    return m;
}

bool md_load(uint8_t *data, size_t size, const char **why)
{
    if (!data || size < 0x200) {
        if (why) *why = "cartridge image is too small";
        free(data);
        return false;
    }
    if (size % 16384 == 512) {            // copier header
        memmove(data, data + 512, size - 512);
        size -= 512;
    }
    if (!normalize_cartridge(data, size)) {
        if (why) *why = "not a Mega Drive / Genesis ROM (no SEGA header)";
        free(data);
        return false;
    }
    if (size > MD_MAX_ROM) size = MD_MAX_ROM;
    const size_t padded = next_pow2((uint32_t)(size < 2 ? 2 : size));
    if (padded > size) {                  // mirror up to a power of two (3 MB carts)
        uint8_t *p = (uint8_t *)realloc(data, padded);
        if (!p) {
            if (why) *why = "out of memory";
            free(data);
            return false;
        }
        data = p;
        for (size_t i = size; i < padded; i++) data[i] = data[i % size];
    }
    free(MD.rom);
    MD.rom = data;
    MD.rom_size = padded;
    MD.rom_mask = (uint32_t)(padded - 1);
    MD.ssf2_mapper = size > 0x400000;
    for (int i = 0; i < 8; i++) MD.rom_banks[i] = (uint32_t)i << 19;
    parse_header(size);
    MD.loaded = true;
    md_reset();
    return true;
}

void md_eject(void)
{
    free(MD.rom);
    MD.rom = NULL;
    MD.rom_size = 0;
    MD.loaded = false;
    md_reset();
}

static void audio_cycles(int cycles)
{
    const uint32_t clock = md_m68k_clock();
    MD.audio_acc += (uint64_t)cycles * MD_RATE;
    while (MD.audio_acc >= clock) {
        MD.audio_acc -= clock;
        int32_t s = ym2612_update(&MD.ym) + sn76496_update(&MD.vdp.psg) / 4;
        if (MD.ext_audio) s += MD.ext_audio();
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (MD.naudio < MD_AUDIO_MAX) MD.audio[MD.naudio++] = (int16_t)s;
    }
}

void md_run_m68k(int cycles)
{
    const int want = cycles - MD.m68k_debt;
    if (want <= 0) { MD.m68k_debt = -want; return; }
    int done = 0;
    if (MD.loaded) {
        MD.in_cpu = true;
        done = m68k_execute(want);
        MD.in_cpu = false;
    } else done = want;
    MD.cycles_on_line += done;
    MD.m68k_debt = done - want;
    audio_cycles(done);
}

void md_run_z80(int cycles)
{
    if (MD.z80_is_reset || !MD.z80_has_bus) return;
    const int want = cycles - MD.z80_debt;
    if (want <= 0) { MD.z80_debt = -want; return; }
    MD.z80_debt = zd_run(&MD.z80, want) - want;
}

int md_run_frame(void)
{
    MD.naudio = 0;
    const int lines = MD.vdp.total_scanlines;
    const double fps = md_fps();
    int m_line = (int)((double)md_m68k_clock() / fps / lines);
    int z_line = (int)((double)((MD.pal ? MD_MASTER_PAL : MD_MASTER_NTSC) / 15) / fps / lines);
    if (m_line < 1) m_line = 1;
    if (z_line < 1) z_line = 1;
    const int height = md_vdp_height(&MD.vdp);
    for (int line = 0; line < lines; line++) {
        MD.cycles_on_line = 0;
        // a slice of the line first, so the last HINT's CRAM changes show
        int pre = m_line / 4;
        if (pre < 1) pre = 1;
        const int post = m_line - pre;
        md_run_m68k(pre);
        const int zpre = z_line / 4 < 1 ? 1 : z_line / 4;
        md_run_z80(zpre);
        md_vdp_set_hpos(&MD.vdp, MD.cycles_on_line);
        md_vdp_scanline(&MD.vdp, line);
        if (MD.ext_line) MD.ext_line(line);
        if (line < height && line < MD_VDP_H && MD.fb)
            memcpy(MD.fb + line * MD_VDP_W, MD.vdp.line, MD_VDP_W * sizeof(uint16_t));
        if (post > 0) md_run_m68k(post);
        if (z_line - zpre > 0) md_run_z80(z_line - zpre);
    }
    md_vdp_eof(&MD.vdp);
    return MD.naudio;
}
