// upd7801.c - NEC uPD7801 (the Epoch Super Cassette Vision's CPU): a C11
// port of dsp-cpp's cpu/upd7801 (from the DSP emulator's upd7810.pas with
// cpu_type = CPU_7801); see upd7801.h.
#include <string.h>
#include "upd7801.h"

#define kPortA 0
#define kPortB 1
#define kPortC 2
#define kIntf1 0
#define kIntf2 1
#define kIntf0 2
#define kIntf0Bit 0x2000
#define kIntFt0Bit 0x0002
#define kIntf1Bit 0x0008
#define kIntf2Bit 0x0010
#define kIntfStBit 0x0400
#define SWAP(a, b) do { __typeof__(a) t_ = (a); (a) = (b); (b) = t_; } while (0)

static const uint8_t kOpSize[256] = {
      1,   1,   1,   1,   3,   3,   1,   2,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   3,   3,   2,   2,   1,   1,   1,   1,   1,   1,   1,   1,
      2,   1,   1,   1,   3,   3,   2,   2,   2,   1,   1,   1,   1,   1,   1,   1,
      2,   1,   1,   1,   3,   3,   2,   2,   2,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   3,   3,   2,   2,   1,   2,   2,   2,   1,   1,   2,   2,
      1,   1,   1,   1,   3,   3,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      1,   1,   1,   2,   1,   3,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      1,   3,   1,   1,   1,   3,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
};

static const uint8_t kOpCycles[256] = {
      4,   6,   7,   7,  10,  16,   4,   7,  11,   4,   4,   4,   4,   4,   4,   4,
      4,   4,   7,   7,  10,  16,   7,   7,  11,   4,   4,   4,   4,   4,   4,   4,
     13,  19,   7,   7,  10,  13,   7,   7,  10,   7,   7,   7,   7,   7,   7,   7,
     13,  13,   7,   7,  10,  13,   7,   7,  10,   7,   7,   7,   7,   7,   7,   7,
      4,   4,   4,   4,  16,  13,   7,   7,   0,  10,  10,  10,   0,   0,  17,  17,
      4,   4,   4,   4,  10,  13,   7,   7,  10,  10,  10,  10,  10,  10,  10,  10,
      0,   4,  15,  13,   0,  13,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,
      0,  13,  19,   4,   0,  13,   7,   7,  16,  16,  16,  16,  16,  16,  16,  16,
     19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,
     19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,
     19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,
     19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,  19,
     13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,
     13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,
     13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,
     13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,
};

static const uint8_t kOp48Size[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
};

static const uint8_t kOp48Cycles[256] = {
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  17,  15,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  17,  15,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  11,  11,  17,  15,
      8,   8,   8,   8,   8,   8,   8,   8,  17,  17,   8,   8,  11,   8,  17,  15,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
};

static const uint8_t kOp4cSize[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
};

static const uint8_t kOp4cCycles[256] = {
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,   8,   8,   8,   8,  10,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
};

static const uint8_t kOp4dSize[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
};

static const uint8_t kOp4dCycles[256] = {
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
     10,  10,  10,  10,  10,  10,  10,  10,  10,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
};

static const uint8_t kOp64Size[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
      2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
      3,   3,   3,   3,   2,   2,   2,   2,   3,   3,   3,   3,   2,   2,   2,   2,
};

static const uint8_t kOp64Cycles[256] = {
      8,   8,   8,   8,   8,   8,   8,   8,  11,   1,   1,  11,  11,  11,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
     11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,   1,  11,  11,
      8,   8,   8,   8,   8,   8,   8,   8,  17,  17,  17,  17,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  17,  17,  17,  17,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
     17,  17,  17,  17,   8,   8,   8,   8,  14,  14,  14,  14,   8,   8,   8,   8,
};

static const uint8_t kOp70Size[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   4,   4,   4,   4,   4,   4,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   4,   4,   4,   4,   4,   4,   4,   4,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
};

static const uint8_t kOp70Cycles[256] = {
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  20,  20,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  20,  20,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  20,  20,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,  20,  20,
      8,  11,  11,  11,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,  17,  17,  17,  17,  17,  17,  17,  17,
      8,   8,   8,   8,   8,   8,   8,   8,  17,  17,  17,  17,  17,  17,  17,  17,
      8,   8,   8,   8,   8,   8,   8,   8,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
      8,  11,  11,  11,  11,  11,  11,  11,   8,  11,  11,  11,  11,  11,  11,  11,
};

static const uint8_t kOp74Size[256] = {
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
      3,   2,   2,   2,   2,   2,   2,   2,   3,   2,   2,   2,   2,   2,   2,   2,
};

static const uint8_t kOp74Cycles[256] = {
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,   8,
      8,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
     14,   8,   8,   8,   8,   8,   8,   8,  14,   8,   8,   8,   8,   8,   8,   8,
};


static inline uint8_t rd(upd7801 *U, uint16_t a) { return U->read(U->user, a); }
static inline void wr(upd7801 *U, uint16_t a, uint8_t v) { U->write(U->user, a, v); }
static inline uint8_t fetch(upd7801 *U) { return rd(U, U->pc_++); }
static inline uint16_t fetch16(upd7801 *U) { const uint8_t lo = fetch(U); return (uint16_t)(lo | (fetch(U) << 8)); }
static inline uint16_t bc(upd7801 *U) { return (uint16_t)(U->c_ | (U->b_ << 8)); }
static inline uint16_t de(upd7801 *U) { return (uint16_t)(U->e_ | (U->d_ << 8)); }
static inline uint16_t hl(upd7801 *U) { return (uint16_t)(U->l_ | (U->h_ << 8)); }
static inline void set_bc(upd7801 *U, uint16_t w) { U->c_ = (uint8_t)w; U->b_ = (uint8_t)(w >> 8); }
static inline void set_de(upd7801 *U, uint16_t w) { U->e_ = (uint8_t)w; U->d_ = (uint8_t)(w >> 8); }
static inline void set_hl(upd7801 *U, uint16_t w) { U->l_ = (uint8_t)w; U->h_ = (uint8_t)(w >> 8); }
static inline uint8_t overlay(upd7801 *U, uint8_t value, uint8_t bit, uint8_t src)
{
    if (U->mcc_ & bit) {
        value = (uint8_t)(value & ~bit);
        if (src & 1) value = (uint8_t)(value | bit);
    }
    return value;
}
static inline void st16(upd7801 *U, uint16_t addr, uint16_t v) { wr(U, addr, (uint8_t)v); wr(U, (uint16_t)(addr + 1), (uint8_t)(v >> 8)); }
static inline uint16_t ld16(upd7801 *U, uint16_t addr) { return (uint16_t)(rd(U, addr) | (rd(U, (uint16_t)(addr + 1)) << 8)); }
static inline uint8_t wa_mem(upd7801 *U) { return rd(U, (uint16_t)((U->v_ << 8) | fetch(U))); }
static inline uint16_t wa(upd7801 *U) { return (uint16_t)((U->v_ << 8) | fetch(U)); }
static inline void push_pc(upd7801 *U) { wr(U, --U->sp_, (uint8_t)(U->pc_ >> 8)); wr(U, --U->sp_, (uint8_t)U->pc_); }
static uint16_t ptr(upd7801 *U, uint8_t op)
{
    switch (op & 7) {
    case 1: return bc(U);
    case 2: return de(U);
    case 3: return hl(U);
    case 4: { const uint16_t a = de(U); set_de(U, (uint16_t)(a + 1)); return a; }
    case 5: { const uint16_t a = hl(U); set_hl(U, (uint16_t)(a + 1)); return a; }
    case 6: { const uint16_t a = de(U); set_de(U, (uint16_t)(a - 1)); return a; }
    case 7: { const uint16_t a = hl(U); set_hl(U, (uint16_t)(a - 1)); return a; }
    default: return 0;
    }
}
static void ana_x_a(upd7801 *U, uint8_t *r);
static void xra_x_a(upd7801 *U, uint8_t *r);
static void ora_x_a(upd7801 *U, uint8_t *r);
static void add_x_a(upd7801 *U, uint8_t *r);
static void adc_x_a(upd7801 *U, uint8_t *r);
static void sub_x_a(upd7801 *U, uint8_t *r);
static void sbb_x_a(upd7801 *U, uint8_t *r);
static void addnc_x_a(upd7801 *U, uint8_t *r);
static void subnb_x_a(upd7801 *U, uint8_t *r);
static void eqa_x_a(upd7801 *U, uint8_t *r);
static void nea_x_a(upd7801 *U, uint8_t *r);
static void lta_x_a(upd7801 *U, uint8_t *r);
static void gta_x_a(upd7801 *U, uint8_t *r);

void upd7801_reset(upd7801 *U) {
    U->pc_ = U->ppc_ = U->sp_ = 0;
    U->v_ = U->a_ = U->b_ = U->c_ = U->d_ = U->e_ = U->h_ = U->l_ = 0;
    U->v2_ = U->a2_ = U->b2_ = U->c2_ = U->d2_ = U->e2_ = U->h2_ = U->l2_ = 0;
    U->ea_ = U->ea2_ = 0;
    U->zf_ = U->cy_ = U->sk_ = U->hc_ = U->l0_ = U->l1_ = U->f1_ = U->f7_ = false;
    U->iff_ = U->iff_pending_ = false;
    U->int1_ = U->int2_ = 0;
    U->irr_ = 0;
    U->mkl_ = 0xFF;
    U->tm_ = 0;
    U->ovc0_ = 0;
    U->to_ = 0;
    U->ma_ = 0;
    U->mb_ = 0xFF;
    U->mc_ = 0xFF;
    U->mcc_ = 0;
    U->pa_in_ = U->pb_in_ = U->pc_in_ = 0;
    U->pa_out_ = U->pb_out_ = U->pc_out_ = 0;
    U->txd_ = U->rdx_ = U->sck_ = U->ci_ = U->co0_ = U->co1_ = 0;
    memset(U->iram, 0, sizeof U->iram);
}

static uint8_t psw(upd7801 *U) {
    uint8_t v = 0;
    if (U->cy_) v |= 0x01;
    if (U->f1_) v |= 0x02;
    if (U->l0_) v |= 0x04;
    if (U->l1_) v |= 0x08;
    if (U->hc_) v |= 0x10;
    if (U->sk_) v |= 0x20;
    if (U->zf_) v |= 0x40;
    if (U->f7_) v |= 0x80;
    return v;
}

static void set_psw(upd7801 *U, uint8_t value) {
    U->cy_ = (value & 0x01) != 0;
    U->f1_ = (value & 0x02) != 0;
    U->l0_ = (value & 0x04) != 0;
    U->l1_ = (value & 0x08) != 0;
    U->hc_ = (value & 0x10) != 0;
    U->sk_ = (value & 0x20) != 0;
    U->zf_ = (value & 0x40) != 0;
    U->f7_ = (value & 0x80) != 0;
}

static void zhc_add(upd7801 *U, uint16_t after, uint16_t before, bool carry) {
    U->zf_ = (after == 0);
    if (after == before) U->cy_ = carry;
    else U->cy_ = after < before;
    U->hc_ = (after & 15) < (before & 15);
}

static void zhc_sub(upd7801 *U, uint16_t after, uint16_t before, bool carry) {
    U->zf_ = (after == 0);
    if (before == after) U->cy_ = carry;
    else U->cy_ = after > before;
    U->hc_ = (after & 15) > (before & 15);
}

static uint8_t read_port(upd7801 *U, int port) {
    uint8_t value = 0xFF;
    if (port == kPortA) {
        value = (U->pa_in_ & U->ma_) | (U->pa_out_ & (uint8_t)(~U->ma_));
    } else if (port == kPortB) {
        if (U->mb_ != 0 && U->port_b_in) U->pb_in_ = U->port_b_in(U->user, U->mb_);
        value = (U->pb_in_ & U->mb_) | (U->pb_out_ & (uint8_t)(~U->mb_));
    } else if (port == kPortC) {
        if (U->mc_ != 0 && U->port_c_in) U->pc_in_ = U->port_c_in(U->user, U->mc_);
        value = (U->pc_in_ & U->mc_) | (U->pc_out_ & (uint8_t)(~U->mc_));
        value = overlay(U, value, 0x01, U->txd_);
        value = overlay(U, value, 0x02, U->rdx_);
        value = overlay(U, value, 0x04, U->sck_);
        value = overlay(U, value, 0x08, U->int2_);
        value = overlay(U, value, 0x10, U->to_);
        value = overlay(U, value, 0x20, U->ci_);
        value = overlay(U, value, 0x40, U->co0_);
        value = overlay(U, value, 0x80, U->co1_);
    }
    return value;
}

static void write_port(upd7801 *U, int port, uint8_t value) {
    if (port == kPortA) {
        U->pa_out_ = value;
        value = (uint8_t)((value & ~U->ma_) | U->ma_);
        if (U->port_a_out) U->port_a_out(U->user, value);
    } else if (port == kPortB) {
        U->pb_out_ = value;
    } else if (port == kPortC) {
        U->pc_out_ = value;
        value = (uint8_t)((value & ~U->mc_) | U->mc_);
        value = overlay(U, value, 0x01, U->txd_);
        value = overlay(U, value, 0x02, U->rdx_);
        value = overlay(U, value, 0x04, U->sck_);
        value = overlay(U, value, 0x08, U->int2_);
        value = overlay(U, value, 0x10, U->to_);
        value = overlay(U, value, 0x20, U->ci_);
        value = overlay(U, value, 0x40, U->co0_);
        value = overlay(U, value, 0x80, U->co1_);
        if (U->port_c_out) U->port_c_out(U->user, value);
    }
}

void upd7801_set_input_line(upd7801 *U, int irqline, int state) {
    const uint8_t asserted = (state == 0) ? 0 : 1;
    if (irqline == kIntf0) {
        if (asserted) U->irr_ |= kIntf0Bit;
        else U->irr_ = (uint16_t)(U->irr_ & ~kIntf0Bit);
    } else if (irqline == kIntf1) {
        if (U->int1_ == 0 && asserted) U->irr_ |= kIntf1Bit;
        U->int1_ = asserted;
    } else if (irqline == kIntf2) {
        if (U->mkl_ & 0x20) {
            if (U->int2_ == 0 && asserted) U->irr_ |= kIntf2Bit;
        } else if (U->int2_ == 1 && asserted == 0) {
            U->irr_ |= kIntf2Bit;
        }
        U->int2_ = asserted;
    }
}

static void take_irq(upd7801 *U) {
    if (!U->iff_) return;
    uint16_t vector = 0;
    if ((U->irr_ & kIntf0Bit) && (U->mkl_ & 0x01) == 0) {
        vector = 0x0004;
        U->irr_ = (uint16_t)(U->irr_ & ~kIntf0Bit);
    }
    if ((U->irr_ & kIntFt0Bit) && (U->mkl_ & 0x02) == 0) {
        vector = 0x0008;
        U->irr_ = (uint16_t)(U->irr_ & ~kIntFt0Bit);
    }
    if ((U->irr_ & kIntf1Bit) && (U->mkl_ & 0x04) == 0) {
        vector = 0x0010;
        U->irr_ = (uint16_t)(U->irr_ & ~kIntf1Bit);
    }
    if ((U->irr_ & kIntf2Bit) && (U->mkl_ & 0x08) == 0) {
        vector = 0x0020;
        U->irr_ = (uint16_t)(U->irr_ & ~kIntf2Bit);
    }
    if ((U->irr_ & kIntfStBit) && (U->mkl_ & 0x10) == 0) {
        vector = 0x0040;
        U->irr_ = (uint16_t)(U->irr_ & ~kIntfStBit);
    }
    if (vector == 0) return;
    wr(U, --U->sp_, psw(U));
    wr(U, --U->sp_, (uint8_t)(U->pc_ >> 8));
    wr(U, --U->sp_, (uint8_t)(U->pc_));
    U->iff_ = U->iff_pending_ = false;
    U->sk_ = U->l0_ = U->l1_ = false;
    U->pc_ = vector;
}

static void handle_timers(upd7801 *U, int cycles) {
    if (U->ovc0_ == 0) return;
    U->ovc0_ -= cycles;
    if (U->ovc0_ <= 0) {
        U->irr_ |= kIntFt0Bit;
        U->to_ = 0;
        U->ovc0_ = 8 * (int)(U->tm_ & 0x0FFF);
    }
}

static uint8_t *reg_by_index(upd7801 *U, int index) {
    switch (index & 7) {
        case 0: return &U->v_;
        case 1: return &U->a_;
        case 2: return &U->b_;
        case 3: return &U->c_;
        case 4: return &U->d_;
        case 5: return &U->e_;
        case 6: return &U->h_;
        default: return &U->l_;
    }
}

static void ani(upd7801 *U, uint8_t *r) {
    (*r) = (uint8_t)((*r) & fetch(U));
    U->zf_ = (*r) == 0;
}
static void xri(upd7801 *U, uint8_t *r) {
    (*r) = (uint8_t)((*r) ^ fetch(U));
    U->zf_ = (*r) == 0;
}
static void ori(upd7801 *U, uint8_t *r) {
    (*r) = (uint8_t)((*r) | fetch(U));
    U->zf_ = (*r) == 0;
}
static void adi(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    (*r) = (uint8_t)((*r) + fetch(U));
    zhc_add(U, (*r), before, false);
}
static void aci(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    (*r) = (uint8_t)((*r) + fetch(U) + (uint8_t)(U->cy_));
    zhc_add(U, (*r), before, U->cy_);
}
static void sui(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    (*r) = (uint8_t)((*r) - fetch(U));
    zhc_sub(U, (*r), before, false);
}
static void sbi(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    const bool carry = U->cy_;
    (*r) = (uint8_t)((*r) - fetch(U) - (uint8_t)(carry));
    zhc_sub(U, (*r), before, carry);
}
static void adinc(upd7801 *U, uint8_t *r) {
    adi(U, &(*r));
    if (!U->cy_) U->sk_ = true;
}
static void suinb(upd7801 *U, uint8_t *r) {
    sui(U, &(*r));
    if (!U->cy_) U->sk_ = true;
}
static void eqi(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    const uint8_t after = (uint8_t)((*r) - fetch(U));
    zhc_sub(U, after, before, false);
    if (U->zf_) U->sk_ = true;
}
static void nei(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    const uint8_t after = (uint8_t)((*r) - fetch(U));
    zhc_sub(U, after, before, false);
    if (!U->zf_) U->sk_ = true;
}
static void lti(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    const uint8_t after = (uint8_t)((*r) - fetch(U));
    zhc_sub(U, after, before, false);
    if (U->cy_) U->sk_ = true;
}
static void gti(upd7801 *U, uint8_t *r) {
    const uint16_t after = (uint16_t)((*r) - fetch(U) - 1);
    zhc_sub(U, after, (*r), false);
    if (!U->cy_) U->sk_ = true;
}
static void oni(upd7801 *U, uint8_t *r) {
    if (((*r) & fetch(U)) != 0) U->sk_ = true;
}
static void offi(upd7801 *U, uint8_t *r) {
    if (((*r) & fetch(U)) == 0) U->sk_ = true;
}

static void add_a(upd7801 *U, uint8_t r) {
    const uint8_t before = U->a_;
    U->a_ = (uint8_t)(U->a_ + r);
    zhc_add(U, U->a_, before, false);
}
static void adc_a(upd7801 *U, uint8_t r) {
    const uint8_t before = U->a_;
    U->a_ = (uint8_t)(U->a_ + r + (uint8_t)(U->cy_));
    zhc_add(U, U->a_, before, U->cy_);
}
static void sub_a(upd7801 *U, uint8_t r) {
    const uint8_t before = U->a_;
    U->a_ = (uint8_t)(U->a_ - r);
    zhc_sub(U, U->a_, before, false);
}
static void sbb_a(upd7801 *U, uint8_t r) {
    const uint8_t before = U->a_;
    const bool carry = U->cy_;
    U->a_ = (uint8_t)(U->a_ - r - (uint8_t)(carry));
    zhc_sub(U, U->a_, before, carry);
}
static void addnc_a(upd7801 *U, uint8_t r) {
    add_a(U, r);
    if (!U->cy_) U->sk_ = true;
}
static void subnb_a(upd7801 *U, uint8_t r) {
    sub_a(U, r);
    if (!U->cy_) U->sk_ = true;
}
static void eqa_a(upd7801 *U, uint8_t r) {
    const uint8_t after = (uint8_t)(U->a_ - r);
    zhc_sub(U, after, U->a_, false);
    if (U->zf_) U->sk_ = true;
}
static void nea_a(upd7801 *U, uint8_t r) {
    const uint8_t after = (uint8_t)(U->a_ - r);
    zhc_sub(U, after, U->a_, false);
    if (!U->zf_) U->sk_ = true;
}
static void lta_a(upd7801 *U, uint8_t r) {
    const uint8_t after = (uint8_t)(U->a_ - r);
    zhc_sub(U, after, U->a_, false);
    if (U->cy_) U->sk_ = true;
}
static void gta_a(upd7801 *U, uint8_t r) {
    const uint16_t after = (uint16_t)(U->a_ - r - 1);
    zhc_sub(U, after, U->a_, false);
    if (!U->cy_) U->sk_ = true;
}
static void ona_a(upd7801 *U, uint8_t r) {
    if ((U->a_ & r) != 0) {
        U->zf_ = false;
        U->sk_ = true;
    } else {
        U->zf_ = true;
    }
}
static void offa_a(upd7801 *U, uint8_t r) {
    if ((U->a_ & r) != 0) U->zf_ = false;
    else {
        U->zf_ = true;
        U->sk_ = true;
    }
}
static void ora_a(upd7801 *U, uint8_t r) {
    U->a_ = (uint8_t)(U->a_ | r);
    U->zf_ = U->a_ == 0;
}
static void xra_a(upd7801 *U, uint8_t r) {
    U->a_ = (uint8_t)(U->a_ ^ r);
    U->zf_ = U->a_ == 0;
}

static void add_x_a(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    (*r) = (uint8_t)((*r) + U->a_);
    zhc_add(U, (*r), before, false);
}
static void adc_x_a(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    (*r) = (uint8_t)((*r) + U->a_ + (uint8_t)(U->cy_));
    zhc_add(U, (*r), before, U->cy_);
}
static void sub_x_a(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    (*r) = (uint8_t)((*r) - U->a_);
    zhc_sub(U, (*r), before, false);
}
static void sbb_x_a(upd7801 *U, uint8_t *r) {
    const uint8_t before = (*r);
    const bool carry = U->cy_;
    (*r) = (uint8_t)((*r) - U->a_ - (uint8_t)(carry));
    zhc_sub(U, (*r), before, carry);
}
static void addnc_x_a(upd7801 *U, uint8_t *r) {
    add_x_a(U, &(*r));
    if (!U->cy_) U->sk_ = true;
}
static void subnb_x_a(upd7801 *U, uint8_t *r) {
    sub_x_a(U, &(*r));
    if (!U->cy_) U->sk_ = true;
}
static void eqa_x_a(upd7801 *U, uint8_t *r) {
    const uint8_t after = (uint8_t)((*r) - U->a_);
    zhc_sub(U, after, (*r), false);
    if (U->zf_) U->sk_ = true;
}
static void nea_x_a(upd7801 *U, uint8_t *r) {
    const uint8_t after = (uint8_t)((*r) - U->a_);
    zhc_sub(U, after, (*r), false);
    if (!U->zf_) U->sk_ = true;
}
static void lta_x_a(upd7801 *U, uint8_t *r) {
    const uint8_t after = (uint8_t)((*r) - U->a_);
    zhc_sub(U, after, (*r), false);
    if (U->cy_) U->sk_ = true;
}
static void gta_x_a(upd7801 *U, uint8_t *r) {
    const uint16_t after = (uint16_t)((*r) - U->a_ - 1);
    zhc_sub(U, after, (*r), false);
    if (!U->cy_) U->sk_ = true;
}
static void ana_x_a(upd7801 *U, uint8_t *r) {
    (*r) = (uint8_t)((*r) & U->a_);
    U->zf_ = (*r) == 0;
}
static void ora_x_a(upd7801 *U, uint8_t *r) {
    (*r) = (uint8_t)((*r) | U->a_);
    U->zf_ = (*r) == 0;
}
static void xra_x_a(upd7801 *U, uint8_t *r) {
    U->a_ = (uint8_t)((*r) ^ U->a_);
    U->zf_ = (*r) == 0;
}

static inline int prefix_size(upd7801 *U, uint8_t op, uint8_t op2) {
    switch (op) {
        case 0x48: return kOp48Size[op2];
        case 0x4C: return kOp4cSize[op2];
        case 0x4D: return kOp4dSize[op2];
        case 0x60: return 2;
        case 0x64: return kOp64Size[op2];
        case 0x70: return kOp70Size[op2];
        case 0x74: return kOp74Size[op2];
        default: return kOpSize[op];
    }
}

static inline int prefix_cycles(upd7801 *U, uint8_t op, uint8_t op2) {
    int cycles = 0;
    switch (op) {
        case 0x48: cycles = kOp48Cycles[op2]; break;
        case 0x4C: cycles = kOp4cCycles[op2]; break;
        case 0x4D: cycles = kOp4dCycles[op2]; break;
        case 0x60: cycles = 8; break;
        case 0x64: cycles = kOp64Cycles[op2]; break;
        case 0x70: cycles = kOp70Cycles[op2]; break;
        case 0x74: cycles = kOp74Cycles[op2]; break;
        default: cycles = kOpCycles[op]; break;
    }
    return cycles ? cycles : 8;
}

static void skip_rest(upd7801 *U, uint8_t op) {
    if (op == 0x48 || op == 0x4C || op == 0x4D || op == 0x60 || op == 0x64 ||
        op == 0x70 || op == 0x74) {
        const uint8_t op2 = fetch(U);
        const int extra = prefix_size(U, op, op2) - 2;
        for (int i = 0; i < extra; i++) fetch(U);
        return;
    }
    const int extra = kOpSize[op] - 1;
    for (int i = 0; i < extra; i++) fetch(U);
}

static void op_48(upd7801 *U) {
    const uint8_t op = fetch(U);
    switch (op) {
        case 0x00:
            if (U->irr_ & kIntf0Bit) U->sk_ = true;
            U->irr_ = (uint16_t)(U->irr_ & ~kIntf0Bit);
            break;
        case 0x01:
            if (U->irr_ & kIntFt0Bit) U->sk_ = true;
            U->irr_ = (uint16_t)(U->irr_ & ~kIntFt0Bit);
            break;
        case 0x02:
            if (U->irr_ & kIntf1Bit) U->sk_ = true;
            U->irr_ = (uint16_t)(U->irr_ & ~kIntf1Bit);
            break;
        case 0x03:
            if (U->irr_ & kIntf2Bit) U->sk_ = true;
            U->irr_ = (uint16_t)(U->irr_ & ~kIntf2Bit);
            break;
        case 0x04:
            if (U->irr_ & kIntfStBit) U->sk_ = true;
            U->irr_ = (uint16_t)(U->irr_ & ~kIntfStBit);
            break;
        case 0x0A:
            if (U->cy_) U->sk_ = true;
            break;
        case 0x0C:
            if (U->zf_) U->sk_ = true;
            break;
        case 0x0E:
            wr(U, --U->sp_, U->v_);
            wr(U, --U->sp_, U->a_);
            break;
        case 0x0F:
            U->a_ = rd(U, U->sp_++);
            U->v_ = rd(U, U->sp_++);
            break;
        case 0x1A:
            if (!U->cy_) U->sk_ = true;
            break;
        case 0x1C:
            if (!U->zf_) U->sk_ = true;
            break;
        case 0x1E:
            wr(U, --U->sp_, U->b_);
            wr(U, --U->sp_, U->c_);
            break;
        case 0x1F:
            U->c_ = rd(U, U->sp_++);
            U->b_ = rd(U, U->sp_++);
            break;
        case 0x20:
            U->iff_pending_ = true;
            break;
        case 0x24:
            U->iff_ = U->iff_pending_ = false;
            break;
        case 0x2A:
            U->cy_ = false;
            break;
        case 0x2B:
            U->cy_ = true;
            break;
        case 0x2E:
            wr(U, --U->sp_, U->d_);
            wr(U, --U->sp_, U->e_);
            break;
        case 0x2F:
            U->e_ = rd(U, U->sp_++);
            U->d_ = rd(U, U->sp_++);
            break;
        case 0x30: {
            const uint8_t carry = (uint8_t)(U->cy_);
            U->cy_ = (U->a_ & 0x80) != 0;
            U->a_ = (uint8_t)((U->a_ << 1) | carry);
            break;
        }
        case 0x31: {
            const uint8_t carry = (uint8_t)(U->cy_) << 7;
            U->cy_ = (U->a_ & 1) != 0;
            U->a_ = (uint8_t)((U->a_ >> 1) | carry);
            break;
        }
        case 0x32: {
            const uint8_t carry = (uint8_t)(U->cy_);
            U->cy_ = (U->c_ & 0x80) != 0;
            U->c_ = (uint8_t)((U->c_ << 1) | carry);
            break;
        }
        case 0x33: {
            const uint8_t carry = (uint8_t)(U->cy_) << 7;
            U->cy_ = (U->c_ & 1) != 0;
            U->c_ = (uint8_t)((U->c_ >> 1) | carry);
            break;
        }
        case 0x34:
            U->cy_ = (U->a_ & 0x80) != 0;
            U->a_ = (uint8_t)(U->a_ << 1);
            break;
        case 0x35:
            U->cy_ = (U->a_ & 1) != 0;
            U->a_ = (uint8_t)(U->a_ >> 1);
            break;
        case 0x36:
            U->cy_ = (U->c_ & 0x80) != 0;
            U->c_ = (uint8_t)(U->c_ << 1);
            break;
        case 0x37:
            U->cy_ = (U->c_ & 1) != 0;
            U->c_ = (uint8_t)(U->c_ >> 1);
            break;
        case 0x38: {
            const uint8_t m = rd(U, hl(U));
            const uint8_t n = (uint8_t)((m << 4) | (U->a_ & 0x0F));
            U->a_ = (uint8_t)((U->a_ & 0xF0) | (m >> 4));
            wr(U, hl(U), n);
            break;
        }
        case 0x39: {
            const uint8_t m = rd(U, hl(U));
            const uint8_t n = (uint8_t)((U->a_ << 4) | (m >> 4));
            U->a_ = (uint8_t)((U->a_ & 0xF0) | (m & 0x0F));
            wr(U, hl(U), n);
            break;
        }
        case 0x3E:
            wr(U, --U->sp_, U->h_);
            wr(U, --U->sp_, U->l_);
            break;
        case 0x3F:
            U->l_ = rd(U, U->sp_++);
            U->h_ = rd(U, U->sp_++);
            break;
        default:
            break;
    }
}

static void op_4c(upd7801 *U) {
    const uint8_t op = fetch(U);
    switch (op) {
        case 0xC0: U->a_ = read_port(U, kPortA); break;
        case 0xC1: U->a_ = read_port(U, kPortB); break;
        case 0xC2: U->a_ = read_port(U, kPortC); break;
        default: break;
    }
}

static void op_4d(upd7801 *U) {
    const uint8_t op = fetch(U);
    switch (op) {
        case 0xC0: write_port(U, kPortA, U->a_); break;
        case 0xC1: write_port(U, kPortB, U->a_); break;
        case 0xC2: write_port(U, kPortC, U->a_); break;
        case 0xC3: U->mkl_ = U->a_; break;
        case 0xC4:
            if (U->mb_ != U->a_) {
                U->mb_ = U->a_;
                write_port(U, kPortB, U->pb_out_);
            }
            break;
        case 0xC5: U->mc_ = (uint8_t)(0x84 | (U->a_ & 0x03)); break;
        case 0xC6: U->tm_ = (uint16_t)((U->tm_ & 0xFF00) | U->a_); break;
        case 0xC7: U->tm_ = (uint16_t)((U->tm_ & 0x00FF) | ((uint16_t)(U->a_) << 8)); break;
        default: break;
    }
}

static void op_60(upd7801 *U) {
    const uint8_t op = fetch(U);
    if (op >= 0x08 && op <= 0x0F) ana_x_a(U, reg_by_index(U, op));
    else if (op == 0x10 || op == 0x11 || op == 0x91) xra_x_a(U, reg_by_index(U, op == 0x91 ? 1 : op));
    else if (op >= 0x12 && op <= 0x17) xra_x_a(U, reg_by_index(U, op));
    else if (op >= 0x18 && op <= 0x1F) ora_x_a(U, reg_by_index(U, op));
    else if (op >= 0x20 && op <= 0x27) addnc_x_a(U, reg_by_index(U, op));
    else if (op >= 0x28 && op <= 0x2F) gta_x_a(U, reg_by_index(U, op));
    else if (op >= 0x30 && op <= 0x37) subnb_x_a(U, reg_by_index(U, op));
    else if (op >= 0x38 && op <= 0x3F) lta_x_a(U, reg_by_index(U, op));
    else if ((op >= 0x40 && op <= 0x47) || op == 0xC1) add_x_a(U, reg_by_index(U, op == 0xC1 ? 1 : op));
    else if (op >= 0x50 && op <= 0x57) adc_x_a(U, reg_by_index(U, op));
    else if (op >= 0x60 && op <= 0x67) sub_x_a(U, reg_by_index(U, op));
    else if (op >= 0x68 && op <= 0x6F) nea_x_a(U, reg_by_index(U, op));
    else if (op >= 0x70 && op <= 0x77) sbb_x_a(U, reg_by_index(U, op));
    else if (op >= 0x78 && op <= 0x7F) eqa_x_a(U, reg_by_index(U, op));
    else if (op >= 0x88 && op <= 0x8F) {
        U->a_ = (uint8_t)(U->a_ & *reg_by_index(U, op));
        U->zf_ = U->a_ == 0;
    } else if (op == 0x90 || (op >= 0x92 && op <= 0x97)) xra_a(U, *reg_by_index(U, op));
    else if (op >= 0x98 && op <= 0x9F) ora_a(U, *reg_by_index(U, op));
    else if (op >= 0xA0 && op <= 0xA7) addnc_a(U, *reg_by_index(U, op));
    else if (op >= 0xA8 && op <= 0xAF) gta_a(U, *reg_by_index(U, op));
    else if (op >= 0xB0 && op <= 0xB7) subnb_a(U, *reg_by_index(U, op));
    else if (op >= 0xB8 && op <= 0xBF) lta_a(U, *reg_by_index(U, op));
    else if (op == 0xC0 || (op >= 0xC2 && op <= 0xC7)) add_a(U, *reg_by_index(U, op));
    else if (op >= 0xC8 && op <= 0xCF) ona_a(U, *reg_by_index(U, op));
    else if (op >= 0xD0 && op <= 0xD7) adc_a(U, *reg_by_index(U, op));
    else if (op >= 0xD8 && op <= 0xDF) offa_a(U, *reg_by_index(U, op));
    else if (op >= 0xE0 && op <= 0xE7) sub_a(U, *reg_by_index(U, op));
    else if (op >= 0xE8 && op <= 0xEF) nea_a(U, *reg_by_index(U, op));
    else if (op >= 0xF0 && op <= 0xF7) sbb_a(U, *reg_by_index(U, op));
    else if (op >= 0xF8 && op <= 0xFF) eqa_a(U, *reg_by_index(U, op));
}

static void op_64(upd7801 *U) {
    const uint8_t op = fetch(U);
    if (op >= 0x08 && op <= 0x0F) ani(U, reg_by_index(U, op));
    else if (op >= 0x10 && op <= 0x17) xri(U, reg_by_index(U, op));
    else if (op >= 0x18 && op <= 0x1F) ori(U, reg_by_index(U, op));
    else if (op >= 0x20 && op <= 0x27) adinc(U, reg_by_index(U, op));
    else if (op >= 0x28 && op <= 0x2F) gti(U, reg_by_index(U, op));
    else if (op >= 0x30 && op <= 0x37) suinb(U, reg_by_index(U, op));
    else if (op >= 0x38 && op <= 0x3F) lti(U, reg_by_index(U, op));
    else if (op >= 0x40 && op <= 0x47) adi(U, reg_by_index(U, op));
    else if (op >= 0x48 && op <= 0x4F) oni(U, reg_by_index(U, op));
    else if (op >= 0x50 && op <= 0x57) aci(U, reg_by_index(U, op));
    else if (op >= 0x58 && op <= 0x5F) offi(U, reg_by_index(U, op));
    else if (op >= 0x60 && op <= 0x67) sui(U, reg_by_index(U, op));
    else if (op >= 0x68 && op <= 0x6F) nei(U, reg_by_index(U, op));
    else if (op >= 0x70 && op <= 0x77) sbi(U, reg_by_index(U, op));
    else if (op >= 0x78 && op <= 0x7F) eqi(U, reg_by_index(U, op));
    else if (op == 0x8A) {
        const uint8_t temp = (uint8_t)(read_port(U, kPortC) & fetch(U));
        write_port(U, kPortC, temp);
        U->zf_ = temp == 0;
    } else if (op == 0x8B) {
        U->mkl_ = (uint8_t)(U->mkl_ & fetch(U));
        U->zf_ = U->mkl_ == 0;
    } else if (op == 0x93) {
        U->mkl_ = (uint8_t)(U->mkl_ ^ fetch(U));
        U->zf_ = U->mkl_ == 0;
    } else if (op == 0x9A) {
        const uint8_t temp = (uint8_t)(read_port(U, kPortC) | fetch(U));
        write_port(U, kPortC, temp);
        U->zf_ = temp == 0;
    } else if (op == 0x9B) {
        U->mkl_ = (uint8_t)(U->mkl_ | fetch(U));
        U->zf_ = U->mkl_ == 0;
    } else if (op == 0xA8) {
        const uint8_t port = read_port(U, kPortA);
        const uint16_t after = (uint16_t)(port - fetch(U) - 1);
        zhc_sub(U, after, port, false);
        if (!U->cy_) U->sk_ = true;
    } else if (op == 0xCA) {
        const uint8_t port = read_port(U, kPortC);
        if ((port & fetch(U)) != 0) U->sk_ = true;
    } else if (op == 0xCB) {
        if ((U->mkl_ & fetch(U)) != 0) U->sk_ = true;
    } else if (op == 0xDA) {
        const uint8_t port = read_port(U, kPortC);
        if ((port & fetch(U)) == 0) U->sk_ = true;
    } else if (op == 0xDB) {
        offi(U, &U->mkl_);
    }
}

static void op_70(upd7801 *U) {
    const uint8_t op = fetch(U);
    switch (op) {
        case 0x0E: st16(U, fetch16(U), U->sp_); break;
        case 0x0F: U->sp_ = ld16(U, fetch16(U)); break;
        case 0x1E: st16(U, fetch16(U), bc(U)); break;
        case 0x1F: set_bc(U, ld16(U, fetch16(U))); break;
        case 0x2E: st16(U, fetch16(U), de(U)); break;
        case 0x2F: set_de(U, ld16(U, fetch16(U))); break;
        case 0x3E: st16(U, fetch16(U), hl(U)); break;
        case 0x3F: set_hl(U, ld16(U, fetch16(U))); break;
        case 0x68: U->v_ = rd(U, fetch16(U)); break;
        case 0x69: U->a_ = rd(U, fetch16(U)); break;
        case 0x6A: U->b_ = rd(U, fetch16(U)); break;
        case 0x6B: U->c_ = rd(U, fetch16(U)); break;
        case 0x6C: U->d_ = rd(U, fetch16(U)); break;
        case 0x6D: U->e_ = rd(U, fetch16(U)); break;
        case 0x6E: U->h_ = rd(U, fetch16(U)); break;
        case 0x6F: U->l_ = rd(U, fetch16(U)); break;
        case 0x78: wr(U, fetch16(U), U->v_); break;
        case 0x79: wr(U, fetch16(U), U->a_); break;
        case 0x7A: wr(U, fetch16(U), U->b_); break;
        case 0x7B: wr(U, fetch16(U), U->c_); break;
        case 0x7C: wr(U, fetch16(U), U->d_); break;
        case 0x7D: wr(U, fetch16(U), U->e_); break;
        case 0x7E: wr(U, fetch16(U), U->h_); break;
        case 0x7F: wr(U, fetch16(U), U->l_); break;
        default: {
                if (op == 0x89 || op == 0x8A || op == 0x8B || op == 0x8D) {
                uint16_t addr = 0;
                if (op == 0x89) addr = bc(U);
                else if (op == 0x8A) addr = de(U);
                else if (op == 0x8B) addr = hl(U);
                else {
                    addr = hl(U);
                    set_hl(U, (uint16_t)(hl(U) + 1));
                }
                U->a_ = (uint8_t)(U->a_ & rd(U, addr));
                U->zf_ = U->a_ == 0;
            } else if (op == 0x91 || op == 0x92 || op == 0x93) {
                const uint16_t addr = (op == 0x91) ? bc(U) : (op == 0x92) ? de(U) : hl(U);
                U->a_ = (uint8_t)(U->a_ ^ rd(U, addr));
                U->zf_ = U->a_ == 0;
            } else if (op >= 0x99 && op <= 0x9F && op != 0x98) {
                const uint16_t addr = ptr(U, op);
                U->a_ = (uint8_t)(U->a_ | rd(U, addr));
                U->zf_ = U->a_ == 0;
            } else if (op == 0xA2 || op == 0xA3) {
                const uint8_t m = rd(U, op == 0xA2 ? de(U) : hl(U));
                const uint8_t before = U->a_;
                U->a_ = (uint8_t)(U->a_ + m);
                zhc_add(U, U->a_, before, false);
                if (!U->cy_) U->sk_ = true;
            } else if (op == 0xA9 || op == 0xAA || op == 0xAB || op == 0xAC) {
                uint16_t addr = 0;
                if (op == 0xA9) addr = bc(U);
                else if (op == 0xAA) addr = de(U);
                else if (op == 0xAB) addr = hl(U);
                else {
                    addr = de(U);
                    set_de(U, (uint16_t)(de(U) + 1));
                }
                const uint16_t after = (uint16_t)(U->a_ - rd(U, addr) - 1);
                zhc_sub(U, after, U->a_, false);
                if (!U->cy_) U->sk_ = true;
            } else if (op >= 0xB1 && op <= 0xB7) {
                const uint16_t addr = ptr(U, op);
                const uint8_t before = U->a_;
                U->a_ = (uint8_t)(U->a_ - rd(U, addr));
                zhc_sub(U, U->a_, before, false);
                if (!U->cy_) U->sk_ = true;
            } else if (op == 0xB9 || op == 0xBA || op == 0xBB || op == 0xBC) {
                uint16_t addr = 0;
                if (op == 0xB9) addr = bc(U);
                else if (op == 0xBA) addr = de(U);
                else if (op == 0xBB) addr = hl(U);
                else {
                    addr = de(U);
                    set_de(U, (uint16_t)(de(U) + 1));
                }
                const uint8_t after = (uint8_t)(U->a_ - rd(U, addr));
                zhc_sub(U, after, U->a_, false);
                if (U->cy_) U->sk_ = true;
            } else if (op >= 0xC1 && op <= 0xC7) {
                const uint16_t addr = ptr(U, op);
                const uint8_t before = U->a_;
                U->a_ = (uint8_t)(U->a_ + rd(U, addr));
                zhc_add(U, U->a_, before, false);
            } else if (op == 0xCA || op == 0xCB) {
                const uint8_t m = rd(U, op == 0xCA ? de(U) : hl(U));
                ona_a(U, m);
            } else if (op == 0xD1 || op == 0xD2 || op == 0xD3 || op == 0xD5 ||
                       op == 0xD6 || op == 0xD7) {
                uint16_t addr = 0;
                if (op == 0xD1) addr = bc(U);
                else if (op == 0xD2) addr = de(U);
                else if (op == 0xD3) addr = hl(U);
                else if (op == 0xD5) {
                    addr = hl(U);
                    set_hl(U, (uint16_t)(hl(U) + 1));
                } else if (op == 0xD6) {
                    addr = de(U);
                    set_de(U, (uint16_t)(de(U) - 1));
                } else {
                    addr = hl(U);
                    set_hl(U, (uint16_t)(hl(U) - 1));
                }
                const uint8_t before = U->a_;
                U->a_ = (uint8_t)(U->a_ + rd(U, addr) + (uint8_t)(U->cy_));
                zhc_add(U, U->a_, before, false);
            } else if (op == 0xD9 || op == 0xDA || op == 0xDB) {
                const uint16_t addr = (op == 0xD9) ? bc(U) : (op == 0xDA) ? de(U) : hl(U);
                offa_a(U, rd(U, addr));
            } else if (op >= 0xE1 && op <= 0xE7) {
                const uint16_t addr = ptr(U, op);
                const uint8_t before = U->a_;
                U->a_ = (uint8_t)(U->a_ - rd(U, addr));
                zhc_sub(U, U->a_, before, false);
            } else if (op >= 0xE9 && op <= 0xED) {
                uint16_t addr = 0;
                if (op == 0xE9) addr = bc(U);
                else if (op == 0xEA) addr = de(U);
                else if (op == 0xEB) addr = hl(U);
                else if (op == 0xEC) {
                    addr = de(U);
                    set_de(U, (uint16_t)(de(U) + 1));
                } else {
                    addr = hl(U);
                    set_hl(U, (uint16_t)(hl(U) + 1));
                }
                const uint8_t after = (uint8_t)(U->a_ - rd(U, addr));
                zhc_sub(U, after, U->a_, false);
                if (!U->zf_) U->sk_ = true;
            } else if (op == 0xF2 || op == 0xF3 || op == 0xF4 || op == 0xF7) {
                uint16_t addr = 0;
                if (op == 0xF2) addr = de(U);
                else if (op == 0xF3) addr = hl(U);
                else if (op == 0xF4) {
                    addr = de(U);
                    set_de(U, (uint16_t)(de(U) + 1));
                } else {
                    addr = hl(U);
                    set_hl(U, (uint16_t)(hl(U) - 1));
                }
                const uint8_t before = U->a_;
                const bool carry = U->cy_;
                U->a_ = (uint8_t)(U->a_ - rd(U, addr) - (uint8_t)(carry));
                zhc_sub(U, U->a_, before, carry);
            } else if (op >= 0xF9 && op <= 0xFF) {
                const uint16_t addr = ptr(U, op);
                const uint8_t after = (uint8_t)(U->a_ - rd(U, addr));
                zhc_sub(U, after, U->a_, false);
                if (U->zf_) U->sk_ = true;
            }
            break;
        }
    }
}

static void op_74(upd7801 *U) {
    const uint8_t op = fetch(U);
    switch (op) {
        case 0x88:
            U->a_ = (uint8_t)(U->a_ & wa_mem(U));
            U->zf_ = U->a_ == 0;
            break;
        case 0x90:
            U->a_ = (uint8_t)(U->a_ ^ wa_mem(U));
            U->zf_ = U->a_ == 0;
            break;
        case 0x98:
            U->a_ = (uint8_t)(U->a_ | wa_mem(U));
            U->zf_ = U->a_ == 0;
            break;
        case 0xA0: addnc_a(U, wa_mem(U)); break;
        case 0xA8: gta_a(U, wa_mem(U)); break;
        case 0xB0: subnb_a(U, wa_mem(U)); break;
        case 0xB8: lta_a(U, wa_mem(U)); break;
        case 0xC0: add_a(U, wa_mem(U)); break;
        case 0xC8: ona_a(U, wa_mem(U)); break;
        case 0xD0: adc_a(U, wa_mem(U)); break;
        case 0xD8: offa_a(U, wa_mem(U)); break;
        case 0xE0: sub_a(U, wa_mem(U)); break;
        case 0xE8: nea_a(U, wa_mem(U)); break;
        case 0xF8: eqa_a(U, wa_mem(U)); break;
        default:
            break;
    }
}

static int execute_one(upd7801 *U) {
    U->ppc_ = U->pc_;
    const uint8_t op = fetch(U);
    int cycles = kOpCycles[op] ? kOpCycles[op] : 4;
    const bool keep_l0 = (op == 0x34 || op == 0x6F);
    const bool keep_l1 = (op == 0x69);

    if (U->sk_ && op != 0x72) {
        U->sk_ = false;
        skip_rest(U, op);
        if (op == 0x48 || op == 0x4C || op == 0x4D || op == 0x60 || op == 0x64 ||
            op == 0x70 || op == 0x74) {
            // skip_rest already consumed the prefix byte; cycles from main table are 0.
            cycles = 8;
        }
        if (!keep_l0) U->l0_ = false;
        if (!keep_l1) U->l1_ = false;
        return cycles;
    }
    U->sk_ = false;


    switch (op) {
        case 0x00: break;
        case 0x01:
            U->pc_--;
            cycles = 6;
            break;
        case 0x02: U->sp_++; break;
        case 0x04: U->sp_ = fetch16(U); break;
        case 0x05: {
            const uint16_t addr = wa(U);
            const uint8_t v = (uint8_t)(rd(U, addr) & fetch(U));
            wr(U, addr, v);
            U->zf_ = v == 0;
            break;
        }
        case 0x07: ani(U, &U->a_); break;
        case 0x08:
            U->pc_ = (uint16_t)(rd(U, U->sp_) | (rd(U, (uint16_t)(U->sp_ + 1)) << 8));
            U->sp_ = (uint16_t)(U->sp_ + 2);
            break;
        case 0x0A: U->a_ = U->b_; break;
        case 0x0B: U->a_ = U->c_; break;
        case 0x0C: U->a_ = U->d_; break;
        case 0x0D: U->a_ = U->e_; break;
        case 0x0E: U->a_ = U->h_; break;
        case 0x0F: U->a_ = U->l_; break;
        case 0x10:
            SWAP(U->ea_, U->ea2_);
            SWAP(U->v_, U->v2_);
            SWAP(U->a_, U->a2_);
            break;
        case 0x11:
            SWAP(U->b_, U->b2_);
            SWAP(U->c_, U->c2_);
            SWAP(U->d_, U->d2_);
            SWAP(U->e_, U->e2_);
            SWAP(U->h_, U->h2_);
            SWAP(U->l_, U->l2_);
            break;
        case 0x12: set_bc(U, (uint16_t)(bc(U) + 1)); break;
        case 0x13: set_bc(U, (uint16_t)(bc(U) - 1)); break;
        case 0x14: set_bc(U, fetch16(U)); break;
        case 0x15: {
            const uint16_t addr = wa(U);
            const uint8_t v = (uint8_t)(rd(U, addr) | fetch(U));
            wr(U, addr, v);
            U->zf_ = v == 0;
            break;
        }
        case 0x16: xri(U, &U->a_); break;
        case 0x17: ori(U, &U->a_); break;
        case 0x18:
            U->pc_ = (uint16_t)(rd(U, U->sp_) | (rd(U, (uint16_t)(U->sp_ + 1)) << 8));
            U->sp_ = (uint16_t)(U->sp_ + 2);
            U->sk_ = true;
            break;
        case 0x19:
            U->to_ = 1;
            U->ovc0_ = 8 * (int)(U->tm_ & 0x0FFF);
            break;
        case 0x1A: U->b_ = U->a_; break;
        case 0x1B: U->c_ = U->a_; break;
        case 0x1C: U->d_ = U->a_; break;
        case 0x1D: U->e_ = U->a_; break;
        case 0x1E: U->h_ = U->a_; break;
        case 0x1F: U->l_ = U->a_; break;
        case 0x20: {
            const bool saved = U->cy_;
            const uint16_t addr = wa(U);
            const uint8_t before = rd(U, addr);
            const uint8_t after = (uint8_t)(before + 1);
            zhc_add(U, after, before, false);
            wr(U, addr, after);
            if (U->cy_) U->sk_ = true;
            U->cy_ = saved;
            break;
        }
        case 0x21: {
            const uint16_t addr = (uint16_t)(U->pc_ + U->a_ + 1);
            U->c_ = rd(U, addr);
            U->b_ = rd(U, (uint16_t)(addr + 1));
            break;
        }
        case 0x22: set_de(U, (uint16_t)(de(U) + 1)); break;
        case 0x23: set_de(U, (uint16_t)(de(U) - 1)); break;
        case 0x24: set_de(U, fetch16(U)); break;
        case 0x25: {
            const uint16_t addr = wa(U);
            const uint8_t l = rd(U, addr);
            const uint16_t after = (uint16_t)(l - fetch(U) - 1);
            zhc_sub(U, after, l, false);
            if (!U->cy_) U->sk_ = true;
            break;
        }
        case 0x26: adinc(U, &U->a_); break;
        case 0x27: gti(U, &U->a_); break;
        case 0x28: U->a_ = rd(U, wa(U)); break;
        case 0x29: U->a_ = rd(U, bc(U)); break;
        case 0x2A: U->a_ = rd(U, de(U)); break;
        case 0x2B: U->a_ = rd(U, hl(U)); break;
        case 0x2C:
            U->a_ = rd(U, de(U));
            set_de(U, (uint16_t)(de(U) + 1));
            break;
        case 0x2D:
            U->a_ = rd(U, hl(U));
            set_hl(U, (uint16_t)(hl(U) + 1));
            break;
        case 0x2E:
            U->a_ = rd(U, de(U));
            set_de(U, (uint16_t)(de(U) - 1));
            break;
        case 0x2F:
            U->a_ = rd(U, hl(U));
            set_hl(U, (uint16_t)(hl(U) - 1));
            break;
        case 0x30: {
            const bool saved = U->cy_;
            const uint16_t addr = wa(U);
            const uint8_t before = rd(U, addr);
            const uint8_t after = (uint8_t)(before - 1);
            zhc_sub(U, after, before, false);
            wr(U, addr, after);
            if (U->cy_) U->sk_ = true;
            U->cy_ = saved;
            break;
        }
        case 0x31:
            wr(U, de(U), rd(U, hl(U)));
            set_de(U, (uint16_t)(de(U) + 1));
            set_hl(U, (uint16_t)(hl(U) + 1));
            U->c_ = (uint8_t)(U->c_ - 1);
            if (U->c_ == 0xFF) U->cy_ = true;
            else {
                U->cy_ = false;
                U->pc_--;
            }
            break;
        case 0x32: set_hl(U, (uint16_t)(hl(U) + 1)); break;
        case 0x33: set_hl(U, (uint16_t)(hl(U) - 1)); break;
        case 0x34:
            if (U->l0_) U->pc_ = (uint16_t)(U->pc_ + 2);
            else {
                set_hl(U, fetch16(U));
                U->l0_ = true;
            }
            break;
        case 0x35: {
            const uint16_t addr = wa(U);
            const uint8_t l = rd(U, addr);
            const uint8_t after = (uint8_t)(l - fetch(U));
            zhc_sub(U, after, l, false);
            if (U->cy_) U->sk_ = true;
            break;
        }
        case 0x36:
            sui(U, &U->a_);
            if (!U->cy_) U->sk_ = true;
            break;
        case 0x37: lti(U, &U->a_); break;
        case 0x38: wr(U, wa(U), U->a_); break;
        case 0x39: wr(U, bc(U), U->a_); break;
        case 0x3A: wr(U, de(U), U->a_); break;
        case 0x3B: wr(U, hl(U), U->a_); break;
        case 0x3C:
            wr(U, de(U), U->a_);
            set_de(U, (uint16_t)(de(U) + 1));
            break;
        case 0x3D:
            wr(U, hl(U), U->a_);
            set_hl(U, (uint16_t)(hl(U) + 1));
            break;
        case 0x3E:
            wr(U, de(U), U->a_);
            set_de(U, (uint16_t)(de(U) - 1));
            break;
        case 0x3F:
            wr(U, hl(U), U->a_);
            set_hl(U, (uint16_t)(hl(U) - 1));
            break;
        case 0x41: {
            const bool saved = U->cy_;
            const uint8_t before = U->a_;
            U->a_ = (uint8_t)(U->a_ + 1);
            zhc_add(U, U->a_, before, false);
            if (U->cy_) U->sk_ = true;
            U->cy_ = saved;
            break;
        }
        case 0x42: {
            const bool saved = U->cy_;
            const uint8_t before = U->b_;
            U->b_ = (uint8_t)(U->b_ + 1);
            zhc_add(U, U->b_, before, false);
            if (U->cy_) U->sk_ = true;
            U->cy_ = saved;
            break;
        }
        case 0x43: {
            const bool saved = U->cy_;
            const uint8_t before = U->c_;
            U->c_ = (uint8_t)(U->c_ + 1);
            zhc_add(U, U->c_, before, false);
            if (U->cy_) U->sk_ = true;
            U->cy_ = saved;
            break;
        }
        case 0x44: {
            const uint16_t target = fetch16(U);
            push_pc(U);
            U->pc_ = target;
            break;
        }
        case 0x45: {
            const uint16_t addr = wa(U);
            const uint8_t mask = fetch(U);
            U->sk_ = (rd(U, addr) & mask) != 0;
            break;
        }
        case 0x46: adi(U, &U->a_); break;
        case 0x47: oni(U, &U->a_); break;
        case 0x48:
            op_48(U);
            cycles = prefix_cycles(U, 0x48, rd(U, (uint16_t)(U->ppc_ + 1)));
            break;
        case 0x49: wr(U, bc(U), fetch(U)); break;
        case 0x4A: wr(U, de(U), fetch(U)); break;
        case 0x4B: wr(U, hl(U), fetch(U)); break;
        case 0x4C:
            op_4c(U);
            cycles = prefix_cycles(U, 0x4C, rd(U, (uint16_t)(U->ppc_ + 1)));
            break;
        case 0x4D:
            op_4d(U);
            cycles = prefix_cycles(U, 0x4D, rd(U, (uint16_t)(U->ppc_ + 1)));
            break;
        case 0x4E: {
            const uint8_t d = fetch(U);
            U->pc_ = (uint16_t)(U->pc_ + d);
            break;
        }
        case 0x4F: {
            const uint8_t d = fetch(U);
            U->pc_ = (uint16_t)(U->pc_ - (256 - d));
            break;
        }
        case 0x51: {
            const bool saved = U->cy_;
            const uint8_t before = U->a_;
            U->a_ = (uint8_t)(U->a_ - 1);
            zhc_sub(U, U->a_, before, false);
            if (U->cy_) U->sk_ = true;
            U->cy_ = saved;
            break;
        }
        case 0x52: {
            const bool saved = U->cy_;
            const uint8_t before = U->b_;
            U->b_ = (uint8_t)(U->b_ - 1);
            zhc_sub(U, U->b_, before, false);
            if (U->cy_) U->sk_ = true;
            U->cy_ = saved;
            break;
        }
        case 0x53: {
            const bool saved = U->cy_;
            const uint8_t before = U->c_;
            U->c_ = (uint8_t)(U->c_ - 1);
            zhc_sub(U, U->c_, before, false);
            if (U->cy_) U->sk_ = true;
            U->cy_ = saved;
            break;
        }
        case 0x54: U->pc_ = fetch16(U); break;
        case 0x55: {
            const uint16_t addr = wa(U);
            const uint8_t mask = fetch(U);
            U->sk_ = (rd(U, addr) & mask) == 0;
            break;
        }
        case 0x56: aci(U, &U->a_); break;
        case 0x57: offi(U, &U->a_); break;
        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
            const uint16_t addr = wa(U);
            U->sk_ = (rd(U, addr) & (1u << (op & 7))) != 0;
            break;
        }
        case 0x60:
            op_60(U);
            cycles = 8;
            break;
        case 0x61: {
            const uint8_t l = U->a_ & 0x0F;
            const uint8_t h = (uint8_t)(U->a_ >> 4);
            uint8_t adj = 0;
            const bool saved = U->cy_;
            if (!U->hc_) {
                if (l < 10) {
                    if (!((h < 10) && !U->cy_)) adj = 0x60;
                } else {
                    adj = ((h < 9) && !U->cy_) ? 0x06 : 0x66;
                }
            } else if (l < 3) {
                adj = ((h < 10) && !U->cy_) ? 0x06 : 0x66;
            }
            const uint8_t before = U->a_;
            U->a_ = (uint8_t)(U->a_ + adj);
            zhc_add(U, U->a_, before, U->cy_);
            U->cy_ = U->cy_ || saved;
            break;
        }
        case 0x62:
            U->pc_ = (uint16_t)(rd(U, U->sp_) | (rd(U, (uint16_t)(U->sp_ + 1)) << 8));
            set_psw(U, rd(U, (uint16_t)(U->sp_ + 2)));
            U->sp_ = (uint16_t)(U->sp_ + 3);
            break;
        case 0x63:
            push_pc(U);
            U->pc_ = bc(U);
            break;
        case 0x64:
            op_64(U);
            cycles = prefix_cycles(U, 0x64, rd(U, (uint16_t)(U->ppc_ + 1)));
            break;
        case 0x65: {
            const uint16_t addr = wa(U);
            const uint8_t l = rd(U, addr);
            const uint8_t after = (uint8_t)(l - fetch(U));
            zhc_sub(U, after, l, false);
            if (!U->zf_) U->sk_ = true;
            break;
        }
        case 0x66: sui(U, &U->a_); break;
        case 0x67: nei(U, &U->a_); break;
        case 0x68: U->v_ = fetch(U); break;
        case 0x69:
            if (U->l1_) U->pc_++;
            else {
                U->a_ = fetch(U);
                U->l1_ = true;
            }
            break;
        case 0x6A: U->b_ = fetch(U); break;
        case 0x6B: U->c_ = fetch(U); break;
        case 0x6C: U->d_ = fetch(U); break;
        case 0x6D: U->e_ = fetch(U); break;
        case 0x6E: U->h_ = fetch(U); break;
        case 0x6F:
            if (U->l0_) U->pc_++;
            else {
                U->l_ = fetch(U);
                U->l0_ = true;
            }
            break;
        case 0x70:
            op_70(U);
            cycles = prefix_cycles(U, 0x70, rd(U, (uint16_t)(U->ppc_ + 1)));
            break;
        case 0x71: {
            const uint16_t addr = wa(U);
            wr(U, addr, fetch(U));
            break;
        }
        case 0x73: U->pc_ = bc(U); break;
        case 0x74:
            op_74(U);
            cycles = prefix_cycles(U, 0x74, rd(U, (uint16_t)(U->ppc_ + 1)));
            break;
        case 0x75: {
            const uint16_t addr = wa(U);
            const uint8_t l = rd(U, addr);
            const uint8_t after = (uint8_t)(l - fetch(U));
            zhc_sub(U, after, l, false);
            if (U->zf_) U->sk_ = true;
            break;
        }
        case 0x76: sbi(U, &U->a_); break;
        case 0x77: eqi(U, &U->a_); break;
        default:
            if (op >= 0x78 && op <= 0x7F) {
                const uint16_t target = (uint16_t)(((0x08 + (op & 7)) << 8) | fetch(U));
                push_pc(U);
                U->pc_ = target;
            } else if (op >= 0x80 && op <= 0xBF) {
                const uint16_t vector = (uint16_t)(0x80 + 2 * (op & 0x3F));
                push_pc(U);
                U->pc_ = (uint16_t)(rd(U, vector) | (rd(U, (uint16_t)(vector + 1)) << 8));
            } else if (op >= 0xC0) {
                U->pc_ = (uint16_t)(U->pc_ + ((int8_t)(op << 2) >> 2));
            }
            break;
    }

    if (!keep_l0) U->l0_ = false;
    if (!keep_l1) U->l1_ = false;
    return cycles;
}

int upd7801_run(upd7801 *U, int cycles) {
    int done = 0;
    while (done < cycles) {
        const int remaining = cycles - done;
        const int cyc = execute_one(U);
        const int used = (cyc > 0) ? cyc : 4;
        handle_timers(U, used);
        take_irq(U);
        U->iff_ = U->iff_pending_;
        done += used;
        if (U->cycles) U->cycles(U->user, used);
        if (U->pc_ == U->ppc_ && used == 6) {
            const int rest = remaining - used;
            if (rest > 0) {
                handle_timers(U, rest);
                take_irq(U);
                U->iff_ = U->iff_pending_;
                done += rest;
                if (U->cycles) U->cycles(U->user, rest);
            }
        }
    }
    return done;
}

