// vicdual.c - the Sega / Gremlin VIC Dual machines for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/vicdual (after
// MAME's sega/vicdual.cpp). A Z80 at 1.93 MHz, 32x28 characters from
// character RAM with an optional colour PROM, the coin resetting the CPU.
// The sound is dsp-cpp's: Head On's discrete board as a behavioural model,
// Carnival's i8035 music board with its AY-3-8912, and synthesized
// stand-ins for MAME's samples and netlists (Depthcharge, Invinco, Pulsar,
// Carnival, N-Sub; Frogs, Borderline, Tranquillizer Gun). ARC_VARIANT: the
// 21 games, in the order of dsp-cpp's VicDual::Game. Two differ from it:
// Heiankyo loads its whole colour PROM (dsp-cpp asks for half of it, the
// load fails and it shows black and white; HEIANKYO_DSP_PROM builds that),
// and headon2s is MAME's set, the Sidam bootleg of Head On 2, on the Head
// On board without colours (dsp-cpp's headon2s wants another set).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/mcs48/mcs48.h"
#include "../../../sound/ay8910.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define G_DEPTHCHARGE 0
#define G_SAFARI 1
#define G_FROGS 2
#define G_SPACEATTACK 3
#define G_SPACEATTACKHEADON 4
#define G_HEADON 5
#define G_HEADON2 6
#define G_HEADON2SLIM 7
#define G_INVINCOHEADON2 8
#define G_NSUB 9
#define G_SAMURAI 10
#define G_INVINCO 11
#define G_INVINCODEEPSCAN 12
#define G_TRANQGUN 13
#define G_SPACETREK 14
#define G_CARNIVAL 15
#define G_BORDERLINE 16
#define G_DIGGER 17
#define G_PULSAR 18
#define G_HEIANKYO 19
#define G_ALPHAFIGHTER 20

#if ARC_VARIANT == 0
#define GAME G_DEPTHCHARGE
static const char *const kSets[] = { "depthch", NULL };
static const arc_rom kCpu[] = { {"50a", 0x0400, 0x0000, 0x56c5ffed}, {"51a", 0x0400, 0x0400, 0x695eb81f}, {"52", 0x0400, 0x0800, 0xaed0ba1b}, {"53", 0x0400, 0x0c00, 0x2ccbd2d0}, {"54a", 0x0400, 0x1000, 0x1b7f6a43}, {"55a", 0x0400, 0x1400, 0x9fc2eb41} };
#define TITLE "Depthcharge"
#define MAKER "Gremlin"
#define YEAR 1977
#define BTNS "DISPARO IZQ", "DISPARO DER"
#define JOY ARC_JOY_2WAY
#elif ARC_VARIANT == 1
#define GAME G_SAFARI
static const char *const kSets[] = { "safari", NULL };
static const arc_rom kCpu[] = { {"316-0066.u48", 0x0400, 0x0000, 0x2a26b098}, {"316-0065.u47", 0x0400, 0x0400, 0xb776f7db}, {"316-0064.u46", 0x0400, 0x0800, 0x19d8c196}, {"316-0063.u45", 0x0400, 0x0c00, 0x028bad25}, {"316-0062.u44", 0x0400, 0x1000, 0x504e0575}, {"316-0061.u43", 0x0400, 0x1400, 0xd4c528e0}, {"316-0060.u42", 0x0400, 0x1800, 0x48c7b0cc}, {"316-0059.u41", 0x0400, 0x1c00, 0x3f7baaff}, {"316-0058.u40", 0x0400, 0x2000, 0x0d5058f1}, {"316-0057.u39", 0x0400, 0x2400, 0x298e8c41} };
#define TITLE "Safari"
#define MAKER "Gremlin"
#define YEAR 1977
#define BTNS "DISPARO", "APUNTAR ARRIBA", "APUNTAR ABAJO"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 2
#define GAME G_FROGS
static const char *const kSets[] = { "frogs", NULL };
static const arc_rom kCpu[] = { {"316-119a.u48", 0x0400, 0x0000, 0xb1d1fce4}, {"316-118a.u47", 0x0400, 0x0400, 0x12fdcc05}, {"316-117a.u46", 0x0400, 0x0800, 0x8a5be424}, {"316-116b.u45", 0x0400, 0x0c00, 0x09b82619}, {"316-115a.u44", 0x0400, 0x1000, 0x3d4e4fa8}, {"316-114a.u43", 0x0400, 0x1400, 0x04a21853}, {"316-113a.u42", 0x0400, 0x1800, 0x02786692}, {"316-112a.u41", 0x0400, 0x1c00, 0x0be2a058} };
#define TITLE "Frogs"
#define MAKER "Gremlin"
#define YEAR 1978
#define BTNS "SALTO"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 3
#define GAME G_SPACEATTACK
static const char *const kSets[] = { "sspaceat", NULL };
static const arc_rom kCpu[] = { {"155.u27", 0x0400, 0x0000, 0xba7bb86f}, {"156.u26", 0x0400, 0x0400, 0x0b3a491c}, {"157.u25", 0x0400, 0x0800, 0x3d3fac3b}, {"158.u24", 0x0400, 0x0c00, 0x843b80f6}, {"159.u23", 0x0400, 0x1000, 0x1eacf60d}, {"160.u22", 0x0400, 0x1400, 0xe61d482f}, {"161.u21", 0x0400, 0x1800, 0xeb5e0993}, {"162.u20", 0x0400, 0x1c00, 0x5f84d550} };
static const arc_rom kProm[] = { {"316-0138.u44", 0x0020, 0x0000, 0x67104ea9} };
#define HAS_PROM 1
#define TITLE "Space Attack"
#define MAKER "Sega"
#define YEAR 1979
#define BTNS "DISPARO"
#define JOY ARC_JOY_2WAY
#elif ARC_VARIANT == 4
#define GAME G_SPACEATTACKHEADON
static const char *const kSets[] = { "sspacaho", NULL };
static const arc_rom kCpu[] = { {"epr-0001.bin", 0x0800, 0x0000, 0xba62f57a}, {"epr-0002.bin", 0x0800, 0x0800, 0x94b3c59c}, {"epr-0003.bin", 0x0800, 0x1000, 0xdf13aef2}, {"epr-0004.bin", 0x0800, 0x1800, 0x8431e15e}, {"epr-0005.bin", 0x0800, 0x2000, 0xeec2b6e7}, {"epr-0006.bin", 0x0800, 0x2800, 0x780e47ed}, {"epr-0007.bin", 0x0800, 0x3000, 0x8189a2fa}, {"epr-0008.bin", 0x0800, 0x3800, 0x34a64a80} };
static const arc_rom kProm[] = { {"316-0138.u44", 0x0020, 0x0000, 0x67104ea9} };
#define HAS_PROM 1
#define TITLE "Space Attack / Head On"
#define MAKER "Sega"
#define YEAR 1979
#define BTNS "DISPARO", "JUEGO"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 5
#define GAME G_HEADON
static const char *const kSets[] = { "headon", NULL };
static const arc_rom kCpu[] = { {"316-163a.u27", 0x0400, 0x0000, 0x4bb51259}, {"316-164a.u26", 0x0400, 0x0400, 0xaeac8c5f}, {"316-165a.u25", 0x0400, 0x0800, 0xf1a0cb72}, {"316-166c.u24", 0x0400, 0x0c00, 0x65d12951}, {"316-167c.u23", 0x0400, 0x1000, 0x2280831e}, {"316-192a.u22", 0x0400, 0x1400, 0xed4666f2}, {"316-193a.u21", 0x0400, 0x1800, 0x37a1df4c} };
static const arc_rom kProm[] = { {"316-0138.u44", 0x0020, 0x0000, 0x67104ea9} };
#define HAS_PROM 1
#define TITLE "Head On"
#define MAKER "Gremlin"
#define YEAR 1979
#define BTNS "ACELERAR"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 6
#define GAME G_HEADON2
static const char *const kSets[] = { "headon2", NULL };
static const arc_rom kCpu[] = { {"u27.bin", 0x0400, 0x0000, 0xfa47d2fb}, {"u26.bin", 0x0400, 0x0400, 0x61c47b15}, {"u25.bin", 0x0400, 0x0800, 0xbb16db92}, {"u24.bin", 0x0400, 0x0c00, 0x17a09f24}, {"u23.bin", 0x0400, 0x1000, 0x0024895e}, {"u22.bin", 0x0400, 0x1400, 0xf798304d}, {"u21.bin", 0x0400, 0x1800, 0x4c19dd40}, {"u20.bin", 0x0400, 0x1c00, 0x25887ff2} };
static const arc_rom kProm[] = { {"316-0138.u44", 0x0020, 0x0000, 0x67104ea9} };
#define HAS_PROM 1
#define TITLE "Head On 2"
#define MAKER "Sega"
#define YEAR 1979
#define BTNS "ACELERAR"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 7
#define GAME G_HEADON   // (a bootleg of Head On 2, on its board without the colour PROM)
#define SIDAM 1
static const char *const kSets[] = { "headon2s", NULL };
static const arc_rom kCpu[] = { {"10304.0.9a", 0x0400, 0x0000, 0x256a1fc8}, {"10304.1.8a", 0x0400, 0x0400, 0x61c47b15}, {"10304.2.7a", 0x0400, 0x0800, 0xa6c268d4}, {"10304.3.6a", 0x0400, 0x0c00, 0x17a09f24}, {"10304.4.5a", 0x0400, 0x1000, 0x9af8a2e0}, {"10304.5.4a", 0x0400, 0x1400, 0x6975286c}, {"10304.6.3a", 0x0400, 0x1800, 0x06fbcdce}, {"10304.7b.2a", 0x0400, 0x1c00, 0x3588fc8f} };
#define TITLE "Head On 2 (Sidam bootleg)"
#define MAKER "bootleg (Sidam)"
#define YEAR 1979
#define BTNS "ACELERAR"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 8
#define GAME G_INVINCOHEADON2
static const char *const kSets[] = { "invho2", NULL };
static const arc_rom kCpu[] = { {"271b.u33", 0x0400, 0x0000, 0x44356a73}, {"272b.u32", 0x0400, 0x0400, 0xbd251265}, {"273b.u31", 0x0400, 0x0800, 0x2fc80cd9}, {"274b.u30", 0x0400, 0x0c00, 0x4fac4210}, {"275b.u29", 0x0400, 0x1000, 0x85af508e}, {"276b.u28", 0x0400, 0x1400, 0xe305843a}, {"277b.u27", 0x0400, 0x1800, 0xb6b4221e}, {"278b.u26", 0x0400, 0x1c00, 0x74d42250}, {"279b.u8", 0x0400, 0x2000, 0x8d30a3e0}, {"280b.u7", 0x0400, 0x2400, 0xb5ee60ec}, {"281b.u6", 0x0400, 0x2800, 0x21a6d4f2}, {"282b.u5", 0x0400, 0x2c00, 0x07d54f8a}, {"283b.u4", 0x0400, 0x3000, 0xbdbe7ec1}, {"284b.u3", 0x0400, 0x3400, 0xae9e9f16}, {"285b.u2", 0x0400, 0x3800, 0x8dc3ec34}, {"286b.u1", 0x0400, 0x3c00, 0x4bab9ba2} };
static const arc_rom kProm[] = { {"316-0287.u49", 0x0020, 0x0000, 0xd4374b01} };
#define HAS_PROM 1
#define TITLE "Invinco / Head On 2"
#define MAKER "Sega"
#define YEAR 1979
#define BTNS "DISPARO", "JUEGO"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 9
#define GAME G_NSUB
static const char *const kSets[] = { "nsub", NULL };
static const arc_rom kCpu[] = { {"epr-268.u48", 0x0800, 0x0000, 0x485b4704}, {"epr-269.u47", 0x0800, 0x0800, 0x32774ac9}, {"epr-270.u46", 0x0800, 0x1000, 0xaf7ca40a}, {"epr-271.u45", 0x0800, 0x1800, 0x3f9c180b}, {"epr-272.u44", 0x0800, 0x2000, 0xd818aa51}, {"epr-273.u43", 0x0800, 0x2800, 0x03a6f12a}, {"epr-274.u42", 0x0800, 0x3000, 0xd69eb098}, {"epr-275.u41", 0x0800, 0x3800, 0x1c7d90cc} };
static const arc_rom kProm[] = { {"pr-69.u11", 0x0020, 0x0000, 0xc94dd091} };
#define HAS_PROM 1
#define TITLE "N-Sub"
#define MAKER "Sega"
#define YEAR 1980
#define BTNS "DISPARO", "DISPARO 2"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 10
#define GAME G_SAMURAI
static const char *const kSets[] = { "samurai", NULL };
static const arc_rom kCpu[] = { {"epr-1217.u33", 0x0400, 0x0000, 0xa1a9cb03}, {"epr-1218.u32", 0x0400, 0x0400, 0x4b45d07d}, {"epr-1219.u31", 0x0400, 0x0800, 0x9fd4b195}, {"epr-1220.u30", 0x0400, 0x0c00, 0x90370e13}, {"epr-1221.u29", 0x0400, 0x1000, 0xdcc47158}, {"epr-1222.u28", 0x0400, 0x1400, 0xd2fab27a}, {"epr-1223.u27", 0x0400, 0x1800, 0xf7e2ad95}, {"epr-1224.u26", 0x0400, 0x1c00, 0xd46e306b}, {"epr-1225.u8", 0x0400, 0x2000, 0x3dd5c41f}, {"epr-1226.u7", 0x0400, 0x2400, 0x7c3561b1}, {"epr-1227.u6", 0x0400, 0x2800, 0xe72c71a4}, {"epr-1228.u5", 0x0400, 0x2c00, 0xd76f4a56}, {"epr-1229.u4", 0x0400, 0x3000, 0xe0d40395}, {"epr-1230.u3", 0x0400, 0x3400, 0x55e9a5c4} };
static const arc_rom kProm[] = { {"pr55.clr", 0x0020, 0x0000, 0x975f5fb0} };
#define HAS_PROM 1
#define TITLE "Samurai"
#define MAKER "Sega"
#define YEAR 1980
#define BTNS "ESPADA"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 11
#define GAME G_INVINCO
static const char *const kSets[] = { "invinco", NULL };
static const arc_rom kCpu[] = { {"310a.u27", 0x0400, 0x0000, 0xe3931365}, {"311a.u26", 0x0400, 0x0400, 0xde1a6c4a}, {"312a.u25", 0x0400, 0x0800, 0xe3c08f39}, {"313a.u24", 0x0400, 0x0c00, 0xb680b306}, {"314a.u23", 0x0400, 0x1000, 0x790f07d9}, {"315a.u22", 0x0400, 0x1400, 0x0d13bed2}, {"316a.u21", 0x0400, 0x1800, 0x88d7eab8}, {"317a.u20", 0x0400, 0x1c00, 0x75389463}, {"318a.uxx", 0x0400, 0x2000, 0x0780721d} };
static const arc_rom kProm[] = { {"316-0246.u44", 0x0020, 0x0000, 0xfe4406cb} };
#define HAS_PROM 1
#define TITLE "Invinco"
#define MAKER "Sega"
#define YEAR 1979
#define BTNS "DISPARO"
#define JOY ARC_JOY_2WAY
#elif ARC_VARIANT == 12
#define GAME G_INVINCODEEPSCAN
static const char *const kSets[] = { "invds", NULL };
static const arc_rom kCpu[] = { {"367.u33", 0x0400, 0x0000, 0xe6a33eae}, {"368.u32", 0x0400, 0x0400, 0x421554a8}, {"369.u31", 0x0400, 0x0800, 0x531e917a}, {"370.u30", 0x0400, 0x0c00, 0x2ad68f8c}, {"371.u29", 0x0400, 0x1000, 0x1b98dc5c}, {"372.u28", 0x0400, 0x1400, 0x3a72190a}, {"373.u27", 0x0400, 0x1800, 0x3d361520}, {"374.u26", 0x0400, 0x1c00, 0xe606e7d9}, {"375.u8", 0x0400, 0x2000, 0xadbe8d32}, {"376.u7", 0x0400, 0x2400, 0x79409a46}, {"377.u6", 0x0400, 0x2800, 0x3f021a71}, {"378.u5", 0x0400, 0x2c00, 0x49a542b0}, {"379.u4", 0x0400, 0x3000, 0xee140e49}, {"380.u3", 0x0400, 0x3400, 0x688ba831}, {"381.u2", 0x0400, 0x3800, 0x798ba0c7}, {"382.u1", 0x0400, 0x3c00, 0x8d195c24} };
static const arc_rom kProm[] = { {"316-0246.u44", 0x0020, 0x0000, 0xfe4406cb} };
#define HAS_PROM 1
#define TITLE "Invinco / Deep Scan"
#define MAKER "Sega"
#define YEAR 1979
#define BTNS "DISPARO", "CARGA", "JUEGO"
#define JOY ARC_JOY_2WAY
#elif ARC_VARIANT == 13
#define GAME G_TRANQGUN
static const char *const kSets[] = { "tranqgun", NULL };
static const arc_rom kCpu[] = { {"u33.bin", 0x0400, 0x0000, 0x6d50e902}, {"u32.bin", 0x0400, 0x0400, 0xf0ba0e60}, {"u31.bin", 0x0400, 0x0800, 0x9fe440d3}, {"u30.bin", 0x0400, 0x0c00, 0x1041608e}, {"u29.bin", 0x0400, 0x1000, 0xfb5de95f}, {"u28.bin", 0x0400, 0x1400, 0x03fd8727}, {"u27.bin", 0x0400, 0x1800, 0x3d93239b}, {"u26.bin", 0x0400, 0x1c00, 0x20f64a7f}, {"u8.bin", 0x0400, 0x2000, 0x5121c695}, {"u7.bin", 0x0400, 0x2400, 0xb13d21f7}, {"u6.bin", 0x0400, 0x2800, 0x603cee59}, {"u5.bin", 0x0400, 0x2c00, 0x7f25475f}, {"u4.bin", 0x0400, 0x3000, 0x57dc3123}, {"u3.bin", 0x0400, 0x3400, 0x7aa7829b}, {"u2.bin", 0x0400, 0x3800, 0xa9b10df5}, {"u1.bin", 0x0400, 0x3c00, 0x431a7449} };
static const arc_rom kProm[] = { {"u49.bin", 0x0020, 0x0000, 0x6481445b} };
#define HAS_PROM 1
#define TITLE "Tranquillizer Gun"
#define MAKER "Sega"
#define YEAR 1980
#define BTNS "DISPARO"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 14
#define GAME G_SPACETREK
static const char *const kSets[] = { "spacetrk", NULL };
static const arc_rom kCpu[] = { {"u33.bin", 0x0400, 0x0000, 0x9033fe50}, {"u32.bin", 0x0400, 0x0400, 0x08f61f0d}, {"u31.bin", 0x0400, 0x0800, 0x1088a8c4}, {"u30.bin", 0x0400, 0x0c00, 0x55560cc8}, {"u29.bin", 0x0400, 0x1000, 0x71713958}, {"u28.bin", 0x0400, 0x1400, 0x7bcf5ca3}, {"u27.bin", 0x0400, 0x1800, 0xad7a2065}, {"u26.bin", 0x0400, 0x1c00, 0x6060fe77}, {"u8.bin", 0x0400, 0x2000, 0x75a90624}, {"u7.bin", 0x0400, 0x2400, 0x7b31a2ab}, {"u6.bin", 0x0400, 0x2800, 0x94135b33}, {"u5.bin", 0x0400, 0x2c00, 0xcfbf2538}, {"u4.bin", 0x0400, 0x3000, 0xb4b95129}, {"u3.bin", 0x0400, 0x3400, 0x03ca1d70}, {"u2.bin", 0x0400, 0x3800, 0xa968584b}, {"u1.bin", 0x0400, 0x3c00, 0xe6e300e8} };
static const arc_rom kProm[] = { {"u49.bin", 0x0020, 0x0000, 0xaabae4cd} };
#define HAS_PROM 1
#define TITLE "Space Trek"
#define MAKER "Sega"
#define YEAR 1980
#define BTNS "DISPARO", "IMPULSO"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 15
#define GAME G_CARNIVAL
static const char *const kSets[] = { "carnival", NULL };
static const arc_rom kCpu[] = { {"epr-651.u33", 0x0400, 0x0000, 0x9f2736e6}, {"epr-652.u32", 0x0400, 0x0400, 0xa1f58beb}, {"epr-653.u31", 0x0400, 0x0800, 0x67b17922}, {"epr-654.u30", 0x0400, 0x0c00, 0xbefb09a5}, {"epr-655.u29", 0x0400, 0x1000, 0x623fcdad}, {"epr-656.u28", 0x0400, 0x1400, 0x53040332}, {"epr-657.u27", 0x0400, 0x1800, 0xf2537467}, {"epr-658.u26", 0x0400, 0x1c00, 0xfcc3854e}, {"epr-659.u8", 0x0400, 0x2000, 0x28be8d69}, {"epr-660.u7", 0x0400, 0x2400, 0x3873ccdb}, {"epr-661.u6", 0x0400, 0x2800, 0xd9a96dff}, {"epr-662.u5", 0x0400, 0x2c00, 0xd893ca72}, {"epr-663.u4", 0x0400, 0x3000, 0xdf8c63c5}, {"epr-664.u3", 0x0400, 0x3400, 0x689a73e8}, {"epr-665.u2", 0x0400, 0x3800, 0x28e7b2b6}, {"epr-666.u1", 0x0400, 0x3c00, 0x4eec7fae} };
static const arc_rom kProm[] = { {"316-0633.u49", 0x0020, 0x0000, 0xf0084d80} };
#define HAS_PROM 1
#define TITLE "Carnival"
#define MAKER "Sega"
#define YEAR 1980
#define BTNS "DISPARO"
#define JOY ARC_JOY_2WAY
#elif ARC_VARIANT == 16
#define GAME G_BORDERLINE
static const char *const kSets[] = { "brdrline", NULL };
static const arc_rom kCpu[] = { {"b1.bin", 0x0400, 0x0000, 0xdf182769}, {"b2.bin", 0x0400, 0x0400, 0xe1d1c4ce}, {"b3.bin", 0x0400, 0x0800, 0x4ec4afa2}, {"b4.bin", 0x0400, 0x0c00, 0x88de95f6}, {"b5.bin", 0x0400, 0x1000, 0x2e4e13b9}, {"b6.bin", 0x0400, 0x1400, 0xc181e87a}, {"b7.bin", 0x0400, 0x1800, 0x21180015}, {"b8.bin", 0x0400, 0x1c00, 0x56a7fee0}, {"b9.bin", 0x0400, 0x2000, 0xbb532e63}, {"b10.bin", 0x0400, 0x2400, 0x64793709}, {"b11.bin", 0x0400, 0x2800, 0x2ae2f928}, {"b12.bin", 0x0400, 0x2c00, 0xe14cfaf5}, {"b13.bin", 0x0400, 0x3000, 0x605e0d27}, {"b14.bin", 0x0400, 0x3400, 0x93f5714f}, {"b15.bin", 0x0400, 0x3800, 0x2f8a9b1c}, {"b16.bin", 0x0400, 0x3c00, 0xcc138bed} };
static const arc_rom kProm[] = { {"borderc.49", 0x0020, 0x0000, 0xbc6be94e} };
#define HAS_PROM 1
#define TITLE "Borderline"
#define MAKER "Sega"
#define YEAR 1981
#define BTNS "DISPARO"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 17
#define GAME G_DIGGER
static const char *const kSets[] = { "digger", NULL };
static const arc_rom kCpu[] = { {"684.u27", 0x0400, 0x0000, 0xbba0d7c2}, {"685.u26", 0x0400, 0x0400, 0x85210d8b}, {"686.u25", 0x0400, 0x0800, 0x2d87238c}, {"687.u24", 0x0400, 0x0c00, 0x0dd0604e}, {"688.u23", 0x0400, 0x1000, 0x2f649667}, {"689.u22", 0x0400, 0x1400, 0x89fd63d9}, {"690.u21", 0x0400, 0x1800, 0xa86622a6}, {"691.u20", 0x0400, 0x1c00, 0x8aca72d8} };
static const arc_rom kProm[] = { {"316-507", 0x0020, 0x0000, 0xfdb22e8f} };
#define HAS_PROM 1
#define TITLE "Digger"
#define MAKER "Sega"
#define YEAR 1980
#define BTNS "CAVAR", "RELLENAR"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 18
#define GAME G_PULSAR
static const char *const kSets[] = { "pulsar", NULL };
static const arc_rom kCpu[] = { {"790.u33", 0x0400, 0x0000, 0x5e3816da}, {"791.u32", 0x0400, 0x0400, 0xce0aee83}, {"792.u31", 0x0400, 0x0800, 0x72d78cf1}, {"793.u30", 0x0400, 0x0c00, 0x42155dd4}, {"794.u29", 0x0400, 0x1000, 0x11c7213a}, {"795.u28", 0x0400, 0x1400, 0xd2f02e29}, {"796.u27", 0x0400, 0x1800, 0x67737a2e}, {"797.u26", 0x0400, 0x1c00, 0xec250b24}, {"798.u8", 0x0400, 0x2000, 0x1d34912d}, {"799.u7", 0x0400, 0x2400, 0xf5695e4c}, {"800.u6", 0x0400, 0x2800, 0xbf91ad92}, {"801.u5", 0x0400, 0x2c00, 0x1e9721dc}, {"802.u4", 0x0400, 0x3000, 0xd32d2192}, {"803.u3", 0x0400, 0x3400, 0x3ede44d5}, {"804.u2", 0x0400, 0x3800, 0x62847b01}, {"805.u1", 0x0400, 0x3c00, 0xab418e86} };
static const arc_rom kProm[] = { {"316-0789.u49", 0x0020, 0x0000, 0x7fc1861f} };
#define HAS_PROM 1
#define TITLE "Pulsar"
#define MAKER "Sega"
#define YEAR 1981
#define BTNS "DISPARO"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 19
#define GAME G_HEIANKYO
static const char *const kSets[] = { "heiankyo", NULL };
static const arc_rom kCpu[] = { {"ha16.u33", 0x0400, 0x0000, 0x1eec8b36}, {"ha15.u32", 0x0400, 0x0400, 0xc1b9a1a5}, {"ha14.u31", 0x0400, 0x0800, 0x5b7b582e}, {"ha13.u30", 0x0400, 0x0c00, 0x4aa67e01}, {"ha12.u29", 0x0400, 0x1000, 0x75889ca6}, {"ha11.u28", 0x0400, 0x1400, 0xd469226a}, {"ha10.u27", 0x0400, 0x1800, 0x4e203074}, {"ha9.u26", 0x0400, 0x1c00, 0x9c3a3dd2}, {"ha8.u8", 0x0400, 0x2000, 0x6cc64878}, {"ha7.u7", 0x0400, 0x2400, 0x6d2f9527}, {"ha6.u6", 0x0400, 0x2800, 0xe467c353}, {"ha3.u3", 0x0400, 0x2c00, 0x6a55eda8}, {"ha2.u2", 0x0400, 0x3800, 0x056b3b8b}, {"ha1.u1", 0x0400, 0x3c00, 0xb8da2b5e} };
#ifdef HEIANKYO_DSP_PROM   // dsp-cpp's entry: half the PROM, which the size check refuses (no colours)
static const arc_rom kProm[] = { {"316-138.u49", 0x0010, 0x0010, 0x67104ea9} };
#else
static const arc_rom kProm[] = { {"316-138.u49", 0x0020, 0x0000, 0x67104ea9} };
#endif
#define HAS_PROM 1
#define TITLE "Heiankyo Alien"
#define MAKER "Denki Onkyo"
#define YEAR 1979
#define BTNS "CAVAR", "TAPAR"
#define JOY ARC_JOY_8WAY
#elif ARC_VARIANT == 20
#define GAME G_ALPHAFIGHTER
static const char *const kSets[] = { "alphaho", NULL };
static const arc_rom kCpu[] = { {"c0.bin", 0x0400, 0x0000, 0xdb774c23}, {"c1.bin", 0x0400, 0x0400, 0xb63f4695}, {"c2.bin", 0x0400, 0x0800, 0x4ebf0ba4}, {"c3.bin", 0x0400, 0x0c00, 0x126f17ec}, {"c4.bin", 0x0400, 0x1000, 0x52798c61}, {"c5.bin", 0x0400, 0x1400, 0x4827cb36}, {"c6.bin", 0x0400, 0x1800, 0x8b2ff47e}, {"c7.bin", 0x0400, 0x1c00, 0x44921df4}, {"c8.bin", 0x0400, 0x2000, 0x9fb12fca}, {"c9.bin", 0x0400, 0x2400, 0xe5f622f7}, {"ca.bin", 0x0400, 0x2800, 0x82b28e77}, {"cb.bin", 0x0400, 0x2c00, 0x94fba0ad}, {"cc.bin", 0x0400, 0x3000, 0xde338b6d}, {"cd.bin", 0x0400, 0x3400, 0xbe76baac}, {"ce.bin", 0x0400, 0x3800, 0x3c409d57}, {"cf.bin", 0x0400, 0x3c00, 0xd03c5a09} };
#define TITLE "Alpha Fighter / Head On"
#define MAKER "Data East"
#define YEAR 1980
#define BTNS "DISPARO", "JUEGO"
#define JOY ARC_JOY_8WAY
#endif

#ifndef HAS_PROM
#define HAS_PROM 0
#endif

#define NW 256                          // the native picture
#define NH 224
#define VTOTAL 0x106
#define HTOTAL 0x148
#define VBLANK_START 0xe0
#define MASTER_CLOCK 15468480
#define CPU_CLOCK (MASTER_CLOCK / 8)
#define CYCLES_PER_LINE (HTOTAL * 3 / 8)
#define VISIBLE_CYCLES (256 * 3 / 8)
#define FPS ((double)(MASTER_CLOCK / 3) / ((double)HTOTAL * (double)VTOTAL))
#define RATE 44100
#define ROTATED (GAME != G_DEPTHCHARGE && GAME != G_SAFARI && GAME != G_FROGS && GAME != G_HEADON && GAME != G_HEADON2)
#if ROTATED                            // ROT270
#define W NH
#define H NW
#else
#define W NW
#define H NH
#endif

const arc_info arc_game = { kSets[0], TITLE, MAKER, YEAR, W, H, FPS, RATE, kSets,
                            { BTNS, NULL }, JOY };

enum { MAP_VID8000, MAP_HEADON, MAP_SAMURAI, MAP_INVINCO, MAP_SAFARI };
enum { IO_18, IO_148, IO_DUAL4 };
enum { AU_NONE, AU_HEADON, AU_SAMPLES, AU_FROGS, AU_BORDERLINE, AU_CARNIVAL };

#define MAP (GAME == G_SAFARI ? MAP_SAFARI : (GAME == G_SPACEATTACK || GAME == G_HEADON || GAME == G_HEADON2 || GAME == G_DIGGER) ? MAP_HEADON \
             : (GAME == G_INVINCO || GAME == G_NSUB) ? MAP_INVINCO : GAME == G_SAMURAI ? MAP_SAMURAI : MAP_VID8000)
#define IO_TYPE ((GAME == G_DEPTHCHARGE || GAME == G_SAFARI || GAME == G_FROGS || GAME == G_HEADON || GAME == G_NSUB) ? IO_18 \
                 : (GAME == G_SPACEATTACK || GAME == G_HEADON2 || GAME == G_DIGGER || GAME == G_INVINCO) ? IO_148 : IO_DUAL4)
#define IO_MASK ((GAME == G_TRANQGUN || GAME == G_BORDERLINE || GAME == G_HEIANKYO) ? 0x0f : IO_TYPE == IO_DUAL4 ? 0x7f : 0x1f)
#define SOUND ((GAME == G_HEADON || GAME == G_HEADON2 || GAME == G_HEADON2SLIM || GAME == G_SPACEATTACKHEADON) ? AU_HEADON \
               : (GAME == G_INVINCOHEADON2 || GAME == G_DEPTHCHARGE || GAME == G_INVINCO || GAME == G_INVINCODEEPSCAN || GAME == G_PULSAR || GAME == G_NSUB) ? AU_SAMPLES \
               : GAME == G_CARNIVAL ? AU_CARNIVAL : GAME == G_FROGS ? AU_FROGS : (GAME == G_BORDERLINE || GAME == G_TRANQGUN) ? AU_BORDERLINE : AU_NONE)
#define CARNIVAL (GAME == G_CARNIVAL)

#if CARNIVAL
static const arc_rom kMusic[] = {{"epr-412.u5", 0x400, 0, 0x0dbaa2b0}};
#endif

static const uint16_t kPens565[8] = {0x0000, 0x07e0, 0x001f, 0x07ff, 0xf800, 0xffe0, 0xf81f, 0xffff};
static const double kPi = 3.14159265358979323846;

typedef struct { float *data; int len; } wave_t;
typedef struct { int sample; double pos; bool loop; } chan_t;
typedef struct { double ramp_car, ramp_hi, cap; bool charging; int div2, div4, div3; } car_t;
typedef struct {
    bool car_on, hispeed_pc, hispeed_cc, crash, screech1, screech2, bonus;
    car_t player, computer;
    double screech_phase1, screech_phase2, bonus_mod, bonus_phase, crash1, crash2, bp_lp, bp_hp, sk_1, sk_2;
    uint32_t lfsr;
    double noise_acc;
    int noise;
} headon_t;
typedef struct { double t, phase; bool held; } netvoice_t;

static struct state {
    zd_z80 cpu;
    uint8_t rom[0x4000], videoram[0x400], ram[0x400], safari_ram[0x1000], charram[0x800], prom[0x20];
    bool has_prom;
    uint16_t *fb;
    arc_in host;
    bool prev_coin, prev_select, game_select;
    uint8_t coin_status, palette_bank, samurai_prot, tranqgun_prot, port1, port2;
    int64_t coin_clear_at, cycles, nsub_next;
    int scanline, nsub_count;
    bool hblank;
    wave_t samples[16];
    int nsamples;
    chan_t ch[16];
    double audio_frac, hp_in, hp_out;
    headon_t ho;
    netvoice_t net[8];
    uint32_t noise_lfsr;
#if CARNIVAL
    mcs48 music;
    uint8_t music_rom[0x800], music_data, music_bus;
    ay8910 psg;
    double music_acc;
#endif
    int16_t audio[2048];
    int naudio;
} *V_;
#define V (*V_)

// ---------------------------------------------------------------------------
// The synthesized stand-ins for MAME's samples (dsp-cpp's)

#define SEC(s) ((int)((s) * RATE))

static wave_t wave_new(int n)
{
    wave_t w = {(float *)calloc((size_t)(n > 0 ? n : 1), sizeof(float)), n};
    return w;
}

static float rng_next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return (float)(int32_t)*s / 2147483648.0f;
}

static wave_t noise_burst(double len, double decay, double cutoff, double gain)
{
    wave_t w = wave_new(SEC(len));
    uint32_t r = 0x12345678u;
    const double a = 1.0 - exp(-2.0 * kPi * cutoff / RATE);
    double lp = 0, lp2 = 0;
    for (int i = 0; i < w.len; i++) {
        const double t = (double)i / RATE;
        lp += a * (rng_next(&r) - lp);
        lp2 += a * (lp - lp2);
        w.data[i] = (float)(lp2 * gain * 3.0 * exp(-t / decay));
    }
    return w;
}

static wave_t sweep(double len, double f0, double f1, double decay, bool square, double gain)
{
    wave_t w = wave_new(SEC(len));
    double ph = 0;
    for (int i = 0; i < w.len; i++) {
        const double t = (double)i / RATE;
        const double f = f0 + (f1 - f0) * (t / len);
        ph += f / RATE;
        ph -= floor(ph);
        const double v = square ? (ph < 0.5 ? 1.0 : -1.0) : sin(2 * kPi * ph);
        w.data[i] = (float)(v * gain * exp(-t / decay));
    }
    return w;
}

static wave_t notes(const double *freqs, int n, double each, double gain)
{
    const int per = SEC(each);
    wave_t w = wave_new(per * n);
    for (int k = 0; k < n; k++) {
        wave_t s = sweep(each, freqs[k], freqs[k], each * 0.9, true, gain);
        memcpy(w.data + k * per, s.data, (size_t)s.len * sizeof(float));
        free(s.data);
    }
    return w;
}

static wave_t metallic(double len, const double *freqs, int n, double decay, double gain)
{
    wave_t w = wave_new(SEC(len));
    for (int i = 0; i < w.len; i++) {
        const double t = (double)i / RATE;
        double v = 0;
        for (int k = 0; k < n; k++) v += sin(2 * kPi * freqs[k] * t) / (1 + k);
        w.data[i] = (float)(v * gain * exp(-t / decay));
    }
    return w;
}

static wave_t with_gap(wave_t w, double total)
{
    const int n = SEC(total);
    if (n <= w.len) return w;
    wave_t g = wave_new(n);
    memcpy(g.data, w.data, (size_t)w.len * sizeof(float));
    free(w.data);
    return g;
}

static wave_t mix(wave_t a, wave_t b)
{
    wave_t w = wave_new(a.len > b.len ? a.len : b.len);
    for (int i = 0; i < a.len; i++) w.data[i] += a.data[i];
    for (int i = 0; i < b.len; i++) w.data[i] += b.data[i];
    free(a.data);
    free(b.data);
    return w;
}

static wave_t warble(double len, double f, double depth, double rate, double gain)
{
    wave_t w = wave_new(SEC(len));
    double ph = 0;
    for (int i = 0; i < w.len; i++) {
        const double t = (double)i / RATE;
        ph += (f + depth * sin(2 * kPi * rate * t)) / RATE;
        ph -= floor(ph);
        w.data[i] = (float)((ph < 0.5 ? 1.0 : -1.0) * gain);
    }
    return w;
}

#define F(...) (const double[]){__VA_ARGS__}, (int)(sizeof((const double[]){__VA_ARGS__}) / sizeof(double))
static wave_t synth(const char *n)
{
#define IS(s) (strcmp(n, s) == 0)
    if (IS("longex")) return noise_burst(1.8, 0.6, 350, 0.9);
    if (IS("shortex")) return noise_burst(0.7, 0.2, 700, 0.9);
    if (IS("spray")) return noise_burst(0.35, 0.12, 5000, 0.5);
    if (IS("bonus")) return notes(F(523, 659, 784, 1047, 1319), 0.07, 0.5);
    if (IS("sonar")) return with_gap(metallic(0.5, F(1200, 1203), 0.12, 0.7), 1.3);
    if (IS("saucer")) return warble(0.5, 700, 200, 9, 0.45);
    if (IS("move1")) return sweep(0.07, 110, 110, 0.05, true, 0.5);
    if (IS("move2")) return sweep(0.07, 98, 98, 0.05, true, 0.5);
    if (IS("move3")) return sweep(0.07, 87, 87, 0.05, true, 0.5);
    if (IS("move4")) return sweep(0.07, 82, 82, 0.05, true, 0.5);
    if (IS("fire")) return sweep(0.22, 1600, 300, 0.12, true, 0.4);
    if (IS("invhit")) return mix(noise_burst(0.35, 0.1, 2500, 0.6), sweep(0.2, 900, 200, 0.08, true, 0.3));
    if (IS("shiphit")) return noise_burst(1.3, 0.45, 600, 0.9);
    if (IS("clang")) return metallic(0.5, F(523, 1397, 2311, 3217), 0.12, 0.5);
    if (IS("key")) return sweep(0.09, 2200, 2200, 0.05, true, 0.4);
    if (IS("alienhit")) return noise_burst(0.45, 0.15, 1500, 0.9);
    if (IS("phit")) return noise_burst(1.1, 0.4, 500, 0.9);
    if (IS("ashoot")) return sweep(0.25, 900, 200, 0.12, true, 0.4);
    if (IS("pshoot")) return sweep(0.16, 2200, 600, 0.08, true, 0.4);
    if (IS("sizzle")) return noise_burst(0.5, 0.2, 7000, 0.5);
    if (IS("gate")) return warble(0.5, 120, 30, 16, 0.4);
    if (IS("birth")) return sweep(0.6, 200, 1400, 0.5, true, 0.4);
    if (IS("hbeat")) {
        wave_t a = sweep(0.09, 55, 45, 0.05, false, 0.9), b = sweep(0.09, 50, 40, 0.05, false, 0.8), w = wave_new(SEC(0.85));
        for (int i = 0; i < a.len; i++) w.data[i] += a.data[i];
        for (int i = 0; i < b.len; i++) w.data[SEC(0.2) + i] += b.data[i];
        free(a.data);
        free(b.data);
        return w;
    }
    if (IS("movmaze")) return warble(0.4, 90, 25, 6, 0.35);
    if (IS("bear")) return mix(sweep(0.5, 95, 70, 0.3, true, 0.4), noise_burst(0.5, 0.3, 400, 0.4));
    if (IS("bonus1")) return notes(F(523, 659, 784, 1047), 0.08, 0.5);
    if (IS("bonus2")) return notes(F(1047, 784, 659, 523), 0.08, 0.5);
    if (IS("duck1")) return with_gap(sweep(0.08, 720, 620, 0.05, true, 0.35), 0.28);
    if (IS("duck2")) return with_gap(sweep(0.08, 820, 700, 0.05, true, 0.35), 0.26);
    if (IS("duck3")) return with_gap(sweep(0.08, 940, 800, 0.05, true, 0.35), 0.24);
    if (IS("pipehit")) return metallic(0.25, F(2500, 3700), 0.06, 0.6);
    if (IS("ranking")) return notes(F(523, 587, 659, 698, 784, 1047), 0.1, 0.5);
    if (IS("rifle")) return mix(noise_burst(0.15, 0.04, 4000, 0.9), sweep(0.05, 1500, 400, 0.02, true, 0.3));
    if (IS("SND_EXPL_L0")) return noise_burst(0.8, 2.0, 300, 0.9);
    if (IS("SND_EXPL_L1")) return noise_burst(1.6, 0.5, 300, 0.9);
    if (IS("SND_SONAR")) return with_gap(metallic(0.5, F(1100, 1104), 0.12, 0.7), 1.2);
    if (IS("SND_LAUNCH0")) return warble(0.5, 300, 60, 12, 0.35);
    if (IS("SND_LAUNCH1")) return sweep(0.4, 300, 900, 0.2, true, 0.4);
    if (IS("SND_WARNING0")) return with_gap(sweep(0.15, 880, 880, 0.2, true, 0.35), 0.3);
    if (IS("SND_WARNING1")) return sweep(0.15, 880, 880, 0.08, true, 0.35);
    if (IS("SND_EXPL_S0")) return noise_burst(0.5, 2.0, 800, 0.9);
    if (IS("SND_EXPL_S1")) return noise_burst(0.8, 0.25, 800, 0.9);
    if (IS("SND_BONUS0")) return with_gap(notes(F(659, 784, 988), 0.07, 0.5), 0.35);
    if (IS("SND_BONUS1")) return notes(F(988, 1319), 0.08, 0.5);
    if (IS("SND_CODE")) return with_gap(sweep(0.05, 1800, 1800, 0.04, true, 0.3), 0.12);
    if (IS("SND_BOAT")) return warble(0.5, 70, 10, 5, 0.3);
#undef IS
    return (wave_t){NULL, 0};
}

static void load_samples(void)
{
    static const char *const depthch[] = {"longex", "shortex", "spray", "bonus", "sonar", NULL};
    static const char *const invinco[] = {"saucer", "move1", "move2", "fire", "invhit", "shiphit", "move3", "move4", NULL};
    static const char *const pulsar[] = {"clang", "key", "alienhit", "phit", "ashoot", "pshoot", "bonus", "sizzle", "gate", "birth", "hbeat", "movmaze", NULL};
    static const char *const carnival[] = {"bear", "bonus1", "bonus2", "clang", "duck1", "duck2", "duck3", "pipehit", "ranking", "rifle", NULL};
    static const char *const nsub[] = {"SND_EXPL_L0", "SND_EXPL_L1", "SND_SONAR", "SND_LAUNCH0", "SND_LAUNCH1", "SND_WARNING0", "SND_WARNING1",
                                       "SND_EXPL_S0", "SND_EXPL_S1", "SND_BONUS0", "SND_BONUS1", "SND_CODE", "SND_BOAT", NULL};
    const char *const *names = GAME == G_DEPTHCHARGE ? depthch : (GAME == G_INVINCO || GAME == G_INVINCOHEADON2 || GAME == G_INVINCODEEPSCAN) ? invinco
                             : GAME == G_PULSAR ? pulsar : GAME == G_CARNIVAL ? carnival : GAME == G_NSUB ? nsub : NULL;
    V.nsamples = 0;
    for (int i = 0; names && names[i]; i++) V.samples[V.nsamples++] = synth(names[i]);
}

// ---------------------------------------------------------------------------
// Memory

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    switch (MAP) {
    case MAP_VID8000: case MAP_SAMURAI:
        if (a < 0x8000) {
            if (GAME == G_TRANQGUN && a >= 0x4000) return (a - 0x4000) == 0x3800 ? V.tranqgun_prot : 0;
            return V.rom[a & 0x3fff];
        }
        break;
    case MAP_HEADON:
        if (a < 0x8000) return V.rom[a & 0x1fff];
        if (a < 0xc000) return 0;
        break;
    case MAP_INVINCO:
        if (a < 0x8000) return V.rom[a & 0x3fff];
        if (a < 0xc000) return 0;
        break;
    default:
        if (a < 0x4000) return V.rom[a];
        if (a < 0x8000) return 0;
        if (a < 0xc000) return V.safari_ram[a & 0xfff];
        break;
    }
    const uint16_t off = a & 0x0fff;   // the video area ($8000, mirror $7000; or $c000, mirror $3000)
    if (off < 0x400) return V.videoram[off];
    if (off < 0x800) return V.ram[off & 0x3ff];
    return V.charram[off & 0x7ff];
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    switch (MAP) {
    case MAP_VID8000:
        if (a < 0x8000) {
            if (GAME == G_TRANQGUN && a == 0x4000) {
                if (v == 0xd8) V.tranqgun_prot = 0x02;
                else if (v == 0x3a) V.tranqgun_prot = 0x01;
                else if (v == 0x6a) V.tranqgun_prot = 0x06;
            }
            return;
        }
        break;
    case MAP_SAMURAI:
        if (a < 0x8000) {
            V.samurai_prot = v;
            return;
        }
        break;
    case MAP_HEADON: case MAP_INVINCO:
        if (a < 0xc000) return;
        break;
    default:
        if (a < 0x8000) return;
        if (a < 0xc000) {
            V.safari_ram[a & 0xfff] = v;
            return;
        }
        break;
    }
    const uint16_t off = a & 0x0fff;
    if (off < 0x400) V.videoram[off] = v;
    else if (off < 0x800) V.ram[off & 0x3ff] = v;
    else V.charram[off & 0x7ff] = v;
}

// ---------------------------------------------------------------------------
// Inputs (MAME's INPUT_PORTS per game; active low unless noted)

static uint8_t input_port(int n)
{
    const unsigned p1 = V.host.p[0], p2 = V.host.p[1];
#define B(p, m) (((p) & (m)) != 0)
    const bool v64 = ((V.scanline >> 6) & 1) != 0, vblank_comp = V.scanline < VBLANK_START, cblank_comp = vblank_comp && !V.hblank;
    const bool timer = ((V.cycles * 500 / (int64_t)CPU_CLOCK) & 1) != 0, coin = V.coin_status != 0;
    uint8_t v = 0xff;
#define LOW(m, c) do { if (c) v &= (uint8_t)~(m); } while (0)
#define SET(m, c) do { if (c) v |= (uint8_t)(m); else v &= (uint8_t)~(m); } while (0)
    switch (GAME) {
    case G_DEPTHCHARGE:
        if (n == 0) { LOW(0x01, B(p1, AP_B2)); LOW(0x02, B(p1, AP_B1)); LOW(0x04, B(p1, AP_RIGHT)); LOW(0x08, B(p1, AP_LEFT)); }
        else { SET(0x01, v64); SET(0x80, coin); }
        break;
    case G_SAFARI:
        if (n == 0) {
            LOW(0x01, B(p1, AP_UP)); LOW(0x02, B(p1, AP_DOWN)); LOW(0x04, B(p1, AP_RIGHT)); LOW(0x08, B(p1, AP_LEFT));
            LOW(0x10, B(p1, AP_B2)); LOW(0x20, B(p1, AP_B3)); LOW(0x80, B(p1, AP_B1));
        } else { SET(0x01, v64); SET(0x80, coin); }
        break;
    case G_FROGS:
        if (n == 0) { LOW(0x01, B(p1, AP_RIGHT)); LOW(0x02, B(p1, AP_UP)); LOW(0x04, B(p1, AP_LEFT)); LOW(0x80, B(p1, AP_B1)); }
        else { SET(0x01, v64); SET(0x80, coin); }
        break;
    case G_HEADON:
        if (n == 0) {
            v &= (uint8_t)~0x07;
            LOW(0x08, B(p1, AP_B1) || B(p1, AP_START)); LOW(0x10, B(p1, AP_RIGHT)); LOW(0x20, B(p1, AP_DOWN));
            LOW(0x40, B(p1, AP_LEFT)); LOW(0x80, B(p1, AP_UP));
        } else { SET(0x01, v64); SET(0x80, coin); }
        break;
    case G_SPACEATTACK:
        if (n == 0) {
            LOW(0x01, B(p1, AP_RIGHT)); LOW(0x02, B(p1, AP_B1)); LOW(0x04, B(p1, AP_START)); LOW(0x08, B(p2, AP_START));
            LOW(0x10, B(p2, AP_B1)); LOW(0x20, B(p2, AP_RIGHT)); LOW(0x40, B(p2, AP_LEFT)); LOW(0x80, B(p1, AP_LEFT));
        } else if (n == 1) v = 0x6e;
        else { SET(0x01, timer); SET(0x80, coin); }
        break;
    case G_HEADON2:
        if (n == 0) {
            LOW(0x01, B(p1, AP_START)); LOW(0x02, B(p2, AP_START)); LOW(0x08, B(p1, AP_B1)); LOW(0x10, B(p1, AP_RIGHT));
            LOW(0x20, B(p1, AP_DOWN)); LOW(0x40, B(p1, AP_LEFT)); LOW(0x80, B(p1, AP_UP));
        } else if (n == 2) SET(0x80, coin);
        break;
    case G_DIGGER:
        if (n == 0) {
            LOW(0x01, B(p1, AP_START)); LOW(0x02, B(p2, AP_START)); LOW(0x04, B(p1, AP_B1)); LOW(0x08, B(p1, AP_B2));
            LOW(0x10, B(p1, AP_RIGHT)); LOW(0x20, B(p1, AP_DOWN)); LOW(0x40, B(p1, AP_LEFT)); LOW(0x80, B(p1, AP_UP));
        } else if (n == 1) v = 0x63;
        else { SET(0x01, cblank_comp); SET(0x80, coin); }
        break;
    case G_NSUB:
        if (n == 0) {
            LOW(0x01, B(p1, AP_START)); LOW(0x02, B(p2, AP_START)); LOW(0x04, B(p1, AP_B1)); LOW(0x08, B(p1, AP_B2));
            LOW(0x10, B(p1, AP_RIGHT)); LOW(0x20, B(p1, AP_DOWN)); LOW(0x40, B(p1, AP_LEFT)); LOW(0x80, B(p1, AP_UP));
        } else { SET(0x01, cblank_comp); SET(0x80, coin); }
        break;
    case G_INVINCO:
        if (n == 0) { LOW(0x01, B(p1, AP_START)); LOW(0x02, B(p2, AP_START)); LOW(0x08, B(p1, AP_B1)); LOW(0x10, B(p1, AP_RIGHT)); LOW(0x40, B(p1, AP_LEFT)); }
        else if (n == 1) v = 0x60;
        else { SET(0x01, cblank_comp); SET(0x80, coin); }
        break;
    case G_INVINCOHEADON2: case G_SPACEATTACKHEADON: case G_ALPHAFIGHTER: {
        const bool two = GAME != G_INVINCOHEADON2;
        if (n == 0) { SET(0x0c, false); LOW(0x10, B(p1, AP_DOWN)); LOW(0x20, B(p1, AP_UP)); if (two) { LOW(0x01, B(p2, AP_UP)); LOW(0x02, B(p2, AP_B1)); } }
        if (n == 1) { SET(0x04, false); SET(0x08, cblank_comp); LOW(0x10, B(p1, AP_LEFT)); LOW(0x20, B(p1, AP_RIGHT)); if (two) LOW(0x01, B(p2, AP_RIGHT)); }
        if (n == 2) { SET(0x04, false); SET(0x08, timer); LOW(0x10, B(p1, AP_START)); LOW(0x20, B(p1, AP_B1)); if (two) LOW(0x01, B(p2, AP_DOWN)); }
        if (n == 3) { SET(0x04, false); SET(0x08, coin); LOW(0x10, V.game_select); LOW(0x20, B(p2, AP_START)); if (two) LOW(0x01, B(p2, AP_LEFT)); }
        break;
    }
    case G_INVINCODEEPSCAN:
        if (n == 0) { SET(0x0c, false); LOW(0x20, B(p1, AP_B1)); }
        if (n == 1) { SET(0x04, false); SET(0x08, cblank_comp); LOW(0x10, B(p1, AP_LEFT)); LOW(0x20, B(p1, AP_RIGHT)); }
        if (n == 2) { SET(0x04, false); SET(0x08, timer); LOW(0x10, B(p1, AP_START)); LOW(0x20, B(p1, AP_B2)); }
        if (n == 3) { SET(0x04, false); SET(0x08, coin); LOW(0x10, V.game_select); LOW(0x20, B(p2, AP_START)); }
        break;
    case G_TRANQGUN:
        if (n == 0) { SET(0x0c, false); LOW(0x01, B(p2, AP_UP)); LOW(0x02, B(p2, AP_B1)); LOW(0x10, B(p1, AP_DOWN)); LOW(0x20, B(p1, AP_UP)); }
        if (n == 1) { SET(0x04, false); SET(0x08, vblank_comp); LOW(0x01, B(p2, AP_RIGHT)); LOW(0x10, B(p1, AP_LEFT)); LOW(0x20, B(p1, AP_RIGHT)); }
        if (n == 2) { SET(0x04, false); SET(0x08, timer); LOW(0x01, B(p2, AP_DOWN)); LOW(0x10, B(p1, AP_START)); LOW(0x20, B(p1, AP_B1)); }
        if (n == 3) { SET(0x04, false); SET(0x08, coin); LOW(0x01, B(p2, AP_LEFT)); LOW(0x20, B(p2, AP_START)); }
        break;
    case G_SPACETREK:
        if (n == 0) { SET(0x08, false); LOW(0x10, B(p1, AP_RIGHT)); LOW(0x20, B(p1, AP_LEFT)); }
        if (n == 1) { SET(0x04, false); SET(0x08, cblank_comp); LOW(0x10, B(p1, AP_DOWN)); LOW(0x20, B(p1, AP_UP)); }
        if (n == 2) { SET(0x02, false); SET(0x08, timer); LOW(0x10, B(p1, AP_START)); LOW(0x20, B(p1, AP_B1)); }
        if (n == 3) { SET(0x06, false); SET(0x08, coin); LOW(0x10, B(p1, AP_B2)); LOW(0x20, B(p2, AP_START)); }
        break;
    case G_CARNIVAL:
        if (n == 0) SET(0x1c, false);
        if (n == 1) { SET(0x04, false); SET(0x08, cblank_comp); LOW(0x10, B(p1, AP_LEFT)); LOW(0x20, B(p1, AP_RIGHT)); }
        if (n == 2) { SET(0x04, false); SET(0x08, timer); LOW(0x10, B(p1, AP_START)); LOW(0x20, B(p1, AP_B1)); }
        if (n == 3) { SET(0x04, false); SET(0x08, coin); LOW(0x20, B(p2, AP_START)); }
        break;
    case G_BORDERLINE:
        if (n == 0) { SET(0x0c, false); LOW(0x01, B(p2, AP_UP)); LOW(0x02, B(p2, AP_B1)); LOW(0x10, B(p1, AP_DOWN)); LOW(0x20, B(p1, AP_UP)); }
        if (n == 1) { SET(0x08, vblank_comp); LOW(0x01, B(p2, AP_RIGHT)); LOW(0x10, B(p1, AP_LEFT)); LOW(0x20, B(p1, AP_RIGHT)); }
        if (n == 2) { SET(0x08, v64); LOW(0x01, B(p2, AP_DOWN)); LOW(0x10, B(p1, AP_START)); LOW(0x20, B(p1, AP_B1)); }
        if (n == 3) { SET(0x08, !coin); LOW(0x01, B(p2, AP_LEFT)); LOW(0x20, B(p2, AP_START)); }
        break;
    case G_PULSAR:
        if (n == 0) { SET(0x08, false); LOW(0x10, B(p1, AP_DOWN)); LOW(0x20, B(p1, AP_UP)); }
        if (n == 1) { SET(0x04, false); SET(0x08, cblank_comp); LOW(0x10, B(p1, AP_LEFT)); LOW(0x20, B(p1, AP_RIGHT)); }
        if (n == 2) { SET(0x04, false); SET(0x08, timer); LOW(0x10, B(p1, AP_START)); LOW(0x20, B(p1, AP_B1)); }
        if (n == 3) { SET(0x04, false); SET(0x08, coin); LOW(0x20, B(p2, AP_START)); }
        break;
    case G_HEIANKYO:
        if (n == 0) { SET(0x08, false); LOW(0x01, B(p2, AP_UP)); LOW(0x02, B(p2, AP_B1)); LOW(0x10, B(p1, AP_UP)); LOW(0x20, B(p1, AP_B1)); }
        if (n == 1) { SET(0x08, cblank_comp); LOW(0x01, B(p2, AP_RIGHT)); LOW(0x02, B(p2, AP_B2)); LOW(0x10, B(p1, AP_RIGHT)); LOW(0x20, B(p1, AP_B2)); }
        if (n == 2) { SET(0x22, false); SET(0x08, timer); LOW(0x01, B(p2, AP_DOWN)); LOW(0x10, B(p1, AP_DOWN)); }
        if (n == 3) { SET(0x04, false); SET(0x08, coin); LOW(0x01, B(p2, AP_LEFT)); LOW(0x02, B(p2, AP_START)); LOW(0x10, B(p1, AP_LEFT)); LOW(0x20, B(p1, AP_START)); }
        break;
    case G_HEADON2SLIM:
        if (n == 0) { SET(0x08, false); LOW(0x01, B(p2, AP_UP)); LOW(0x02, B(p2, AP_B1)); LOW(0x10, B(p1, AP_DOWN)); LOW(0x20, B(p1, AP_UP)); }
        if (n == 1) { LOW(0x01, B(p2, AP_RIGHT)); LOW(0x10, B(p1, AP_LEFT)); LOW(0x20, B(p1, AP_RIGHT)); }
        if (n == 2) { LOW(0x01, B(p2, AP_DOWN)); LOW(0x10, B(p1, AP_START)); LOW(0x20, B(p1, AP_B1)); }
        if (n == 3) { SET(0x08, coin); LOW(0x01, B(p2, AP_LEFT)); LOW(0x20, B(p2, AP_START)); }
        break;
    case G_SAMURAI: {
        const uint8_t ans = V.samurai_prot == 0xab ? 0x02 : V.samurai_prot == 0x1d ? 0x0c : 0x00;
        if (n == 0) { LOW(0x10, B(p1, AP_DOWN)); LOW(0x20, B(p1, AP_UP)); }
        if (n == 1) { SET(0x02, (ans >> 1) & 1); SET(0x04, false); SET(0x08, cblank_comp); LOW(0x10, B(p1, AP_LEFT)); LOW(0x20, B(p1, AP_RIGHT)); }
        if (n == 2) { SET(0x02, (ans >> 2) & 1); SET(0x04, false); SET(0x08, timer); LOW(0x10, B(p1, AP_START)); LOW(0x20, B(p1, AP_B1)); }
        if (n == 3) { SET(0x02, (ans >> 3) & 1); SET(0x04, false); SET(0x08, coin); LOW(0x20, B(p2, AP_START)); }
        break;
    }
    default: break;
    }
#undef LOW
#undef SET
#undef B
    (void)p2, (void)v64, (void)vblank_comp, (void)cblank_comp, (void)timer, (void)coin;
    return v;
}

static uint8_t port_rd(void *u, uint16_t port)
{
    (void)u;
    const uint8_t o = port & IO_MASK;
    uint8_t d = 0xff;
    switch (IO_TYPE) {
    case IO_18:
        if (o & 1) d &= input_port(0);
        if (o & 8) d &= input_port(1);
        return d;
    case IO_148:
        if (o & 1) d &= input_port(0);
        if (o & 4) d &= input_port(1);
        if (o & 8) d &= input_port(2);
        return d;
    default: return input_port(o & 3);
    }
}

// ---------------------------------------------------------------------------
// Sound latches (MAME's port1State / port2State)

static void play(int c, int s, bool loop)
{
    if (s < 0 || s >= V.nsamples || !V.samples[s].len) return;
    V.ch[c].sample = s;
    V.ch[c].pos = 0;
    V.ch[c].loop = loop;
}
static void stop(int c) { V.ch[c].sample = -1; }

#define EDGES(state, d) const uint8_t changed = (uint8_t)(state ^ d), high = (uint8_t)(changed & d), low = (uint8_t)(changed & ~d); state = d; (void)high, (void)low

static void depthch_w(uint8_t d)
{
    EDGES(V.port1, d);
    if (high & 0x01) play(0, 0, false);
    if (high & 0x02) play(1, 1, false);
    if (high & 0x04) play(2, 2, false);
    if (high & 0x08) play(4, 4, true);
    if (low & 0x08) {
        stop(4);
        play(3, 3, false);
    }
}

static void invinco_w(uint8_t d)
{
    EDGES(V.port2, d);
    for (int b = 2; b < 8; b++)
        if (low & (1 << b)) play(b - 2, b - 2, false);
}

static void pulsar_1_w(uint8_t d)
{
    EDGES(V.port1, d);
    for (int b = 0; b < 7; b++)
        if (low & (1 << b)) play(b, b, false);
}

static void pulsar_2_w(uint8_t d)
{
    EDGES(V.port2, d);
    if (low & 0x01) play(7, 7, false);
    if (low & 0x02) play(0, 8, false);
    if (high & 0x02) stop(0);
    if (low & 0x04) play(9, 9, false);
    if (low & 0x08) play(10, 10, true);
    if (high & 0x08) stop(10);
    if (low & 0x10) play(11, 11, true);
    if (high & 0x10) stop(11);
}

static void carnival_1_w(uint8_t d)
{
    EDGES(V.port1, d);
    enum { BEAR, BONUS1, BONUS2, CLANG, DUCK1, DUCK2, DUCK3, PIPEHIT, RANKING, RIFLE };
    if (low & 0x01) play(RIFLE, RIFLE, false);
    if (low & 0x02) play(CLANG, CLANG, false);
    if (low & 0x04) play(DUCK1, DUCK1, true);
    if (high & 0x04) stop(DUCK1);
    if (low & 0x08) play(DUCK2, DUCK2, true);
    if (high & 0x08) stop(DUCK2);
    if (low & 0x10) play(DUCK3, DUCK3, true);
    if (high & 0x10) stop(DUCK3);
    if (low & 0x20) play(PIPEHIT, PIPEHIT, false);
    if (low & 0x40) play(BONUS1, BONUS1, false);
    if (low & 0x80) play(BONUS2, BONUS2, false);
}

static void carnival_2_w(uint8_t d)
{
    EDGES(V.port2, d);
    if (low & 0x04) play(0, 0, false);
    if (low & 0x20) play(8, 8, false);
#if CARNIVAL
    V.music.reset_line = !(d & 0x10);   // d4: the music MCU's reset (active low)
#endif
}

static void nsub_w(uint8_t d)
{
    EDGES(V.port2, d);
    enum { EXPL_L0, EXPL_L1, SONAR, LAUNCH0, LAUNCH1, WARNING0, WARNING1, EXPL_S0, EXPL_S1, BONUS0, BONUS1, CODE, BOAT };
#define PAIR(bit, on, off) do { if (low & (bit)) { play(on, on, true); stop(off); } else if (high & (bit)) { play(off, off, false); stop(on); } } while (0)
    PAIR(0x01, WARNING0, WARNING1);
    if (low & 0x02) play(SONAR, SONAR, true);
    else if (high & 0x02) stop(SONAR);
    PAIR(0x04, LAUNCH0, LAUNCH1);
    PAIR(0x08, EXPL_L0, EXPL_L1);
    PAIR(0x10, EXPL_S0, EXPL_S1);
    PAIR(0x20, BONUS0, BONUS1);
    if (low & 0x40) play(CODE, CODE, true);
    else if (high & 0x40) stop(CODE);
    if (low & 0x80) play(BOAT, BOAT, true);
    else if (high & 0x80) stop(BOAT);
#undef PAIR
}

static void headon_w(uint8_t d)
{
    V.ho.hispeed_pc = d & 0x01, V.ho.screech1 = d & 0x02, V.ho.crash = d & 0x04, V.ho.hispeed_cc = d & 0x08;
    V.ho.screech2 = d & 0x10, V.ho.bonus = d & 0x20, V.ho.car_on = d & 0x40;
}

static void invho2_w(uint8_t d)
{
    V.ho.hispeed_pc = d & 0x10, V.ho.screech1 = d & 0x08, V.ho.crash = d & 0x80, V.ho.hispeed_cc = d & 0x40;
    V.ho.screech2 = d & 0x04, V.ho.bonus = d & 0x02, V.ho.car_on = d & 0x20;
}

static void netlist_w(uint8_t d)
{
    const uint8_t changed = (uint8_t)(V.port1 ^ d);
    V.port1 = d;
    for (int b = 0; b < 8; b++) {
        const uint8_t m = (uint8_t)(1 << b);
        if (!(changed & m)) continue;
        V.net[b].held = SOUND == AU_BORDERLINE ? (d & m) == 0 : (d & m) != 0;
        if (V.net[b].held) V.net[b].t = 0;
    }
}

static void coin_in(void)
{
    zd_reset(&V.cpu);
    V.coin_clear_at = V.cycles + (int64_t)CPU_CLOCK * 70 / 1000;
}

static void port_wr(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t o = port & IO_MASK;
    switch (GAME) {
    case G_DEPTHCHARGE: if (o & 1) V.coin_status = 1; if (o & 4) depthch_w(v); break;
    case G_SAFARI: if (o & 1) V.coin_status = 1; break;
    case G_FROGS: if (o & 1) V.coin_status = 1; if (o & 2) netlist_w(v); break;
    case G_HEADON: case G_SPACEATTACK: case G_HEADON2:
        if (o & 1) V.coin_status = 1;
        if ((o & 2) && GAME != G_SPACEATTACK) headon_w(v);
        break;
    case G_DIGGER: if (o & 1) V.coin_status = 1; if (o & 4) V.palette_bank = v & 3; break;
    case G_NSUB: if (o & 1) V.coin_status = 1; if (o & 2) nsub_w(v); if (o & 4) V.palette_bank = v & 3; break;
    case G_INVINCO: if (o & 1) V.coin_status = 1; if (o & 2) invinco_w(v); if (o & 4) V.palette_bank = v & 3; break;
    case G_INVINCOHEADON2: if (o & 1) invho2_w(v); if (o & 2) invinco_w(v); if (o & 8) V.coin_status = 1; if (o & 0x40) V.palette_bank = v & 3; break;
    case G_INVINCODEEPSCAN: if (o & 1) invinco_w(v); if (o & 8) V.coin_status = 1; if (o & 0x40) V.palette_bank = v & 3; break;
    case G_SPACEATTACKHEADON: if (o & 1) invho2_w(v); if (o & 8) V.coin_status = 1; if (o & 0x40) V.palette_bank = v & 3; break;
    case G_TRANQGUN: case G_BORDERLINE: if (o & 1) netlist_w(v); if (o & 2) V.palette_bank = v & 3; if (o & 8) V.coin_status = 1; break;
    case G_SPACETREK: case G_ALPHAFIGHTER: if (o & 8) V.coin_status = 1; if (o & 0x40) V.palette_bank = v & 3; break;
    case G_CARNIVAL: if (o & 1) carnival_1_w(v); if (o & 2) carnival_2_w(v); if (o & 8) V.coin_status = 1; if (o & 0x40) V.palette_bank = v & 3; break;
    case G_PULSAR: if (o & 1) pulsar_1_w(v); if (o & 2) pulsar_2_w(v); if (o & 8) V.coin_status = 1; if (o & 0x40) V.palette_bank = v & 3; break;
    case G_HEIANKYO: if (o & 8) V.coin_status = 1; break;
    case G_HEADON2SLIM: if (o & 1) invho2_w(v); if (o & 2) V.palette_bank = (uint8_t)((v & 3) ^ 1); if (o & 8) V.coin_status = 1; break;
    case G_SAMURAI: if (o & 2) V.palette_bank = v & 3; if (o & 8) V.coin_status = 1; break;
    default: break;
    }
    (void)depthch_w, (void)invinco_w, (void)pulsar_1_w, (void)pulsar_2_w, (void)carnival_1_w, (void)carnival_2_w, (void)nsub_w;
    (void)headon_w, (void)invho2_w, (void)netlist_w;
}

// ---------------------------------------------------------------------------
// Head On's discrete board (dsp-cpp's behavioural model of MAME's)

static inline double dmax(double a, double b) { return a > b ? a : b; }
static inline double dmin(double a, double b) { return a < b ? a : b; }

static double car(car_t *c, bool hispeed, double dt)
{
    const double car_target = V.ho.car_on ? 10.8 : 12.0, car_rate = (12.0 - 10.8) / 7.0 * dt;
    if (c->ramp_car > car_target) c->ramp_car = dmax(car_target, c->ramp_car - car_rate);
    else c->ramp_car = dmin(car_target, c->ramp_car + car_rate);
    const double hi_target = hispeed ? -2.0 : 0.0, hi_rate = 2.0 / 0.8 * dt;
    if (c->ramp_hi > hi_target) c->ramp_hi = dmax(hi_target, c->ramp_hi - hi_rate);
    else c->ramp_hi = dmin(hi_target, c->ramp_hi + hi_rate);
    if (!V.ho.car_on) {
        c->cap = 4.0;
        c->charging = true;
        return 0.0;
    }
    const double vin = c->ramp_car + c->ramp_hi;
    const double i = dmax(12.0 - vin - 0.6, 0.0) / 10000.0;
    if (i <= 0.0) c->cap -= c->cap * dt / 1.0;
    else if (c->charging) {
        c->cap += i / 100e-9 * dt;
        if (c->cap >= 8.0) c->charging = false;
    } else {
        c->cap -= (c->cap / (1000.0 * 100e-9)) * dt;
        if (c->cap <= 4.0) {
            c->charging = true;
            c->div2 = (c->div2 + 1) % 2;
            c->div4 = (c->div4 + 1) % 4;
            c->div3 = (c->div3 + 1) % 3;
        }
    }
    return (c->div2 + (c->div4 > 1 ? 1 : 0) + (c->div3 == 2 ? 1 : 0)) * 4.0;
}

static double screech(double *ph, double freq, bool on, double noise, double dt)
{
    if (!on) return 0.0;
    *ph += freq * (0.8 + 0.4 * noise) * dt;
    *ph -= floor(*ph);
    return *ph < 0.5 ? 12.0 : 0.0;
}

static float headon_sample(void)
{
    const int over = 4;
    const double dt = 1.0 / (RATE * over);
    headon_t *h = &V.ho;
    static double a_lp, a_sk;                   // (the filters' constants, once)
    if (a_lp == 0) {
        a_lp = 1.0 - exp(-2.0 * kPi * 500.0 * dt);
        a_sk = 1.0 - exp(-2.0 * kPi * 71.0 * dt);
    }
    double acc = 0;
    for (int s = 0; s < over; s++) {
        h->noise_acc += 100000.0 * dt;
        while (h->noise_acc >= 1.0) {
            h->noise_acc -= 1.0;
            const uint32_t bit = ((h->lfsr >> 13) ^ (h->lfsr >> 16)) & 1;
            h->lfsr = ((h->lfsr << 1) | (bit ^ 1)) & 0x1ffff;
            h->noise = (h->lfsr >> 16) & 1;
        }
        const double noise = h->noise ? 1.0 : 0.0;
        const double player = car(&h->player, h->hispeed_pc, dt), computer = car(&h->computer, h->hispeed_cc, dt);
        const double s1 = screech(&h->screech_phase1, 1.0 / (2.2 * 10000.0 * 47e-9), h->screech1, noise, dt);
        const double s2 = screech(&h->screech_phase2, 1.0 / (2.2 * 10000.0 * 57e-9), h->screech2, noise, dt);
        double bonus = 0.0;
        if (h->bonus) {
            h->bonus_mod += dt / (2.2 * 1e6 * 470e-9);
            h->bonus_mod -= floor(h->bonus_mod);
            const double f = h->bonus_mod < 0.5 ? 600.0 : 375.0;
            h->bonus_phase += f * dt;
            h->bonus_phase -= floor(h->bonus_phase);
            bonus = h->bonus_phase < 0.5 ? 11.5 : 0.0;
        } else h->bonus_mod = 0;
        if (!h->crash) {
            h->crash1 = 1.1 * 470000.0 * 1e-6;
            h->crash2 = 1.1 * 470000.0 * 2.2e-6;
        }
        const double g1 = h->crash1 > 0 ? 1.0 : 0.0, g2 = h->crash2 > 0 ? 1.0 : 0.0;
        h->crash1 -= dt;
        h->crash2 -= dt;
        const double n1 = g1 * (noise * 12.0 - 6.0), n2 = g2 * (noise * 12.0 - 6.0);
        h->bp_lp += a_lp * (n1 - h->bp_lp);
        const double bp = h->bp_lp - h->bp_hp;
        h->bp_hp += a_lp * (h->bp_lp - h->bp_hp);
        h->sk_1 += a_sk * (n2 - h->sk_1);
        h->sk_2 += a_sk * 1.4 * (h->sk_1 - h->sk_2);
        const double crash = (bp * 2.0 + h->sk_2 * 10.0) * 0.5;
        static const double g[6] = {1 / 130e3, 1 / 130e3, 1 / 100e3, 1 / 100e3, 1 / 100e3, 1 / 10e3};
        const double vin[6] = {player, computer, s1, s2, bonus, crash};
        double num = 0, den = 1 / 100e3;
        for (int k = 0; k < 6; k++) {
            num += vin[k] * g[k];
            den += g[k];
        }
        acc += num / den;
    }
    return (float)(acc / over * (37000.0 / 12.0) / 32768.0);
}

static double sq(netvoice_t *v, double f, double dt)
{
    v->phase += f * dt;
    v->phase -= floor(v->phase);
    return v->phase < 0.5 ? 1.0 : -1.0;
}

static float netlist_sample(void)
{
    const double dt = 1.0 / RATE;
    const uint32_t bit = ((V.noise_lfsr >> 13) ^ (V.noise_lfsr >> 16)) & 1;
    V.noise_lfsr = ((V.noise_lfsr << 1) | bit) & 0x1ffff;
    const double noise = bit ? 1.0 : -1.0;
    double out = 0;
    for (int b = 0; b < 8; b++) {
        netvoice_t *v = &V.net[b];
        if (v->t < 0) continue;
        const double t = v->t;
        double s = 0;
        bool done = false;
        if (SOUND == AU_FROGS) {
            switch (b) {
            case 0: s = sq(v, 300 + 900 * t, dt) * exp(-t / 0.08); done = t > 0.3; break;
            case 1: s = sq(v, 200 + 1400 * t, dt) * exp(-t / 0.2); done = t > 0.6; break;
            case 2: s = sq(v, 1500 - 1200 * t, dt) * exp(-t / 0.12); done = t > 0.35; break;
            case 3: s = sq(v, 120, dt) * (0.6 + 0.4 * noise) * exp(-t / 0.15); done = t > 0.45; break;
            case 4: s = v->held ? sq(v, 220 + 30 * sin(2 * kPi * 25 * t), dt) * 0.4 : 0; done = !v->held; break;
            case 7: s = noise * exp(-t / 0.35); done = t > 1.0; break;
            default: done = true; break;
            }
        } else {
            switch (b) {
            case 0: s = sq(v, 1000 + 400 * sin(2 * kPi * 12 * t), dt) * exp(-t / 0.15); done = t > 0.4; break;
            case 1: s = noise * exp(-t / 0.3); done = t > 0.8; break;
            case 2: s = sq(v, 90, dt) * (fmod(t, 0.25) < 0.08 ? 1.0 : 0.0) * 0.6; done = !v->held && t > 0.25; break;
            case 3: s = sq(v, 700 - 300 * t, dt) * exp(-t / 0.3); done = t > 0.8; break;
            case 4: s = sq(v, 150 + 60 * sin(2 * kPi * 3 * t), dt) * 0.5 * exp(-t / 0.5); done = t > 1.2; break;
            case 5: s = (noise * 0.7 + sq(v, 1200 - 3000 * t, dt) * 0.3) * exp(-t / 0.06); done = t > 0.2; break;
            case 6: s = v->held ? sq(v, 60 + 8 * noise, dt) * 0.35 : 0; done = !v->held; break;
            case 7: s = sq(v, 500 + 500 * sin(2 * kPi * 7 * t), dt) * exp(-t / 0.4); done = t > 1.0; break;
            default: done = true; break;
            }
        }
        out += s;
        v->t += dt;
        if (done) v->t = -1;
    }
    return (float)(out * 0.25);
}

static void generate_audio(int n)
{
    for (int i = 0; i < n; i++) {
        double m = 0;
        for (int k = 0; k < 16; k++) {
            chan_t *c = &V.ch[k];
            if (c->sample < 0) continue;
            const wave_t *w = &V.samples[c->sample];
            if ((int)c->pos >= w->len) {
                if (c->loop && w->len) c->pos = 0;
                else {
                    c->sample = -1;
                    continue;
                }
            }
            m += 0.5 * w->data[(int)c->pos];
            c->pos += 1.0;
        }
        if (SOUND == AU_HEADON || GAME == G_INVINCOHEADON2) m += headon_sample();
        if (SOUND == AU_FROGS || SOUND == AU_BORDERLINE) m += netlist_sample();
#if CARNIVAL
        m += 0.25 * (double)ay8910_render(&V.psg) / 16384.0;
#endif
        V.hp_out = 0.998 * (V.hp_out + m - V.hp_in);   // the output's coupling capacitor
        V.hp_in = m;
        const long s = lround(V.hp_out * 32767.0);
        if (V.naudio < 2048) V.audio[V.naudio++] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}

#if CARNIVAL
// the music board: an i8035 and an AY-3-8912 on its ports
static uint8_t music_in(void *u, uint16_t port)
{
    (void)u;
    return port == MCS48_T1 ? (uint8_t)((~V.port2 >> 3) & 1) : 0xff;
}

static void music_out(void *u, uint16_t port, uint8_t d)
{
    (void)u;
    if (port == MCS48_P1) V.music_data = d;
    else if (port == MCS48_P2) V.music_bus = (d >> 6) & 3;
    else return;
    if (V.music_bus & 1) {
        if (V.music_bus & 2) ay8910_address_w(&V.psg, V.music_data);
        else ay8910_data_w(&V.psg, V.music_data);
    }
}
#endif

// ---------------------------------------------------------------------------
// Video: a line of characters, 1bpp, the colour PROM picking the colours

static void render_line(int y)
{
    for (int col = 0; col < 32; col++) {
        const uint8_t code = V.videoram[((y >> 3) << 5) | col], bits = V.charram[(code << 3) | (y & 7)];
        uint16_t fore = 0xffff, back = 0;
        if (V.has_prom) {
            const uint8_t pr = V.prom[((code >> 5) | (V.palette_bank << 3)) & 0x1f];
            back = kPens565[(pr >> 1) & 7];
            fore = kPens565[(pr >> 5) & 7];
        }
        for (int b = 0; b < 8; b++) {
            const uint16_t c = ((bits << b) & 0x80) ? fore : back;
            const int x = col * 8 + b;
#if ROTATED
            V.fb[(NW - 1 - x) * W + y] = c;
#else
            V.fb[y * W + x] = c;
#endif
        }
    }
}

bool arc_init(void)
{
    if (!(V_ = (struct state *)calloc(1, sizeof(struct state))) || !(V.fb = (uint16_t *)calloc(W * H, 2))) return false;
    if (!ARC_LOAD(kCpu, V.rom, sizeof V.rom)) return false;
#if HAS_PROM
    V.has_prom = arc_load(kProm, (int)(sizeof kProm / sizeof kProm[0]), V.prom, sizeof V.prom);
    if (!V.has_prom) memset(V.prom, 0, sizeof V.prom);
#endif
#if CARNIVAL
    ARC_LOAD(kMusic, V.music_rom, 0x400);   // (without it no music, as dsp-cpp)
    mcs48_init(&V.music, MCS48_I8035, V.music_rom);
    V.music.port_in = music_in;
    V.music.port_out = music_out;
    ay8910_init(&V.psg, 3579545 / 3, RATE);
    ay8910_set_amplitude(&V.psg, 1.0f);
#endif
    load_samples();
    V.noise_lfsr = 0x1ffff;             // (not on a reset, as dsp-cpp)
    V.cpu.read = rd;
    V.cpu.write = wr;
    V.cpu.in = port_rd;
    V.cpu.out = port_wr;
    zd_init(&V.cpu);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&V.cpu);
    memset(V.videoram, 0, sizeof V.videoram);
    memset(V.ram, 0, sizeof V.ram);
    memset(V.safari_ram, 0, sizeof V.safari_ram);
    memset(V.charram, 0, sizeof V.charram);
    V.coin_status = 0;
    V.coin_clear_at = -1;
    V.palette_bank = GAME == G_HEADON2 ? 3 : 0;
    V.scanline = 0;
    V.hblank = false;
    V.cycles = 0;
    V.samurai_prot = V.tranqgun_prot = 0;
    V.nsub_count = 0;
    V.nsub_next = 0;
    V.port1 = 0;
    V.port2 = GAME == G_NSUB ? 0xff : 0;
    for (int i = 0; i < 16; i++) V.ch[i] = (chan_t){-1, 0, false};
    V.audio_frac = V.hp_in = V.hp_out = 0;
    memset(&V.ho, 0, sizeof V.ho);
    V.ho.player = V.ho.computer = (car_t){12.0, 0.0, 4.0, true, 0, 0, 0};
    V.ho.lfsr = 1;
    for (int i = 0; i < 8; i++) V.net[i] = (netvoice_t){-1, 0, false};
    V.game_select = false;
#if CARNIVAL
    mcs48_reset(&V.music);
    V.music.reset_line = true;          // held until port 2's d4 goes high
    ay8910_reset(&V.psg);
    V.music_data = V.music_bus = 0;
    V.music_acc = 0;
#endif
    for (int i = 0; i < W * H; i++) V.fb[i] = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const double samples_per_line = (double)RATE / (FPS * VTOTAL);
#if CARNIVAL
    const double music_per_line = (double)(3579545 / 15) / (FPS * VTOTAL);   // machine cycles of 15 clocks
#endif
    V.naudio = 0;
    for (V.scanline = 0; V.scanline < VTOTAL; V.scanline++) {
        V.hblank = false;
        zd_run(&V.cpu, VISIBLE_CYCLES);
        V.hblank = true;
        zd_run(&V.cpu, CYCLES_PER_LINE - VISIBLE_CYCLES);
        V.cycles += CYCLES_PER_LINE;
        if (V.scanline < NH) render_line(V.scanline);
        if (V.coin_clear_at >= 0 && V.cycles >= V.coin_clear_at) {
            V.coin_status = 0;
            V.coin_clear_at = -1;
        }
        if (GAME == G_NSUB && V.cycles >= V.nsub_next) {   // the play counter's 555: a coin pulse every 150 ms
            V.nsub_next = V.cycles + (int64_t)CPU_CLOCK * 150 / 1000;
            if (V.nsub_count > 0) {
                V.nsub_count--;
                coin_in();
            }
        }
#if CARNIVAL
        V.music_acc += music_per_line;
        const int mn = (int)V.music_acc;
        V.music_acc -= mn;
        if (mn > 0) mcs48_run(&V.music, mn);
#endif
        V.audio_frac += samples_per_line;
        const int n = (int)V.audio_frac;
        V.audio_frac -= n;
        if (n > 0) generate_audio(n);
    }
    if (audio) *audio = V.audio;
    return V.naudio;
}

const uint16_t *arc_frame(void) { return V.fb; }

void arc_input(const arc_in *in)
{
    V.host = *in;
    const bool coin = in->coin1 || in->coin2;
    if (coin && !V.prev_coin) {
        if (GAME == G_NSUB) V.nsub_count++;
        else coin_in();
    }
    V.prev_coin = coin;
    const bool select = (in->p[0] & (GAME == G_INVINCODEEPSCAN ? AP_B3 : AP_B2)) != 0;   // two-game boards: the game select
    if (select && !V.prev_select) V.game_select = !V.game_select;
    V.prev_select = select;
}

void arc_dip(int bank, uint8_t v) { (void)bank, (void)v; }
