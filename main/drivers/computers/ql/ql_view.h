// ql_view.h - the QL picture (512x256) on a 320x240 screen: whole (FIT,
// averaged: 8 columns -> 5, 16 rows -> 15) or 1:1 (the left or right 320
// columns, rows 8-247).
#ifndef QL_VIEW_H
#define QL_VIEW_H
#include <stdint.h>
#include <stdbool.h>

enum { QL_VIEW_FIT, QL_VIEW_LEFT, QL_VIEW_RIGHT, QL_VIEW_N };

// rgb: the screen's colour format (0xRRGGBB parts -> pixel; the parts are OR-ed)
void ql_view_init(uint16_t (*rgb)(uint8_t r, uint8_t g, uint8_t b));
// The picture to draw rows of (see ql_screen / ql_video_control / ql_flash).
void ql_view_begin(const uint8_t *screen, uint8_t control, bool flash);
// Source rows lo..hi the output rows y0..y1 are made of.
void ql_view_src_rows(int view, int y0, int y1, int *lo, int *hi);
// Output row y (0-239): 320 pixels.
void ql_view_row(int view, int y, uint16_t *out);
#endif
