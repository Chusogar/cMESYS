#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "psx_gte.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void cdp(psx_gte *P);
static void cc(psx_gte *P);
static void dcpt(psx_gte *P);
static void dcpl(psx_gte *P);
static void nccs(psx_gte *P, int r);
static void ncct(psx_gte *P);
static void dpcs(psx_gte *P, bool dpct);
static void intpl(psx_gte *P);
static void nct(psx_gte *P);
static void ncs(psx_gte *P, int r);
static void mvmva(psx_gte *P);
static void gpl(psx_gte *P);
static void gpf(psx_gte *P);
static void ncdt(psx_gte *P);
static void op(psx_gte *P);
static void sqr(psx_gte *P);
static void avsz3(psx_gte *P);
static void avsz4(psx_gte *P);
static void ncds(psx_gte *P, int r);
static void interpolate_color(psx_gte *P, int mac1, int mac2, int mac3);
static void nclip(psx_gte *P);
static void rtpt(psx_gte *P);
static void rtps(psx_gte *P, int r, bool set_mac0_flag);
static int16_t set_ir0(psx_gte *P, int64_t value);
static int16_t set_sxy(psx_gte *P, int i, int value);
static uint16_t set_sz3(psx_gte *P, int64_t value);
static uint8_t set_rgb(psx_gte *P, int i, int value);
static int16_t set_ir(psx_gte *P, int i, int value, bool lm_flag);
static int64_t set_mac0(psx_gte *P, int64_t value);
static int64_t set_mac(psx_gte *P, int i, int64_t value);
static uint8_t saturate_rgb(psx_gte *P, int value);
static int leading_count(psx_gte *P, uint32_t v);
// Portions derived from ProjectPSX GTE.cs (MIT License)
// Copyright (c) 2019 Pedro Cortés
// See PROJECTPSX_LICENSE / https://github.com/BluestormDNA/ProjectPSX





// ProjectPSX UNR reciprocal approximation table (257 entries).
static const uint8_t kUnrTable[257] = {
    0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1, 0xEF, 0xEE, 0xEC, 0xEA, 0xE8, 0xE6, 0xE4, 0xE3,
    0xE1, 0xDF, 0xDD, 0xDC, 0xDA, 0xD8, 0xD6, 0xD5, 0xD3, 0xD1, 0xD0, 0xCE, 0xCD, 0xCB, 0xC9, 0xC8,
    0xC6, 0xC5, 0xC3, 0xC1, 0xC0, 0xBE, 0xBD, 0xBB, 0xBA, 0xB8, 0xB7, 0xB5, 0xB4, 0xB2, 0xB1, 0xB0,
    0xAE, 0xAD, 0xAB, 0xAA, 0xA9, 0xA7, 0xA6, 0xA4, 0xA3, 0xA2, 0xA0, 0x9F, 0x9E, 0x9C, 0x9B, 0x9A,
    0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8F, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x87, 0x86,
    0x85, 0x84, 0x83, 0x82, 0x81, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x79, 0x78, 0x77, 0x75, 0x74,
    0x73, 0x72, 0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
    0x63, 0x62, 0x61, 0x60, 0x5F, 0x5E, 0x5D, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58, 0x57, 0x56, 0x55,
    0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D, 0x4D, 0x4C, 0x4B, 0x4A, 0x49, 0x48, 0x48,
    0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3F, 0x3F, 0x3E, 0x3D, 0x3C, 0x3C, 0x3B,
    0x3A, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2F,
    0x2E, 0x2E, 0x2D, 0x2C, 0x2C, 0x2B, 0x2A, 0x2A, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
    0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1F, 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1B, 0x1B, 0x1A,
    0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
    0x10, 0x0F, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0A, 0x0A, 0x09, 0x09, 0x08, 0x08,
    0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00,
    0x00};
void psx_gte_reset(psx_gte *P) {
  memset(P, 0, sizeof *P);
}
void psx_gte_execute(psx_gte *P, uint32_t command) {
  P->current_command_ = command;
  P->sf_ = (int)((command & 0x80000u) >> 19) * 12;
  P->lm_ = ((command >> 10) & 0x1u) != 0;
  P->FLAG_ = 0;

  switch (command & 0x3Fu) {
    case 0x01: rtps(P, 0, true); break;
    case 0x06: nclip(P); break;
    case 0x0C: op(P); break;
    case 0x10: dpcs(P, false); break;
    case 0x11: intpl(P); break;
    case 0x12: mvmva(P); break;
    case 0x13: ncds(P, 0); break;
    case 0x14: cdp(P); break;
    case 0x16: ncdt(P); break;
    case 0x1B: nccs(P, 0); break;
    case 0x1C: cc(P); break;
    case 0x1E: ncs(P, 0); break;
    case 0x20: nct(P); break;
    case 0x28: sqr(P); break;
    case 0x29: dcpl(P); break;
    case 0x2A: dcpt(P); break;
    case 0x2D: avsz3(P); break;
    case 0x2E: avsz4(P); break;
    case 0x30: rtpt(P); break;
    case 0x3D: gpf(P); break;
    case 0x3E: gpl(P); break;
    case 0x3F: ncct(P); break;
    default: break;
  }

  if ((P->FLAG_ & 0x7F87E000u) != 0) {
    P->FLAG_ |= 0x80000000u;
  }
}
static void cdp(psx_gte *P) {
  P->MAC1_ = (int32_t)(
      set_mac(P, 1, set_mac(P, 1, set_mac(P, 1, (int64_t)(P->RBK_) * 0x1000 + P->LRGB_.v1.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v1.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v1.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, set_mac(P, 2, set_mac(P, 2, (int64_t)(P->GBK_) * 0x1000 + P->LRGB_.v2.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v2.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v2.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC3_ = (int32_t)(
      set_mac(P, 3, set_mac(P, 3, set_mac(P, 3, (int64_t)(P->BBK_) * 0x1000 + P->LRGB_.v3.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v3.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v3.z) * P->IR_[3]) >>
      P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->MAC1_ = (int32_t)(set_mac(P, 1, (int64_t)(P->RGBC_.r) * P->IR_[1]) << 4);
  P->MAC2_ = (int32_t)(set_mac(P, 2, (int64_t)(P->RGBC_.g) * P->IR_[2]) << 4);
  P->MAC3_ = (int32_t)(set_mac(P, 3, (int64_t)(P->RGBC_.b) * P->IR_[3]) << 4);

  interpolate_color(P, P->MAC1_, P->MAC2_, P->MAC3_);

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;
}
static void cc(psx_gte *P) {
  P->MAC1_ = (int32_t)(
      set_mac(P, 1, set_mac(P, 1, set_mac(P, 1, (int64_t)(P->RBK_) * 0x1000 + P->LRGB_.v1.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v1.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v1.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, set_mac(P, 2, set_mac(P, 2, (int64_t)(P->GBK_) * 0x1000 + P->LRGB_.v2.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v2.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v2.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC3_ = (int32_t)(
      set_mac(P, 3, set_mac(P, 3, set_mac(P, 3, (int64_t)(P->BBK_) * 0x1000 + P->LRGB_.v3.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v3.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v3.z) * P->IR_[3]) >>
      P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->MAC1_ = (int32_t)(set_mac(P, 1, (int64_t)(P->RGBC_.r) * P->IR_[1]) << 4);
  P->MAC2_ = (int32_t)(set_mac(P, 2, (int64_t)(P->RGBC_.g) * P->IR_[2]) << 4);
  P->MAC3_ = (int32_t)(set_mac(P, 3, (int64_t)(P->RGBC_.b) * P->IR_[3]) << 4);

  P->MAC1_ = (int32_t)(set_mac(P, 1, P->MAC1_) >> P->sf_);
  P->MAC2_ = (int32_t)(set_mac(P, 2, P->MAC2_) >> P->sf_);
  P->MAC3_ = (int32_t)(set_mac(P, 3, P->MAC3_) >> P->sf_);

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);
}
static void dcpt(psx_gte *P) {
  dpcs(P, true);
  dpcs(P, true);
  dpcs(P, true);
}
static void dcpl(psx_gte *P) {
  P->MAC1_ = (int32_t)(set_mac(P, 1, P->RGBC_.r * P->IR_[1]) << 4);
  P->MAC2_ = (int32_t)(set_mac(P, 2, P->RGBC_.g * P->IR_[2]) << 4);
  P->MAC3_ = (int32_t)(set_mac(P, 3, P->RGBC_.b * P->IR_[3]) << 4);

  interpolate_color(P, P->MAC1_, P->MAC2_, P->MAC3_);

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;
}
static void nccs(psx_gte *P, int r) {
  P->MAC1_ = (int32_t)(
      set_mac(P, 1, (int64_t)(P->LM_.v1.x) * P->V_[r].x + P->LM_.v1.y * P->V_[r].y + P->LM_.v1.z * P->V_[r].z) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, (int64_t)(P->LM_.v2.x) * P->V_[r].x + P->LM_.v2.y * P->V_[r].y + P->LM_.v2.z * P->V_[r].z) >>
      P->sf_);
  P->MAC3_ = (int32_t)(
      set_mac(P, 3, (int64_t)(P->LM_.v3.x) * P->V_[r].x + P->LM_.v3.y * P->V_[r].y + P->LM_.v3.z * P->V_[r].z) >>
      P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->MAC1_ = (int32_t)(
      set_mac(P, 1, set_mac(P, 1, set_mac(P, 1, (int64_t)(P->RBK_) * 0x1000 + P->LRGB_.v1.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v1.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v1.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, set_mac(P, 2, set_mac(P, 2, (int64_t)(P->GBK_) * 0x1000 + P->LRGB_.v2.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v2.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v2.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC3_ = (int32_t)(
      set_mac(P, 3, set_mac(P, 3, set_mac(P, 3, (int64_t)(P->BBK_) * 0x1000 + P->LRGB_.v3.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v3.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v3.z) * P->IR_[3]) >>
      P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->MAC1_ = (int32_t)(set_mac(P, 1, (P->RGBC_.r * P->IR_[1]) << 4));
  P->MAC2_ = (int32_t)(set_mac(P, 2, (P->RGBC_.g * P->IR_[2]) << 4));
  P->MAC3_ = (int32_t)(set_mac(P, 3, (P->RGBC_.b * P->IR_[3]) << 4));

  P->MAC1_ = (int32_t)(set_mac(P, 1, P->MAC1_ >> P->sf_));
  P->MAC2_ = (int32_t)(set_mac(P, 2, P->MAC2_ >> P->sf_));
  P->MAC3_ = (int32_t)(set_mac(P, 3, P->MAC3_ >> P->sf_));

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);
}
static void ncct(psx_gte *P) {
  nccs(P, 0);
  nccs(P, 1);
  nccs(P, 2);
}
static void dpcs(psx_gte *P, bool dpct) {
  uint8_t r = P->RGBC_.r;
  uint8_t g = P->RGBC_.g;
  uint8_t b = P->RGBC_.b;
  if (dpct) {
    r = P->RGB_[0].r;
    g = P->RGB_[0].g;
    b = P->RGB_[0].b;
  }

  P->MAC1_ = (int32_t)(set_mac(P, 1, r) << 16);
  P->MAC2_ = (int32_t)(set_mac(P, 2, g) << 16);
  P->MAC3_ = (int32_t)(set_mac(P, 3, b) << 16);

  interpolate_color(P, P->MAC1_, P->MAC2_, P->MAC3_);

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;
}
static void intpl(psx_gte *P) {
  P->MAC1_ = (int32_t)(set_mac(P, 1, (int64_t)(P->IR_[1]) << 12));
  P->MAC2_ = (int32_t)(set_mac(P, 2, (int64_t)(P->IR_[2]) << 12));
  P->MAC3_ = (int32_t)(set_mac(P, 3, (int64_t)(P->IR_[3]) << 12));

  interpolate_color(P, P->MAC1_, P->MAC2_, P->MAC3_);

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;
}
static void nct(psx_gte *P) {
  ncs(P, 0);
  ncs(P, 1);
  ncs(P, 2);
}
static void ncs(psx_gte *P, int r) {
  P->MAC1_ = (int32_t)(
      set_mac(P, 1, (int64_t)(P->LM_.v1.x) * P->V_[r].x + P->LM_.v1.y * P->V_[r].y + P->LM_.v1.z * P->V_[r].z) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, (int64_t)(P->LM_.v2.x) * P->V_[r].x + P->LM_.v2.y * P->V_[r].y + P->LM_.v2.z * P->V_[r].z) >>
      P->sf_);
  P->MAC3_ = (int32_t)(
      set_mac(P, 3, (int64_t)(P->LM_.v3.x) * P->V_[r].x + P->LM_.v3.y * P->V_[r].y + P->LM_.v3.z * P->V_[r].z) >>
      P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->MAC1_ = (int32_t)(
      set_mac(P, 1, set_mac(P, 1, set_mac(P, 1, (int64_t)(P->RBK_) * 0x1000 + P->LRGB_.v1.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v1.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v1.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, set_mac(P, 2, set_mac(P, 2, (int64_t)(P->GBK_) * 0x1000 + P->LRGB_.v2.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v2.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v2.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC3_ = (int32_t)(
      set_mac(P, 3, set_mac(P, 3, set_mac(P, 3, (int64_t)(P->BBK_) * 0x1000 + P->LRGB_.v3.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v3.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v3.z) * P->IR_[3]) >>
      P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);
}
static void mvmva(psx_gte *P) {
  const uint32_t mx_index = (P->current_command_ >> 17) & 0x3u;
  const uint32_t mv_index = (P->current_command_ >> 15) & 0x3u;
  const uint32_t tv_index = (P->current_command_ >> 13) & 0x3u;

  gte_matrix mx; memset(&mx, 0, sizeof mx);
  gte_vec3 vx; memset(&vx, 0, sizeof vx);
  int64_t tx = 0, ty = 0, tz = 0;

  if (mx_index == 0) {
    mx = P->RT_;
  } else if (mx_index == 1) {
    mx = P->LM_;
  } else if (mx_index == 2) {
    mx = P->LRGB_;
  } else {
    mx.v1.x = (int16_t)(-(P->RGBC_.r << 4));
    mx.v1.y = (int16_t)(P->RGBC_.r << 4);
    mx.v1.z = P->IR_[0];
    mx.v2.x = mx.v2.y = mx.v2.z = P->RT_.v1.z;
    mx.v3.x = mx.v3.y = mx.v3.z = P->RT_.v2.y;
  }

  if (mv_index == 0) {
    vx = P->V_[0];
  } else if (mv_index == 1) {
    vx = P->V_[1];
  } else if (mv_index == 2) {
    vx = P->V_[2];
  } else {
    vx.x = P->IR_[1];
    vx.y = P->IR_[2];
    vx.z = P->IR_[3];
  }

  if (tv_index == 0) {
    tx = P->TRX_;
    ty = P->TRY_;
    tz = P->TRZ_;
  } else if (tv_index == 1) {
    tx = P->RBK_;
    ty = P->GBK_;
    tz = P->BBK_;
  } else if (tv_index == 2) {
    tx = P->RFC_;
    ty = P->GFC_;
    tz = P->BFC_;

    int64_t mac1 = set_mac(P, 1, tx * 0x1000 + mx.v1.x * vx.x);
    int64_t mac2 = set_mac(P, 2, ty * 0x1000 + mx.v2.x * vx.x);
    int64_t mac3 = set_mac(P, 3, tz * 0x1000 + mx.v3.x * vx.x);

    set_ir(P, 1, (int)(mac1 >> P->sf_), false);
    set_ir(P, 2, (int)(mac2 >> P->sf_), false);
    set_ir(P, 3, (int)(mac3 >> P->sf_), false);

    mac1 = set_mac(P, 1, set_mac(P, 1, (int64_t)(mx.v1.y) * vx.y) +
                          (int64_t)(mx.v1.z) * vx.z);
    mac2 = set_mac(P, 2, set_mac(P, 2, (int64_t)(mx.v2.y) * vx.y) +
                          (int64_t)(mx.v2.z) * vx.z);
    mac3 = set_mac(P, 3, set_mac(P, 3, (int64_t)(mx.v3.y) * vx.y) +
                          (int64_t)(mx.v3.z) * vx.z);

    P->MAC1_ = (int32_t)(mac1 >> P->sf_);
    P->MAC2_ = (int32_t)(mac2 >> P->sf_);
    P->MAC3_ = (int32_t)(mac3 >> P->sf_);

    P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
    P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
    P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);
    return;
  }

  P->MAC1_ = (int32_t)(
      set_mac(P, 1, set_mac(P, 1, set_mac(P, 1, tx * 0x1000 + mx.v1.x * vx.x) +
                                    (int64_t)(mx.v1.y) * vx.y) +
                        (int64_t)(mx.v1.z) * vx.z) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, set_mac(P, 2, set_mac(P, 2, ty * 0x1000 + mx.v2.x * vx.x) +
                                    (int64_t)(mx.v2.y) * vx.y) +
                        (int64_t)(mx.v2.z) * vx.z) >>
      P->sf_);
  P->MAC3_ = (int32_t)(
      set_mac(P, 3, set_mac(P, 3, set_mac(P, 3, tz * 0x1000 + mx.v3.x * vx.x) +
                                    (int64_t)(mx.v3.y) * vx.y) +
                        (int64_t)(mx.v3.z) * vx.z) >>
      P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);
}
static void gpl(psx_gte *P) {
  const int64_t mac1 = (int64_t)(P->MAC1_) << P->sf_;
  const int64_t mac2 = (int64_t)(P->MAC2_) << P->sf_;
  const int64_t mac3 = (int64_t)(P->MAC3_) << P->sf_;

  P->MAC1_ = (int32_t)(set_mac(P, 1, P->IR_[1] * P->IR_[0] + mac1) >> P->sf_);
  P->MAC2_ = (int32_t)(set_mac(P, 2, P->IR_[2] * P->IR_[0] + mac2) >> P->sf_);
  P->MAC3_ = (int32_t)(set_mac(P, 3, P->IR_[3] * P->IR_[0] + mac3) >> P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;
}
static void gpf(psx_gte *P) {
  // Match ProjectPSX cast/shift precedence: (int)setMAC(...) >> sf
  P->MAC1_ = (int32_t)(set_mac(P, 1, P->IR_[1] * P->IR_[0])) >> P->sf_;
  P->MAC2_ = (int32_t)(set_mac(P, 2, P->IR_[2] * P->IR_[0])) >> P->sf_;
  P->MAC3_ = (int32_t)(set_mac(P, 3, P->IR_[3] * P->IR_[0])) >> P->sf_;

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;
}
static void ncdt(psx_gte *P) {
  ncds(P, 0);
  ncds(P, 1);
  ncds(P, 2);
}
static void op(psx_gte *P) {
  const int16_t d1 = P->RT_.v1.x;
  const int16_t d2 = P->RT_.v2.y;
  const int16_t d3 = P->RT_.v3.z;

  P->MAC1_ = (int32_t)(set_mac(P, 1, ((P->IR_[3] * d2) - (P->IR_[2] * d3)) >> P->sf_));
  P->MAC2_ = (int32_t)(set_mac(P, 2, ((P->IR_[1] * d3) - (P->IR_[3] * d1)) >> P->sf_));
  P->MAC3_ = (int32_t)(set_mac(P, 3, ((P->IR_[2] * d1) - (P->IR_[1] * d2)) >> P->sf_));

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);
}
static void sqr(psx_gte *P) {
  P->MAC1_ = (int32_t)(set_mac(P, 1, (P->IR_[1] * P->IR_[1]) >> P->sf_));
  P->MAC2_ = (int32_t)(set_mac(P, 2, (P->IR_[2] * P->IR_[2]) >> P->sf_));
  P->MAC3_ = (int32_t)(set_mac(P, 3, (P->IR_[3] * P->IR_[3]) >> P->sf_));

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);
}
static void avsz3(psx_gte *P) {
  const int64_t avsz3 = (int64_t)(P->ZSF3_) * (P->SZ_[1] + P->SZ_[2] + P->SZ_[3]);
  P->MAC0_ = (int32_t)(set_mac0(P, avsz3));
  P->OTZ_ = set_sz3(P, avsz3 >> 12);
}
static void avsz4(psx_gte *P) {
  const int64_t avsz4 = (int64_t)(P->ZSF4_) * (P->SZ_[0] + P->SZ_[1] + P->SZ_[2] + P->SZ_[3]);
  P->MAC0_ = (int32_t)(set_mac0(P, avsz4));
  P->OTZ_ = set_sz3(P, avsz4 >> 12);
}
static void ncds(psx_gte *P, int r) {
  P->MAC1_ = (int32_t)(
      set_mac(P, 1, (int64_t)(P->LM_.v1.x) * P->V_[r].x + P->LM_.v1.y * P->V_[r].y + P->LM_.v1.z * P->V_[r].z) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, (int64_t)(P->LM_.v2.x) * P->V_[r].x + P->LM_.v2.y * P->V_[r].y + P->LM_.v2.z * P->V_[r].z) >>
      P->sf_);
  P->MAC3_ = (int32_t)(
      set_mac(P, 3, (int64_t)(P->LM_.v3.x) * P->V_[r].x + P->LM_.v3.y * P->V_[r].y + P->LM_.v3.z * P->V_[r].z) >>
      P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->MAC1_ = (int32_t)(
      set_mac(P, 1, set_mac(P, 1, set_mac(P, 1, (int64_t)(P->RBK_) * 0x1000 + P->LRGB_.v1.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v1.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v1.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, set_mac(P, 2, set_mac(P, 2, (int64_t)(P->GBK_) * 0x1000 + P->LRGB_.v2.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v2.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v2.z) * P->IR_[3]) >>
      P->sf_);
  P->MAC3_ = (int32_t)(
      set_mac(P, 3, set_mac(P, 3, set_mac(P, 3, (int64_t)(P->BBK_) * 0x1000 + P->LRGB_.v3.x * P->IR_[1]) +
                                    (int64_t)(P->LRGB_.v3.y) * P->IR_[2]) +
                        (int64_t)(P->LRGB_.v3.z) * P->IR_[3]) >>
      P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);

  P->MAC1_ = (int32_t)(set_mac(P, 1, ((int64_t)(P->RGBC_.r) * P->IR_[1]) << 4));
  P->MAC2_ = (int32_t)(set_mac(P, 2, ((int64_t)(P->RGBC_.g) * P->IR_[2]) << 4));
  P->MAC3_ = (int32_t)(set_mac(P, 3, ((int64_t)(P->RGBC_.b) * P->IR_[3]) << 4));

  interpolate_color(P, P->MAC1_, P->MAC2_, P->MAC3_);

  P->RGB_[0] = P->RGB_[1];
  P->RGB_[1] = P->RGB_[2];
  P->RGB_[2].r = set_rgb(P, 1, P->MAC1_ >> 4);
  P->RGB_[2].g = set_rgb(P, 2, P->MAC2_ >> 4);
  P->RGB_[2].b = set_rgb(P, 3, P->MAC3_ >> 4);
  P->RGB_[2].c = P->RGBC_.c;
}
static void interpolate_color(psx_gte *P, int mac1, int mac2, int mac3) {
  P->MAC1_ = (int32_t)(set_mac(P, 1, ((int64_t)(P->RFC_) << 12) - mac1) >> P->sf_);
  P->MAC2_ = (int32_t)(set_mac(P, 2, ((int64_t)(P->GFC_) << 12) - mac2) >> P->sf_);
  P->MAC3_ = (int32_t)(set_mac(P, 3, ((int64_t)(P->BFC_) << 12) - mac3) >> P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, false);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, false);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, false);

  P->MAC1_ = (int32_t)(set_mac(P, 1, ((int64_t)(P->IR_[1]) * P->IR_[0]) + mac1) >> P->sf_);
  P->MAC2_ = (int32_t)(set_mac(P, 2, ((int64_t)(P->IR_[2]) * P->IR_[0]) + mac2) >> P->sf_);
  P->MAC3_ = (int32_t)(set_mac(P, 3, ((int64_t)(P->IR_[3]) * P->IR_[0]) + mac3) >> P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  P->IR_[3] = set_ir(P, 3, P->MAC3_, P->lm_);
}
static void nclip(psx_gte *P) {
  P->MAC0_ = (int32_t)(set_mac0(P, 
      (int64_t)(P->SXY_[0].x) * P->SXY_[1].y + (int64_t)(P->SXY_[1].x) * P->SXY_[2].y +
      (int64_t)(P->SXY_[2].x) * P->SXY_[0].y - (int64_t)(P->SXY_[0].x) * P->SXY_[2].y -
      (int64_t)(P->SXY_[1].x) * P->SXY_[0].y - (int64_t)(P->SXY_[2].x) * P->SXY_[1].y));
}
static void rtpt(psx_gte *P) {
  rtps(P, 0, false);
  rtps(P, 1, false);
  rtps(P, 2, true);
}
static void rtps(psx_gte *P, int r, bool set_mac0_flag) {
  P->MAC1_ = (int32_t)(
      set_mac(P, 1, set_mac(P, 1, set_mac(P, 1, (int64_t)(P->TRX_) * 0x1000 + P->RT_.v1.x * P->V_[r].x) +
                                    (int64_t)(P->RT_.v1.y) * P->V_[r].y) +
                        (int64_t)(P->RT_.v1.z) * P->V_[r].z) >>
      P->sf_);
  P->MAC2_ = (int32_t)(
      set_mac(P, 2, set_mac(P, 2, set_mac(P, 2, (int64_t)(P->TRY_) * 0x1000 + P->RT_.v2.x * P->V_[r].x) +
                                    (int64_t)(P->RT_.v2.y) * P->V_[r].y) +
                        (int64_t)(P->RT_.v2.z) * P->V_[r].z) >>
      P->sf_);
  const int64_t mac3 =
      set_mac(P, 3, set_mac(P, 3, set_mac(P, 3, (int64_t)(P->TRZ_) * 0x1000 + P->RT_.v3.x * P->V_[r].x) +
                                    (int64_t)(P->RT_.v3.y) * P->V_[r].y) +
                        (int64_t)(P->RT_.v3.z) * P->V_[r].z);
  P->MAC3_ = (int32_t)(mac3 >> P->sf_);

  P->IR_[1] = set_ir(P, 1, P->MAC1_, P->lm_);
  P->IR_[2] = set_ir(P, 2, P->MAC2_, P->lm_);
  set_ir(P, 3, (int)(mac3 >> 12), false);
  P->IR_[3] = (int16_t)(
      CLAMP(P->MAC3_, P->lm_ ? 0 : -0x8000, 0x7FFF));

  P->SZ_[0] = P->SZ_[1];
  P->SZ_[1] = P->SZ_[2];
  P->SZ_[2] = P->SZ_[3];
  P->SZ_[3] = set_sz3(P, mac3 >> 12);

  int64_t n;
  if (P->H_ < P->SZ_[3] * 2u) {
    // BitOperations.LeadingZeroCount on the zero-extended ushort.
    const int z = (P->SZ_[3] == 0) ? 16 : (__builtin_clz((unsigned)(P->SZ_[3])) - 16);
    n = (int64_t)(P->H_) << z;
    uint32_t d = (uint32_t)(P->SZ_[3]) << z;
    uint16_t u = (uint16_t)(kUnrTable[(int)((d - 0x7FC0u) >> 7)] + 0x101);
    d = (0x2000080u - (d * u)) >> 8;
    d = (0x0000080u + (d * u)) >> 8;
    n = (int)(MIN(0x1FFFF, ((n * d) + 0x8000) >> 16));
  } else {
    P->FLAG_ |= 1u << 17;
    n = 0x1FFFF;
  }

  const int x = (int)(set_mac0(P, n * P->IR_[1] + P->OFX_) >> 16);
  const int y = (int)(set_mac0(P, n * P->IR_[2] + P->OFY_) >> 16);

  P->SXY_[0] = P->SXY_[1];
  P->SXY_[1] = P->SXY_[2];
  P->SXY_[2].x = set_sxy(P, 1, x);
  P->SXY_[2].y = set_sxy(P, 2, y);

  if (set_mac0_flag) {
    const int64_t mac0 = set_mac0(P, n * P->DQA_ + P->DQB_);
    P->MAC0_ = (int32_t)(mac0);
    P->IR_[0] = set_ir0(P, mac0 >> 12);
  }
}
static int16_t set_ir0(psx_gte *P, int64_t value) {
  if (value < 0) {
    P->FLAG_ |= 0x1000;
    return 0;
  }
  if (value > 0x1000) {
    P->FLAG_ |= 0x1000;
    return 0x1000;
  }
  return (int16_t)(value);
}
static int16_t set_sxy(psx_gte *P, int i, int value) {
  if (value < -0x400) {
    P->FLAG_ |= (uint32_t)(0x4000 >> (i - 1));
    return -0x400;
  }
  if (value > 0x3FF) {
    P->FLAG_ |= (uint32_t)(0x4000 >> (i - 1));
    return 0x3FF;
  }
  return (int16_t)(value);
}
static uint16_t set_sz3(psx_gte *P, int64_t value) {
  if (value < 0) {
    P->FLAG_ |= 0x40000;
    return 0;
  }
  if (value > 0xFFFF) {
    P->FLAG_ |= 0x40000;
    return 0xFFFF;
  }
  return (uint16_t)(value);
}
static uint8_t set_rgb(psx_gte *P, int i, int value) {
  if (value < 0) {
    P->FLAG_ |= (uint32_t)(0x200000) >> (i - 1);
    return 0;
  }
  if (value > 0xFF) {
    P->FLAG_ |= (uint32_t)(0x200000) >> (i - 1);
    return 0xFF;
  }
  return (uint8_t)(value);
}
static int16_t set_ir(psx_gte *P, int i, int value, bool lm_flag) {
  if (lm_flag && value < 0) {
    P->FLAG_ |= (uint32_t)(0x1000000 >> (i - 1));
    return 0;
  }
  if (!lm_flag && value < -0x8000) {
    P->FLAG_ |= (uint32_t)(0x1000000 >> (i - 1));
    return -0x8000;
  }
  if (value > 0x7FFF) {
    P->FLAG_ |= (uint32_t)(0x1000000 >> (i - 1));
    return 0x7FFF;
  }
  return (int16_t)(value);
}
static int64_t set_mac0(psx_gte *P, int64_t value) {
  if (value < -0x80000000LL) {
    P->FLAG_ |= 0x8000;
  } else if (value > 0x7FFFFFFFLL) {
    P->FLAG_ |= 0x10000;
  }
  return value;
}
static int64_t set_mac(psx_gte *P, int i, int64_t value) {
  if (value < -0x80000000000LL) {
    P->FLAG_ |= (uint32_t)(0x8000000 >> (i - 1));
  } else if (value > 0x7FFFFFFFFFFLL) {
    P->FLAG_ |= (uint32_t)(0x40000000 >> (i - 1));
  }
  return (value << 20) >> 20;
}
static uint8_t saturate_rgb(psx_gte *P, int value) {
  if (value < 0x00) return 0x00;
  if (value > 0x1F) return 0x1F;
  return (uint8_t)(value);
}
static int leading_count(psx_gte *P, uint32_t v) {
  const uint32_t sign = v >> 31;
  int count = 0;
  for (int i = 0; i < 32; ++i) {
    if ((v >> 31) != sign) break;
    ++count;
    v <<= 1;
  }
  return count;
}
uint32_t psx_gte_read_data(psx_gte *P, uint32_t fs) {
  switch (fs) {
    case 0: return V3_XY(P->V_[0]);
    case 1: return (uint32_t)((int32_t)(P->V_[0].z));
    case 2: return V3_XY(P->V_[1]);
    case 3: return (uint32_t)((int32_t)(P->V_[1].z));
    case 4: return V3_XY(P->V_[2]);
    case 5: return (uint32_t)((int32_t)(P->V_[2].z));
    case 6: return C_VAL(P->RGBC_);
    case 7: return P->OTZ_;
    case 8: return (uint32_t)((int32_t)(P->IR_[0]));
    case 9: return (uint32_t)((int32_t)(P->IR_[1]));
    case 10: return (uint32_t)((int32_t)(P->IR_[2]));
    case 11: return (uint32_t)((int32_t)(P->IR_[3]));
    case 12: return V2_VAL(P->SXY_[0]);
    case 13: return V2_VAL(P->SXY_[1]);
    case 14:
    case 15: return V2_VAL(P->SXY_[2]);
    case 16: return P->SZ_[0];
    case 17: return P->SZ_[1];
    case 18: return P->SZ_[2];
    case 19: return P->SZ_[3];
    case 20: return C_VAL(P->RGB_[0]);
    case 21: return C_VAL(P->RGB_[1]);
    case 22: return C_VAL(P->RGB_[2]);
    case 23: return P->RES1_;
    case 24: return (uint32_t)(P->MAC0_);
    case 25: return (uint32_t)(P->MAC1_);
    case 26: return (uint32_t)(P->MAC2_);
    case 27: return (uint32_t)(P->MAC3_);
    case 28:
    case 29: {
      // ProjectPSX assigns IRGB on read; keep const-correct by computing.
      return (uint16_t)(
          (saturate_rgb(P, P->IR_[3] / 0x80) << 10) | (saturate_rgb(P, P->IR_[2] / 0x80) << 5) |
          saturate_rgb(P, P->IR_[1] / 0x80));
    }
    case 30: return (uint32_t)(P->LZCS_);
    case 31: return (uint32_t)(P->LZCR_);
    default: return 0xFFFFFFFFu;
  }
}
void psx_gte_write_data(psx_gte *P, uint32_t fs, uint32_t v) {
  switch (fs) {
    case 0: V3_SET_XY(P->V_[0], v); break;
    case 1: P->V_[0].z = (int16_t)(v); break;
    case 2: V3_SET_XY(P->V_[1], v); break;
    case 3: P->V_[1].z = (int16_t)(v); break;
    case 4: V3_SET_XY(P->V_[2], v); break;
    case 5: P->V_[2].z = (int16_t)(v); break;
    case 6: C_SET_VAL(P->RGBC_, v); break;
    case 7: P->OTZ_ = (uint16_t)(v); break;
    case 8: P->IR_[0] = (int16_t)(v); break;
    case 9: P->IR_[1] = (int16_t)(v); break;
    case 10: P->IR_[2] = (int16_t)(v); break;
    case 11: P->IR_[3] = (int16_t)(v); break;
    case 12: V2_SET_VAL(P->SXY_[0], v); break;
    case 13: V2_SET_VAL(P->SXY_[1], v); break;
    case 14: V2_SET_VAL(P->SXY_[2], v); break;
    case 15:
      P->SXY_[0] = P->SXY_[1];
      P->SXY_[1] = P->SXY_[2];
      V2_SET_VAL(P->SXY_[2], v);
      break;
    case 16: P->SZ_[0] = (uint16_t)(v); break;
    case 17: P->SZ_[1] = (uint16_t)(v); break;
    case 18: P->SZ_[2] = (uint16_t)(v); break;
    case 19: P->SZ_[3] = (uint16_t)(v); break;
    case 20: C_SET_VAL(P->RGB_[0], v); break;
    case 21: C_SET_VAL(P->RGB_[1], v); break;
    case 22: C_SET_VAL(P->RGB_[2], v); break;
    case 23: P->RES1_ = v; break;
    case 24: P->MAC0_ = (int32_t)(v); break;
    case 25: P->MAC1_ = (int32_t)(v); break;
    case 26: P->MAC2_ = (int32_t)(v); break;
    case 27: P->MAC3_ = (int32_t)(v); break;
    case 28:
      P->IRGB_ = (uint16_t)(v & 0x7FFF);
      P->IR_[1] = (int16_t)((v & 0x1F) * 0x80);
      P->IR_[2] = (int16_t)(((v >> 5) & 0x1F) * 0x80);
      P->IR_[3] = (int16_t)(((v >> 10) & 0x1F) * 0x80);
      break;
    case 29: break;
    case 30:
      P->LZCS_ = (int32_t)(v);
      P->LZCR_ = leading_count(P, v);
      break;
    case 31: break;
    default: break;
  }
}
uint32_t psx_gte_read_control(psx_gte *P, uint32_t fs) {
  switch (fs) {
    case 0: return V3_XY(P->RT_.v1);
    case 1: return (uint16_t)(P->RT_.v1.z) | ((uint32_t)(P->RT_.v2.x) << 16);
    case 2: return (uint16_t)(P->RT_.v2.y) | ((uint32_t)(P->RT_.v2.z) << 16);
    case 3: return V3_XY(P->RT_.v3);
    case 4: return (uint32_t)((int32_t)(P->RT_.v3.z));
    case 5: return (uint32_t)(P->TRX_);
    case 6: return (uint32_t)(P->TRY_);
    case 7: return (uint32_t)(P->TRZ_);
    case 8: return V3_XY(P->LM_.v1);
    case 9: return (uint16_t)(P->LM_.v1.z) | ((uint32_t)(P->LM_.v2.x) << 16);
    case 10: return (uint16_t)(P->LM_.v2.y) | ((uint32_t)(P->LM_.v2.z) << 16);
    case 11: return V3_XY(P->LM_.v3);
    case 12: return (uint32_t)((int32_t)(P->LM_.v3.z));
    case 13: return (uint32_t)(P->RBK_);
    case 14: return (uint32_t)(P->GBK_);
    case 15: return (uint32_t)(P->BBK_);
    case 16: return V3_XY(P->LRGB_.v1);
    case 17: return (uint16_t)(P->LRGB_.v1.z) | ((uint32_t)(P->LRGB_.v2.x) << 16);
    case 18: return (uint16_t)(P->LRGB_.v2.y) | ((uint32_t)(P->LRGB_.v2.z) << 16);
    case 19: return V3_XY(P->LRGB_.v3);
    case 20: return (uint32_t)((int32_t)(P->LRGB_.v3.z));
    case 21: return (uint32_t)(P->RFC_);
    case 22: return (uint32_t)(P->GFC_);
    case 23: return (uint32_t)(P->BFC_);
    case 24: return (uint32_t)(P->OFX_);
    case 25: return (uint32_t)(P->OFY_);
    case 26: return (uint32_t)((int16_t)(P->H_));
    case 27: return (uint32_t)(P->DQA_);
    case 28: return (uint32_t)(P->DQB_);
    case 29: return (uint32_t)(P->ZSF3_);
    case 30: return (uint32_t)(P->ZSF4_);
    case 31: return P->FLAG_;
    default: return 0xFFFFFFFFu;
  }
}
void psx_gte_write_control(psx_gte *P, uint32_t fs, uint32_t v) {
  switch (fs) {
    case 0: V3_SET_XY(P->RT_.v1, v); break;
    case 1:
      P->RT_.v1.z = (int16_t)(v);
      P->RT_.v2.x = (int16_t)(v >> 16);
      break;
    case 2:
      P->RT_.v2.y = (int16_t)(v);
      P->RT_.v2.z = (int16_t)(v >> 16);
      break;
    case 3: V3_SET_XY(P->RT_.v3, v); break;
    case 4: P->RT_.v3.z = (int16_t)(v); break;
    case 5: P->TRX_ = (int32_t)(v); break;
    case 6: P->TRY_ = (int32_t)(v); break;
    case 7: P->TRZ_ = (int32_t)(v); break;
    case 8: V3_SET_XY(P->LM_.v1, v); break;
    case 9:
      P->LM_.v1.z = (int16_t)(v);
      P->LM_.v2.x = (int16_t)(v >> 16);
      break;
    case 10:
      P->LM_.v2.y = (int16_t)(v);
      P->LM_.v2.z = (int16_t)(v >> 16);
      break;
    case 11: V3_SET_XY(P->LM_.v3, v); break;
    case 12: P->LM_.v3.z = (int16_t)(v); break;
    case 13: P->RBK_ = (int32_t)(v); break;
    case 14: P->GBK_ = (int32_t)(v); break;
    case 15: P->BBK_ = (int32_t)(v); break;
    case 16: V3_SET_XY(P->LRGB_.v1, v); break;
    case 17:
      P->LRGB_.v1.z = (int16_t)(v);
      P->LRGB_.v2.x = (int16_t)(v >> 16);
      break;
    case 18:
      P->LRGB_.v2.y = (int16_t)(v);
      P->LRGB_.v2.z = (int16_t)(v >> 16);
      break;
    case 19: V3_SET_XY(P->LRGB_.v3, v); break;
    case 20: P->LRGB_.v3.z = (int16_t)(v); break;
    case 21: P->RFC_ = (int32_t)(v); break;
    case 22: P->GFC_ = (int32_t)(v); break;
    case 23: P->BFC_ = (int32_t)(v); break;
    case 24: P->OFX_ = (int32_t)(v); break;
    case 25: P->OFY_ = (int32_t)(v); break;
    case 26: P->H_ = (uint16_t)(v); break;
    case 27: P->DQA_ = (int16_t)(v); break;
    case 28: P->DQB_ = (int32_t)(v); break;
    case 29: P->ZSF3_ = (int16_t)(v); break;
    case 30: P->ZSF4_ = (int16_t)(v); break;
    case 31:
      P->FLAG_ = v & 0x7FFFF000u;
      if ((P->FLAG_ & 0x7F87E000u) != 0) {
        P->FLAG_ |= 0x80000000u;
      }
      break;
    default: break;
  }
}

