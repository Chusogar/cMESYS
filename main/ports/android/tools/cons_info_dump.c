/* cons_info_dump.c - prints a console's cons_machine (gen_consoles_android.py
   links it with the console's core). */
#include <stdio.h>
#include "cons.h"

static void s(const char *k, const char *v) { printf("%s=%s\n", k, v ? v : "-"); }

int main(void)
{
    const cons_info *c = &cons_machine;
    s("id", c->id);
    s("title", c->title);
    s("maker", c->maker);
    s("media_label", c->media_label);
    printf("exts=");
    for (int i = 0; c->exts && c->exts[i]; i++) printf("%s%s", i ? " " : "", c->exts[i]);
    printf("\nmax_w=%d\nmax_h=%d\nfps=%.4f\naudio_rate=%d\n", c->max_w, c->max_h, c->fps, c->audio_rate);
    printf("buttons=");
    for (int i = 0; i < 4; i++) printf("%s%s", i ? " " : "", c->button[i] ? c->button[i] : "-");
    printf("\n");
    s("start", c->start);
    s("select", c->select);
    printf("keys=%d\nplayers=%d\n", c->keys, c->players);
    s("keypad_help", c->keypad_help);
    return 0;
}
