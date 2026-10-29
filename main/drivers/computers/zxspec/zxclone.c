// zxclone.c - Pentagon 128 / Pentagon 1024 and Scorpion ZS-256: C11 port of
// dsp-cpp drivers/computers/zx_clone (ZxClone). Z80 at 3.5 MHz with no
// contention, 224 T-states x 320 lines, paper on lines 80-271; one AY at
// 1.75 MHz; the Beta 128 disk interface (TR-DOS paged in by an opcode fetch
// from $3D00-$3DFF with the 48K ROM selected, out again by one from RAM).
// Pentagon: up to 64 RAM pages with port $DFFD, the Gluk ROM. Scorpion:
// 16 pages, port $1FFD (RAM over the ROM, service ROM), the 128K-compatible
// $7FFD decoding and the magic button (an NMI taken on the next fetch from RAM).
#include "zx_internal.h"

static bool is_scorpion(const zxspec *m) { return m->model == ZX_SCORPION; }

static void update_memory(zxspec *m)
{
    m->pantalla = (m->port_7ffd & 0x08) ? 7 : 5;
    m->page0_ram = false;
    if (!is_scorpion(m)) {
        m->ram3 = (uint8_t)(((m->port_7ffd & 7) | ((m->port_7ffd & 0xc0) >> 3) | ((m->port_dffd & 1) << 5)) &
                            (m->ram_pages - 1));
        const uint8_t rom1 = (uint8_t)((m->port_7ffd >> 4) & 1);
        if (m->beta.active) m->rom_page = (m->gluk_present && rom1 == 0) ? 2 : 3;
        else m->rom_page = rom1;
    } else {
        m->ram3 = (uint8_t)(((m->port_7ffd & 7) | ((m->port_1ffd & 0x10) >> 1)) & (m->ram_pages - 1));
        if ((m->port_1ffd & 0x01) && !m->nmi_pending) {
            m->page0_ram = true;
            m->rom_page = 0;
        } else if (m->port_1ffd & 0x02) {
            m->rom_page = 2;
        } else {
            const uint8_t rom1 = (uint8_t)((m->port_7ffd >> 4) & 1);
            m->rom_page = (uint8_t)((((m->nmi_pending || m->beta.active) ? 1 : 0) << 1) | rom1);
        }
    }
}

// before each opcode fetch
static void ZX_HOT on_m1(void *user, uint16_t pc)
{
    zxspec *m = (zxspec *)user;
    if (pc >= 0x3d00 && pc < 0x3e00) {
        const bool rom48 = (m->port_7ffd & 0x10) != 0;
        if (rom48 && !m->page0_ram) zx_beta_enable(&m->beta);
        update_memory(m);
    } else if (pc >= 0x4000) {
        if (m->nmi_pending && !is_scorpion(m)) {
            m->nmi_pending = false;
            update_memory(m);
        } else if (m->nmi_pending) {
            // MAME scorpion do_nmi: the magic button takes effect on the next
            // opcode fetch outside the ROM, with the service ROM (or TR-DOS
            // in 48K mode) and the Beta ports paged in.
            zx_beta_enable(&m->beta);
            update_memory(m);
            m->nmi_pending = false;
            zd_set_nmi(&m->cpu, ZD_PULSE);
            return;
        }
        if (m->beta.active) {
            zx_beta_disable(&m->beta);
            update_memory(m);
        }
    }
}

static uint8_t ZX_HOT mem_read(void *user, uint16_t addr)
{
    zxspec *m = (zxspec *)user;
    if (addr < 0x4000) return m->page0_ram ? m->ram[0][addr] : m->rom[m->rom_page][addr];
    if (addr < 0x8000) return m->ram[5][addr & 0x3fff];
    if (addr < 0xc000) return m->ram[2][addr & 0x3fff];
    return m->ram[m->ram3][addr & 0x3fff];
}

static void ZX_HOT mem_write(void *user, uint16_t addr, uint8_t value)
{
    zxspec *m = (zxspec *)user;
    if (addr < 0x4000) {
        if (m->page0_ram) m->ram[0][addr] = value;
        return;
    }
    if (addr < 0x8000) m->ram[5][addr & 0x3fff] = value;
    else if (addr < 0xc000) m->ram[2][addr & 0x3fff] = value;
    else m->ram[m->ram3][addr & 0x3fff] = value;
}

static uint8_t io_in(void *user, uint16_t port)
{
    zxspec *m = (zxspec *)user;
    if (m->rzx.playing) return zx_rzx_next_in(&m->rzx);
    if ((port & 1) == 0) return (uint8_t)((zx_keys_read(m, port) & 0x1f) | 0xa0 | m->ear | m->speaker);
    if (m->beta.active) {
        switch (port & 0xff) {
        case 0x1f: return zx_beta_status_r(&m->beta);
        case 0x3f: return zx_beta_track_r(&m->beta);
        case 0x5f: return zx_beta_sector_r(&m->beta);
        case 0x7f: return zx_beta_data_r(&m->beta);
        case 0xff: return zx_beta_state_r(&m->beta);
        default: break;
        }
    } else if ((port & 0x21) == 0x01) {
        if (is_scorpion(m) && (port & 0xa3) == 0x03) {
            // Scorpion: port #1F still shows the Beta INTRQ / DRQ on D7-D6
            // with TR-DOS paged out (MAME scorpion_io); Kempston on D4-D0.
            return (uint8_t)((zx_beta_state_r(&m->beta) & 0xc0) | (m->joy & 0x1f));
        }
        return (uint8_t)(m->joy & 0x1f);
    }
    if (!is_scorpion(m)) {
        if ((port & 0xc002) == 0xc000) return ay8910_data_r(&m->ay[0]);
    } else {
        if ((port & 0xe023) == 0xe021) return ay8910_data_r(&m->ay[0]);
    }
    return 0xff;
}

static void io_out(void *user, uint16_t port, uint8_t value)
{
    zxspec *m = (zxspec *)user;
    if ((port & 1) == 0) {
        m->border = value & 7;
        m->speaker = (value & 0x10) ? 0x10 : 0x00;
        m->beeper_level = (value & 0x10) ? 4096 : -4096;
    }
    if (m->beta.active) {
        switch (port & 0xff) {
        case 0x1f: zx_beta_command_w(&m->beta, value); break;
        case 0x3f: zx_beta_track_w(&m->beta, value); break;
        case 0x5f: zx_beta_sector_w(&m->beta, value); break;
        case 0x7f: zx_beta_data_w(&m->beta, value); break;
        case 0xff: zx_beta_param_w(&m->beta, value); break;
        default: break;
        }
    }
    if (!is_scorpion(m)) {
        if ((port & 0x8002) == 0 && (port & 1) != 0 && !m->paging_locked) {
            m->port_7ffd = value;
            m->paging_locked = (value & 0x20) != 0;
            update_memory(m);
        }
        if ((port & 0xf002) == 0xd000) {
            m->port_dffd = value;
            update_memory(m);
        } else if ((port & 0xc002) == 0xc000) {
            ay8910_address_w(&m->ay[0], value);
        } else if ((port & 0xc002) == 0x8000) {
            ay8910_data_w(&m->ay[0], value);
        }
    } else {
        // The board decodes #7FFD as 01xxxxxxxx1xxx01 and #1FFD as
        // 00xxxxxxxx1xxx01 (MAME scorpion_io). With the compatibility
        // decoding (dsp-cpp's default) only #1FFD itself reaches the
        // Scorpion port and every other A15 = 0, A1 = 0 address is #7FFD
        // (the common "LD A,#11 : OUT (#FD),A" of 128K programs).
        bool is_7ffd = (port & 0xc023) == 0x4021;
        bool is_1ffd = (port & 0xc023) == 0x0021;
        if (m->port_compat && (port & 0x8002) == 0 && (port & 1) != 0) {
            is_1ffd = (port & 0xff00) == 0x1f00;
            is_7ffd = !is_1ffd;
        }
        if (is_7ffd && !m->paging_locked) {
            m->port_7ffd = value;
            m->paging_locked = (value & 0x20) != 0;
            update_memory(m);
        }
        if (is_1ffd) {
            m->port_1ffd = value;
            update_memory(m);
        }
        if ((port & 0xe023) == 0xe021) ay8910_address_w(&m->ay[0], value);
        if ((port & 0xe023) == 0xa021) ay8910_data_w(&m->ay[0], value);
    }
}

static void m1(void *user)
{
    zxspec *m = (zxspec *)user;
    if (m->rzx.playing) zx_rzx_on_m1(&m->rzx);
}

static void cycles(void *user, int t) { zx_on_cycles((zxspec *)user, t); }

void zxclone_setup(zxspec *m)
{
    m->clock = 3500000;
    m->t_line = 224;
    m->lines = 320;
    m->t_frame = 224 * 320;
    m->paper_first = 80;
    m->pic_first = 32;
    m->left_t = 200;
    m->fps = 3500000.0 / (224 * 320);
    m->nay = 1;
    ay8910_init(&m->ay[0], 1750000, m->audio_rate);
    m->ulaplus_enabled = false;
    m->port_compat = true;
    m->cpu.read = mem_read;
    m->cpu.write = mem_write;
    m->cpu.in = io_in;
    m->cpu.out = io_out;
    m->cpu.cycles = cycles;
    m->cpu.hook = on_m1;
    m->cpu.m1 = m1;
    m->cpu.internal = NULL;
}

void zxclone_reset(zxspec *m)
{
    for (int i = 0; i < m->ram_pages; i++) memset(m->ram[i], 0, ZX_BANK);
    m->port_7ffd = m->port_1ffd = m->port_dffd = 0;
    m->paging_locked = false;
    m->nmi_pending = false;
    m->magic_down = false;
    zx_beta_reset(&m->beta);
    if (!is_scorpion(m) && m->gluk_present) zx_beta_enable(&m->beta);
    update_memory(m);
}

void zxclone_apply_snap(zxspec *m, const zx_snap *s)
{
    // classic snapshots have no #1FFD / #DFFD
    m->port_1ffd = s->is_128 ? s->port_1ffd : 0;
    m->port_dffd = 0;
    m->paging_locked = false;
    m->nmi_pending = false;
    if (s->is_128) {
        m->port_7ffd = s->port_7ffd;
        if (s->trdos_paged) zx_beta_enable(&m->beta);
        else zx_beta_disable(&m->beta);
    } else {
        m->port_7ffd = 0x10;            // 48K: ROM 1, bank 0 at C000, screen 5, Beta out
        zx_beta_disable(&m->beta);
    }
    update_memory(m);
    if (!s->is_128) {
        m->pantalla = 5;
        m->ram3 = 0;
        m->page0_ram = false;
        m->rom_page = 1;
    }
    if (s->ay_used) {
        ay8910_reset(&m->ay[0]);
        for (int r = 0; r < 16; r++) {
            ay8910_address_w(&m->ay[0], (uint8_t)r);
            ay8910_data_w(&m->ay[0], s->ay_regs[r]);
        }
        ay8910_address_w(&m->ay[0], s->ay_latch);
    }
}

// F5: the magic button
void zxclone_magic(zxspec *m)
{
    m->nmi_pending = true;
    // the Scorpion defers it until the CPU fetches from RAM (on_m1)
    if (!is_scorpion(m)) zd_set_nmi(&m->cpu, ZD_PULSE);
}
