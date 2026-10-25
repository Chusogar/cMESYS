// pico_host.c - the RP2040-PiZero port API (main/ports/rp2040-pizero/
// pico_port.h) on the desktop, without a window: runs a *_pico.c frontend,
// draws its picture with its render callback like core 1 does (bands of 4
// rows) and saves frames, to test the frontends without the board.
//
//   make GAME=<game> PORT=picohost        -> build/picohost/<game>
//   PICO_FRAMES=n      quit after n frames (default 600)
//   PICO_SHOT=k[,k..]  save frame k as shot_<k>.ppm (320x240)
//   PICO_KEYS=bits@first:count,...   SP_* bits held from frame first for count frames
//   PICO_PRESS=usage@frame,...       HID key presses (computers), e.g. 58@5 = F1 at frame 5
#include "pico_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

static pico_port_cfg cfg;
static long frames, quit_after = 600;
static long audio_samples;

void pico_port_open(const pico_port_cfg *c)
{
    cfg = *c;
    const char *e = getenv("PICO_FRAMES");
    if (e) quit_after = atol(e);
}

static void shot(long f)
{
    static uint16_t band[PICO_BAND * PICO_W];
    char name[64];
    snprintf(name, sizeof name, "shot_%ld.ppm", f);
    FILE *o = fopen(name, "wb");
    if (!o) return;
    fprintf(o, "P6 %d %d 255\n", PICO_W, PICO_H);
    for (int y = 0; y < PICO_H; y += PICO_BAND) {
        cfg.render(cfg.user, y, PICO_BAND, band);
        for (int i = 0; i < PICO_BAND * PICO_W; i++) {
            uint16_t c = band[i];
            fputc(((c >> 11) & 31) << 3, o);
            fputc(((c >> 5) & 63) << 2, o);
            fputc((c & 31) << 3, o);
        }
    }
    fclose(o);
}

static bool listed(const char *list, long f)
{
    for (const char *p = list; p && *p;) {
        if (atol(p) == f) return true;
        p = strchr(p, ',');
        if (p) p++;
    }
    return false;
}

void pico_port_run(void (*fn)(void))
{
    fn();
    exit(0);
}

bool pico_port_poll(void) { return false; }

uint32_t pico_port_keys(void)
{
    uint32_t k = 0;
    for (const char *p = getenv("PICO_KEYS"); p && *p;) {
        unsigned bits;
        long first, count;
        if (sscanf(p, "%u@%ld:%ld", &bits, &first, &count) == 3 && frames >= first && frames < first + count) k |= bits;
        p = strchr(p, ',');
        if (p) p++;
    }
    return k;
}

static int held_usage = -1;
static long held_until;

bool pico_port_key(int usage) { return usage == held_usage && frames < held_until; }

int pico_port_next_press(void)
{
    static long last = -1;
    if (frames == last) return -1;
    for (const char *p = getenv("PICO_PRESS"); p && *p;) {
        int u;
        long f;
        if (sscanf(p, "%d@%ld", &u, &f) == 2 && f == frames) {
            last = frames;
            held_usage = u;
            held_until = frames + 8;
            return u;
        }
        p = strchr(p, ',');
        if (p) p++;
    }
    return -1;
}

void pico_port_audio(const int16_t *s, int n)
{
    (void)s;
    audio_samples += n;
}

void pico_port_frame_done(void)
{
    if (listed(getenv("PICO_SHOT"), frames)) shot(frames);
    frames++;
    if (frames >= quit_after) {
        printf("%s: %ld frames, %ld audio samples (%.0f Hz at %.2f fps), heap in use %zu KB\n", cfg.title, frames,
               audio_samples, audio_samples * cfg.fps / frames, cfg.fps, mallinfo2().uordblks / 1024);
        exit(0);
    }
}
