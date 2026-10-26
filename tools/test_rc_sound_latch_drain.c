/* Host regression: mid-slice latch drain must use now(c), not slice start.
 *   cc -O2 -o /tmp/test_rc_latch tools/test_rc_sound_latch_drain.c \
 *      main/drivers/arcades/robocop/rc_sound.c main/sound/ym2610.c \
 *      main/sound/okim6295.c main/sound/emu8950/emu8950.c \
 *      main/sound/emu8950/emuadpcm.c -I main/drivers/arcades/robocop \
 *      -I main/cpu -I main/cpu/m6502 -I main/sound -I main/sound/emu8950 -lm
 * (Heavy; this file instead reimplements the queue rule alone.)
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define RC_SND_EVENTS 8

typedef struct { int64_t time; uint8_t value; } ev_t;
typedef struct {
    ev_t ev[RC_SND_EVENTS];
    uint32_t head, tail;
    int64_t time;
} snd_t;

static bool latch(snd_t *s, int64_t t, uint8_t v)
{
    if (s->head - s->tail >= RC_SND_EVENTS) return false;
    s->ev[s->head % RC_SND_EVENTS] = (ev_t){ t, v };
    s->head++;
    return true;
}

static void run_to(snd_t *s, int64_t until)
{
    while (s->tail != s->head) {
        ev_t *e = &s->ev[s->tail % RC_SND_EVENTS];
        if (e->time > until) break;
        s->tail++;
    }
    if (until > s->time) s->time = until;
}

/* Bug: drain to slice_start leaves mid-slice events stuck. */
static int try_write(snd_t *s, int64_t slice_start, int64_t now, uint8_t v, bool use_now)
{
    int spins = 0;
    while (!latch(s, now, v)) {
        run_to(s, use_now ? now : slice_start);
        if (++spins > 1000) return -1; /* hung */
    }
    return (int)(s->head - s->tail);
}

int main(void)
{
    snd_t s;
    memset(&s, 0, sizeof s);
    const int64_t slice = 0;
    /* Fill queue with mid-slice timestamps (all > slice_start). */
    for (int i = 0; i < RC_SND_EVENTS; i++)
        if (!latch(&s, 100 + i, (uint8_t)i)) return 2;

    int bad = try_write(&s, slice, 200, 0xff, false);
    int good = 0;
    {
        snd_t s2;
        memset(&s2, 0, sizeof s2);
        for (int i = 0; i < RC_SND_EVENTS; i++)
            latch(&s2, 100 + i, (uint8_t)i);
        good = try_write(&s2, slice, 200, 0xff, true);
    }

    if (bad != -1) {
        fprintf(stderr, "FAIL: drain-to-slice-start should hang, got Q=%d\n", bad);
        return 1;
    }
    if (good < 0) {
        fprintf(stderr, "FAIL: drain-to-now hung\n");
        return 1;
    }
    printf("ok: drain-to-slice-start hangs; drain-to-now leaves Q=%d\n", good);
    return 0;
}
