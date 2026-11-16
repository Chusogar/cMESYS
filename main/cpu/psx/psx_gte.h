// psx_gte.h - the PlayStation's Geometry Transformation Engine (COP2): a C11
// port of dsp-cpp's cpu/psx_gte (from ProjectPSX's GTE.cs, MIT License,
// Copyright (c) 2019 Pedro Cortés).
#ifndef PSX_GTE_H
#define PSX_GTE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct { int16_t x, y, z; } gte_vec3;
typedef struct { int16_t x, y; } gte_vec2;
typedef struct { uint8_t r, g, b, c; } gte_color;
typedef struct { gte_vec3 v1, v2, v3; } gte_matrix;

#define V3_XY(v) ((uint32_t)(uint16_t)(v).x | ((uint32_t)(uint16_t)(v).y << 16))
#define V3_SET_XY(v, val) do { const uint32_t v_ = (val); (v).x = (int16_t)v_; (v).y = (int16_t)(v_ >> 16); } while (0)
#define V2_VAL(v) ((uint32_t)(uint16_t)(v).x | ((uint32_t)(uint16_t)(v).y << 16))
#define V2_SET_VAL(v, val) do { const uint32_t v_ = (val); (v).x = (int16_t)v_; (v).y = (int16_t)(v_ >> 16); } while (0)
#define C_VAL(col) ((uint32_t)(col).r | ((uint32_t)(col).g << 8) | ((uint32_t)(col).b << 16) | ((uint32_t)(col).c << 24))
#define C_SET_VAL(col, val) do { const uint32_t v_ = (val); (col).r = (uint8_t)v_; (col).g = (uint8_t)(v_ >> 8); (col).b = (uint8_t)(v_ >> 16); (col).c = (uint8_t)(v_ >> 24); } while (0)

typedef struct {
    gte_vec3 V_[3];
    gte_color RGBC_;
    uint16_t OTZ_;
    int16_t IR_[4];
    gte_vec2 SXY_[4];
    uint16_t SZ_[4];
    gte_color RGB_[3];
    uint32_t RES1_;
    int32_t MAC0_, MAC1_, MAC2_, MAC3_;
    uint16_t IRGB_;
    int32_t LZCS_, LZCR_;
    gte_matrix RT_, LM_, LRGB_;
    int32_t TRX_, TRY_, TRZ_, RBK_, GBK_, BBK_, RFC_, GFC_, BFC_, OFX_, OFY_, DQB_;
    uint16_t H_;
    int16_t ZSF3_, ZSF4_, DQA_;
    uint32_t FLAG_;
    int sf_;
    bool lm_;
    uint32_t current_command_;
} psx_gte;

void psx_gte_reset(psx_gte *P);
void psx_gte_execute(psx_gte *P, uint32_t command);
uint32_t psx_gte_read_data(psx_gte *P, uint32_t fs);
void psx_gte_write_data(psx_gte *P, uint32_t fs, uint32_t value);
uint32_t psx_gte_read_control(psx_gte *P, uint32_t fs);
void psx_gte_write_control(psx_gte *P, uint32_t fs, uint32_t value);
#endif
