// Host stub for ql_sd_* when building *_pico.c with PORT=picohost (no microSD).
#include "../main/drivers/computers/ql/ql_sd.h"
#include <stddef.h>

bool ql_sd_mount(void) { return false; }
void ql_sd_unmount(void) {}
bool ql_sd_ready(void) { return false; }
int ql_sd_list_ext(ql_sd_entry *out, int max, const char *const *exts)
{
    (void)out; (void)max; (void)exts;
    return 0;
}
bool ql_sd_open(const char *name) { (void)name; return false; }
void ql_sd_close(void) {}
int ql_sd_read_sector(void *user, unsigned sector, uint8_t dest[686])
{
    (void)user; (void)sector; (void)dest;
    return -1;
}
uint8_t *ql_sd_load_file(const char *name, size_t *out_size)
{
    (void)name;
    if (out_size) *out_size = 0;
    return NULL;
}
