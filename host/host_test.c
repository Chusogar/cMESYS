// host_test.c - runs the Track & Field core headless, dumps frames (PPM) and audio.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../main/tf_core.h"

static uint8_t fb[TF_W * TF_H];
static uint8_t pal[32][3];

static void dump(const char *name)
{
    FILE *f = fopen(name, "wb");
    fprintf(f, "P6\n256 224\n255\n");
    for (int i = 0; i < 256 * 224; i++) fwrite(pal[fb[i] & 31], 1, 3, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 3000;
    tf_init();
    tf_palette_rgb(pal);
    static tf_video_t v;
    static int16_t audio[TF_SAMPLES_FRAME];
    FILE *raw = fopen("out/audio.raw", "wb");
    clock_t t0 = clock();
    int faults = 0;
    for (int f = 0; f < frames; f++) {
        uint8_t in0 = 0xff, in1 = 0xff;
        if (f >= 900 && f < 906) in0 &= ~TF_IN0_COIN1;
        if (f >= 1000 && f < 1006) in0 &= ~TF_IN0_START1;
        if (f >= 1300) {                       // alternate the two run buttons
            int ph = f % 8;
            if (ph < 2) in1 &= ~TF_IN1_RUN1;
            else if (ph >= 4 && ph < 6) in1 &= ~TF_IN1_RUN2;
        }
        if (f >= 1300 && (f % 90) < 4) in1 &= ~TF_IN1_JUMP;
        tf_in.in0 = in0; tf_in.in1 = in1;
        if (!tf_run_frame(audio)) faults++;
        fwrite(audio, 2, TF_SAMPLES_FRAME, raw);
        tf_snapshot(&v);
        tf_render(&v, fb);
        if (f % 200 == 199) {
            char n[64]; snprintf(n, sizeof n, "out/f%05d.ppm", f + 1);
            dump(n);
        }
    }
    fclose(raw);
    printf("%d frames, %d faults, %.2fs host CPU\n", frames, faults, (double)(clock() - t0) / CLOCKS_PER_SEC);
    return 0;
}
