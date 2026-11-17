// romfs_sd.c - romfs on the boards' microSD (CYD and RP2040/RP2350), through
// the card access the QL and the computers use (ql_sd_esp.c / ql_sd_pico.c).
#include "romfs.h"
#include "romfs_sd.h"
#include "../drivers/computers/ql/ql_sd.h"

// (the RP2350/RP2040 port: clock, UART and PSRAM before the card is read)
void romfs_board_prepare(void) __attribute__((weak));

static bool ready(void)
{
    static bool tried;
    if (ql_sd_ready()) return true;
    if (tried) return false;            // (one mount attempt: no card, no wait)
    tried = true;
    if (romfs_board_prepare) romfs_board_prepare();
    return ql_sd_mount();
}

static void *sd_open(const char *path, uint32_t *size)
{
    if (!ready()) return NULL;
    size_t n = 0;
    ql_sd_file *f = ql_sd_file_open(path, &n);
    if (f) *size = (uint32_t)n;
    return f;
}

static bool sd_read(void *h, uint32_t off, void *dst, uint32_t n)
{
    return n == 0 || ql_sd_file_read((ql_sd_file *)h, off, (uint8_t *)dst, n);
}

static void sd_close(void *h) { ql_sd_file_close((ql_sd_file *)h); }

static bool sd_list(const char *dir, void (*fn)(void *user, const char *name), void *user)
{
    return ready() && ql_sd_list_dir(dir, fn, user);
}

static const romfs_io sd_io = { sd_open, sd_read, sd_close, sd_list };

void romfs_use_sd(void)
{
    static bool done;
    if (done) return;
    done = true;
    romfs_set_io(&sd_io);
    romfs_clear_dirs();
    romfs_add_dir("roms");
    romfs_add_dir("");
}
