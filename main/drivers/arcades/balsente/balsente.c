// balsente.c - the Bally/Sente SAC-1 games for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/balsente (after MAME's
// midway/balsente.cpp). A 6809 at 1.25 MHz over a 256x240 4bpp bitmap with
// 40 16-pixel-wide sprites and a banked program ROM, the sound board's Z80
// with an 8253, the soft 6850 UARTs between both and six CEM3394 voices
// (sound/cem3394d). The analog controls (trackballs, steering) are centred,
// as dsp-cpp leaves them. ARC_VARIANT: the 25 games, in dsp-cpp's order.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/cem3394d.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#if ARC_VARIANT == 0
static const arc_rom kMain[] = { {"sdiagef.bin", 0x2000, 0x1e000, 0x2a39fc53} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"sdiaggr0.bin", 0x2000, 0x0000, 0x5e0ff62a} };
#define SPRITES_END 0x2000
#elif ARC_VARIANT == 1
static const arc_rom kMain[] = { {"chicken_shift_ab_0_11-23-84.u9a", 0x2000, 0x0000, 0xd2069e75}, {"chicken_shift_ab_1_11-23-84.u8a", 0x2000, 0x2000, 0x198f25a8}, {"chicken_shift_ab_2_11-23-84.u7a", 0x2000, 0x4000, 0x2e2b2b82}, {"chicken_shift_ab_3_11-23-84.u6a", 0x2000, 0x6000, 0xb97fc520}, {"chicken_shift_ab_4_11-23-84.u5a", 0x2000, 0x8000, 0xb4f0d673}, {"chicken_shift_ab_5_11-23-84.u4a", 0x2000, 0xa000, 0xb1f8e589}, {"chicken_shift_cd_11-23-84.u3a", 0x2000, 0x1c000, 0xf555a0b2}, {"chicken_shift_ef_11-23-84.u2a", 0x2000, 0x1e000, 0x368b1ce3} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"chicken_shift_gr-0_11-23-84.u9b", 0x2000, 0x0000, 0x67f9d3b3}, {"chicken_shift_gr-1_11-23-84.u8b", 0x2000, 0x2000, 0x78973d50}, {"chicken_shift_gr-2_11-23-84.u7b", 0x2000, 0x4000, 0x1784f939}, {"chicken_shift_gr-3_11-23-84.u6b", 0x2000, 0x6000, 0xb43916a2}, {"chicken_shift_gr-4_11-23-84.u5b", 0x2000, 0x8000, 0xa94cd35b} };
#define SPRITES_END 0xa000
#elif ARC_VARIANT == 2
static const arc_rom kMain[] = { {"hat_trk_ab0_11-12-84.u9a", 0x2000, 0x0000, 0xf25c1b99}, {"hat_trk_ab1_11-12-84.u8a", 0x2000, 0x2000, 0xc1df3d1f}, {"hat_trk_ab2_11-12-84.u7a", 0x2000, 0x4000, 0xf6c41257}, {"hat_trk_cd_11-12-84.u3a", 0x2000, 0x1c000, 0xfc44f36c}, {"hat_trk_ef_11-12-84.u2a", 0x2000, 0x1e000, 0xd8f910fb} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"hat_trk_gr0_11-12-84.u9b", 0x2000, 0x0000, 0x9f41baba}, {"hat_trk_gr1_11-12-84.u8b", 0x2000, 0x2000, 0x951f08c9} };
#define SPRITES_END 0x4000
#elif ARC_VARIANT == 3
static const arc_rom kMain[] = { {"ggh-ab0.u9a", 0x2000, 0x0000, 0xed0fdeac}, {"ggh-ab1.u8a", 0x2000, 0x2000, 0x5bfbae58}, {"ggh-ab2.u7a", 0x2000, 0x4000, 0xf0baf921}, {"ggh-ab3.u6a", 0x2000, 0x6000, 0xed0fdeac}, {"ggh-ab4.u5a", 0x2000, 0x8000, 0x5bfbae58}, {"ggh-ab5.u4a", 0x2000, 0xa000, 0xf0baf921}, {"ggh-cd.u3a", 0x2000, 0x1c000, 0xd3d75f84}, {"ggh-ef.u2a", 0x2000, 0x1e000, 0xa02b4243} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"ggh-gr0.u8b", 0x2000, 0x0000, 0x03515526}, {"ggh-gr1.u8b", 0x2000, 0x2000, 0xb4293435}, {"ggh-gr2.u7b", 0x2000, 0x4000, 0xece0cb97}, {"ggh-gr3.u6b", 0x2000, 0x6000, 0xdd7e25d0}, {"ggh-gr4.u5b", 0x2000, 0x8000, 0xb4293435}, {"ggh-gr5.u4b", 0x2000, 0xa000, 0xd3da0093} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 4
static const arc_rom kMain[] = { {"off_the_wall_ab0_10-16-84.u9a", 0x2000, 0x0000, 0x474441c7}, {"off_the_wall_ab1_10-16-84.u8a", 0x2000, 0x2000, 0x2e9e9411}, {"off_the_wall_ab2_10-16-84.u7a", 0x2000, 0x4000, 0xba092128}, {"off_the_wall_ab3_10-16-84.u6a", 0x2000, 0x6000, 0x74bc479d}, {"off_the_wall_ab4_10-16-84.u5a", 0x2000, 0x8000, 0xf5f67619}, {"off_the_wall_ab5_10-16-84.u4a", 0x2000, 0xa000, 0xf5f67619}, {"off_the_wall_cd_10-16-84.u3a", 0x2000, 0x1c000, 0x8e2d15ab}, {"off_the_wall_ef_10-16-84.u2a", 0x2000, 0x1e000, 0x57eab299} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"off_the_wall_gr0_10-16-84.u9b", 0x2000, 0x0000, 0x210bad3c}, {"off_the_wall_gr1_10-16-84.u8b", 0x2000, 0x2000, 0x13e6aaa5}, {"off_the_wall_gr2_10-16-84.u7b", 0x2000, 0x4000, 0x5cfefee5}, {"off_the_wall_gr3_10-16-84.u6b", 0x2000, 0x6000, 0x6b17e4a9}, {"off_the_wall_gr4_10-16-84.u5b", 0x2000, 0x8000, 0x15985c8c}, {"off_the_wall_gr5_10-16-84.u4b", 0x2000, 0xa000, 0x448f7e3c} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 5
static const arc_rom kMain[] = { {"spit-ab0.u9a", 0x2000, 0x0000, 0x5aa86081}, {"spit-ab1.u8a", 0x2000, 0x2000, 0x588228b8}, {"spit-ab2.u7a", 0x2000, 0x4000, 0x60173ab6}, {"spit-ab3.u6a", 0x2000, 0x6000, 0x56cb51a8}, {"spit-ab4.u5a", 0x2000, 0x8000, 0x40ba61e0}, {"spit-ab5.u4a", 0x2000, 0xa000, 0x2a1d9d8f}, {"spit-cd.u3a", 0x2000, 0x1c000, 0x54095cbb}, {"spit-ef.u2a", 0x2000, 0x1e000, 0x5f836a66} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"spit-gr0.u9b", 0x2000, 0x0000, 0xf77fd85d}, {"spit-gr1.u8b", 0x2000, 0x2000, 0x3ad10334}, {"spit-gr2.u7b", 0x2000, 0x4000, 0x24887703}, {"spit-gr3.u6b", 0x2000, 0x6000, 0xc6703ec2}, {"spit-gr4.u5b", 0x2000, 0x8000, 0xb4293435}, {"spit-gr5.u4b", 0x2000, 0xa000, 0xdc27c970} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 6
static const arc_rom kMain[] = { {"t.prst_u9a_2-12-85.u9a", 0x2000, 0x0000, 0x79fd3ac3}, {"t.prst_u8a_2-12-85.u8a", 0x2000, 0x2000, 0x0ff677e9}, {"t.prst_u7a_2-12-85.u7a", 0x2000, 0x4000, 0x3b4d03e7}, {"t.prst_u6a_2-12-85.u6a", 0x2000, 0x6000, 0x2c6c0651}, {"t.prst_u5a_2-12-85.u5a", 0x2000, 0x8000, 0x397529e7}, {"t.prst_u4a_2-12-85.u4a", 0x2000, 0xa000, 0x499773a4}, {"t.prst_u3a_2-12-85.u3a", 0x2000, 0x1c000, 0x35c9b9c2}, {"t.prst_u2a_2-12-85.u2a", 0x2000, 0x1e000, 0x64878342} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"t.prst_u9b_2-12-85.u9b", 0x2000, 0x0000, 0x20c9217a}, {"t.prst_u8b_2-12-85.u8b", 0x2000, 0x2000, 0xd7f44504}, {"t.prst_u7b_2-12-85.u7b", 0x2000, 0x4000, 0x4e59a15d}, {"t.prst_u6b_2-12-85.u6b", 0x2000, 0x6000, 0x323a8640}, {"t.prst_u5b_2-12-85.u5b", 0x2000, 0x8000, 0x673acf42}, {"t.prst_u4b_2-12-85.u4b", 0x2000, 0xa000, 0x067bfd66} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 7
static const arc_rom kMain[] = { {"rom-ab0.u9a", 0x2000, 0x0000, 0xda2dd119}, {"rom-ab1.u8a", 0x2000, 0x2000, 0x657ddf26}, {"rom-ab2.u7a", 0x2000, 0x4000, 0x15333dcf}, {"rom-ab3.u6a", 0x2000, 0x6000, 0x57671f6f}, {"rom-ab4.u5a", 0x2000, 0x8000, 0xc16c5dc0}, {"rom-ab5.u4a", 0x2000, 0xa000, 0xd7019747}, {"rom-cd.u3a", 0x2000, 0x1c000, 0x7b44ca4c}, {"rom-ef.u1a", 0x2000, 0x1e000, 0xf5309b38} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"rom-gr0.u9b", 0x2000, 0x0000, 0x3e64b5d5}, {"rom-gr1.u8b", 0x2000, 0x2000, 0xb3b8baee}, {"rom-gr2.u7b", 0x2000, 0x4000, 0xe9d89dac}, {"rom-gr3.u6b", 0x2000, 0x6000, 0xb6602be8}, {"rom-gr4.u5b", 0x2000, 0x8000, 0x3fbfa686}, {"rom-gr5.u4b", 0x2000, 0xa000, 0x345f94fb} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 8
static const arc_rom kMain[] = { {"stocker_ab_01_3-19-85.u8a", 0x4000, 0x0000, 0x6a914d99}, {"stocker_ab_23_3-19-85.u7a", 0x4000, 0x4000, 0x48e432c2}, {"stocker_ef_3-19-85.u1a", 0x4000, 0x1c000, 0x83e6e5c9} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"stocker_gr_01_3-19-85.u6b", 0x4000, 0x0000, 0x2e66ac35}, {"stocker_gr_23_3-19-85.u5b", 0x4000, 0x4000, 0x6fa43631} };
#define SPRITES_END 0x8000
#elif ARC_VARIANT == 9
static const arc_rom kMain[] = { {"b.boomer_rom_ab01r_3-20-85.u8a", 0x4000, 0x0000, 0x1b7c439d}, {"b.boomer_rom_ab23r_3-20-85.u7a", 0x4000, 0x4000, 0xe4f1e704}, {"b.boomer_rom_ab45r_3-20-85.u6a", 0x4000, 0x8000, 0xdaa2d8bc}, {"b.boomer_rom_ab67r_3-20-85.u5a", 0x4000, 0xc000, 0x3622c4f1}, {"b.boomer_rom_cd45r_3-20-85.u2a", 0x4000, 0x18000, 0x07fd88ff}, {"b.boomer_rom_cd6efr_3-20-85.u1a", 0x4000, 0x1c000, 0x2d03f241} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"b.boomer_gr01r_3-20-85.u6b", 0x4000, 0x0000, 0x6829de8e}, {"b.boomer_gr23r_3-20-85.u5b", 0x4000, 0x4000, 0x89398700}, {"b.boomer_gr45r_3-20-85.u4b", 0x4000, 0x8000, 0x92fb6fb1} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 10
static const arc_rom kMain[] = { {"genus_ii_ab01_r_3-22-85.u8a", 0x4000, 0x0000, 0x4fca20c5}, {"genus_ii_ab23_r_3-22-85.u7a", 0x4000, 0x4000, 0x6cf2ddeb}, {"genus_ii_ab45_r_3-22-85.u6a", 0x4000, 0x8000, 0xa7ff789c}, {"genus_ii_ab67_r_3-22-85.u5a", 0x4000, 0xc000, 0xcc5c68ef}, {"genus_ii_rom_cd45r_3-22-85.u2a", 0x4000, 0x18000, 0xfc9c752a}, {"genus_ii_rom_d6efr_3-22-85.u1a", 0x4000, 0x1c000, 0x23b56fb8} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"genus_ii_gr0_r_3-22-85.u6b", 0x4000, 0x0000, 0x6829de8e}, {"genus_ii_gr1_r_3-22-85.u5b", 0x4000, 0x4000, 0x89398700}, {"genus_ii_grz_r_3-22-85.u4b", 0x4000, 0x8000, 0x1e870293} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 11
static const arc_rom kMain[] = { {"young_rom_ab01_r_3-29-85.u8a", 0x4000, 0x0000, 0x97d35a85}, {"young_rom_ab23_r_3-29-85.u7a", 0x4000, 0x4000, 0x2ff67c70}, {"young_rom_ab45_r_3-29-85.u6a", 0x4000, 0x8000, 0x511a0fab}, {"young_rom_ab67_r_3-29-85.u5a", 0x4000, 0xc000, 0xdf99d00c}, {"young_rom_cd45_r_3-29-85.u2a", 0x4000, 0x18000, 0xac45809e}, {"young_rom_cd6ef_r_3-29-85.u1a", 0x4000, 0x1c000, 0xa008059f} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"young_gr01_r_3-29-85.u6b", 0x4000, 0x0000, 0x6829de8e}, {"young_gr23_r_3-29-85.u5b", 0x4000, 0x4000, 0x89398700}, {"young_gr45_r_3-29-85.u4b", 0x4000, 0x8000, 0x1242033e} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 12
static const arc_rom kMain[] = { {"allsport.u8a", 0x4000, 0x0000, 0x54b7ff31}, {"allsport.u7a", 0x4000, 0x4000, 0x59fae9d2}, {"allsport.u6a", 0x4000, 0x8000, 0x237b6b95}, {"allsport.u5a", 0x4000, 0xc000, 0xb64d7f61}, {"allsport.u2a", 0x4000, 0x18000, 0xe45d09d6}, {"allsport.u1a", 0x4000, 0x1c000, 0x8bb3e831} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"allsport.u6b", 0x4000, 0x0000, 0x6829de8e}, {"allsport.u5b", 0x4000, 0x4000, 0x89398700}, {"allsport.u4b", 0x4000, 0x8000, 0x7415a7fc} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 13
static const arc_rom kMain[] = { {"gimmeabreak_ab01_7-7-85.u8a", 0x4000, 0x0000, 0x18cc53db}, {"gimmeabreak_ab23_7-7-85.u7a", 0x4000, 0x4000, 0x6bd4190a}, {"gimmeabreak_ab45_7-7-85.u6a", 0x4000, 0x8000, 0x5dca4f33}, {"gimmeabreak_cd_6_ef_7-7-85.u1a", 0x4000, 0x1c000, 0x5e2b3510} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"gimmeabreak_gr01_7-7-85.u6b", 0x4000, 0x0000, 0xe3cdc476}, {"gimmeabreak_gr23_7-7-85.u5b", 0x4000, 0x4000, 0x0555d9c0} };
#define SPRITES_END 0x8000
#elif ARC_VARIANT == 14
static const arc_rom kMain[] = { {"ab01.u8a", 0x4000, 0x0000, 0x348f827f}, {"ab23.u7a", 0x4000, 0x4000, 0x19a6ff47}, {"ab45.u6a", 0x4000, 0x8000, 0x925d76eb}, {"ab67.u5a", 0x4000, 0xc000, 0x6a311c9a}, {"1a-ver2", 0x10000, 0x10000, 0x60b6cd58} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"gr01.u6b", 0x4000, 0x0000, 0x8e24d594}, {"gr23.u5b", 0x4000, 0x4000, 0x3bf355ef}, {"gr45.u4b", 0x4000, 0x8000, 0x8eb14921} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 15
static const arc_rom kMain[] = { {"hat_trk_ab-0_11-12-84.u8a", 0x4000, 0x0000, 0xcb746de8}, {"hat_trk_ab-1_11-16-84.u7a", 0x4000, 0x4000, 0x5f2a0b24}, {"hat_trk_cd_11-16-84.u1a", 0x4000, 0x1c000, 0x6c6cf2be} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"hat_trk_gr-0_11-16-84.u6b", 0x4000, 0x0000, 0x6e299728} };
#define SPRITES_END 0x4000
#elif ARC_VARIANT == 16
static const arc_rom kMain[] = { {"ab0.8a.romab0", 0x8000, 0x0000, 0xeabeec2b}, {"ab4.9a.romab4", 0x8000, 0x8000, 0x2dddb371}, {"g.m._cd-0_9-21-87.13a.romcd0", 0x8000, 0x10000, 0xad168726}, {"cd4.15a.romcd4", 0x8000, 0x18000, 0x1de8dd2e}, {"cd12.18a.romcd12", 0x8000, 0x18000, 0x1de8dd2e} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"g.m._gr0_9-21-87.8a.gr0", 0x8000, 0x0000, 0xb9681f53} };
#define SPRITES_END 0x8000
#elif ARC_VARIANT == 17
static const arc_rom kMain[] = { {"tp_a2.bin", 0x4000, 0x0000, 0xb4d69463}, {"tp_a7.bin", 0x4000, 0x4000, 0xd78bd4b6}, {"tp_a4.bin", 0x4000, 0x8000, 0x0de9e14d}, {"tp_a5.bin", 0x4000, 0xc000, 0xe749adac}, {"tp_a8.bin", 0x4000, 0x10000, 0x168ef5ed}, {"tp_a1.bin", 0x4000, 0x14000, 0x1f6ef37f}, {"tp_a6.bin", 0x4000, 0x18000, 0x421c1a29}, {"tp_a3.bin", 0x4000, 0x1c000, 0xc6254f46} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"tp_gr3.bin", 0x4000, 0x0000, 0x6829de8e}, {"tp_gr2.bin", 0x4000, 0x4000, 0x89398700}, {"tp_gr1.bin", 0x4000, 0x8000, 0x1242033e} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 18
static const arc_rom kMain[] = { {"tgle-ab0.bin", 0x2000, 0x0000, 0x8c7b7fad}, {"tgle-ab1.bin", 0x2000, 0x2000, 0x771e5434}, {"tgle-ab2.bin", 0x2000, 0x4000, 0x9b4baa3f}, {"tgle-ab3.bin", 0x2000, 0x6000, 0x35308a41}, {"tgle-ab4.bin", 0x2000, 0x8000, 0xbaf5617b}, {"tgle-ab5.bin", 0x2000, 0xa000, 0x88077dad}, {"tgle-cd.bin", 0x2000, 0x1c000, 0x0a2bb949}, {"tgle-ef.bin", 0x2000, 0x1e000, 0x3ec10804} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"tgle-gr0.bin", 0x2000, 0x0000, 0x0e0e5d0e}, {"tgle-gr1.bin", 0x2000, 0x2000, 0x3b141ad2} };
#define SPRITES_END 0x4000
#elif ARC_VARIANT == 19
static const arc_rom kMain[] = { {"night_stocker_ab_01_10-06-86.u8a", 0x4000, 0x0000, 0xa635f973}, {"night_stocker_ab_23_10-06-86.u7a", 0x4000, 0x4000, 0x223acbb2}, {"night_stocker_ab_45_10-06-86.u6a", 0x4000, 0x8000, 0x27a728b5}, {"night_stocker_ab_67_10-06-86.u5a", 0x4000, 0xc000, 0x2999cdf2}, {"night_stocker_cd_01_10-06-86.u4a", 0x4000, 0x10000, 0x75e9b51a}, {"night_stocker_cd_23_10-06-86.u3a", 0x4000, 0x14000, 0x0a32e0a5}, {"night_stocker_cd_45_10-06-86.u2a", 0x4000, 0x18000, 0x9bb292fe}, {"night_stocker_cd_6_ef_10-06-86.u1a", 0x4000, 0x1c000, 0xe77c1aea} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"night_stocker_gr_01_10-06-86.u4c", 0x4000, 0x0000, 0xfd0c38be}, {"night_stocker_gr_23_10-06-86.u3c", 0x4000, 0x4000, 0x35d4433e}, {"night_stocker_gr_45_10-06-86.u2c", 0x4000, 0x8000, 0x734b858a}, {"night_stocker_gr_67_10-06-86.u1c", 0x4000, 0xc000, 0x3311f9c0} };
#define SPRITES_END 0x10000
#elif ARC_VARIANT == 20
static const arc_rom kMain[] = { {"street_football_ab_01_11-12-86.u8a", 0x4000, 0x0000, 0x2a69803f}, {"street_football_ab_23_11-12-86.u7a", 0x4000, 0x4000, 0x89f157c2}, {"street_football_ab_45_11-12-86.u6a", 0x4000, 0x8000, 0x91ad42c5}, {"street_football_cd_6_ef_11-12-86.u1a", 0x4000, 0x1c000, 0xbf80bb1a} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"street_football_gr_01_11-12-86.u4c", 0x4000, 0x0000, 0xe3108d35}, {"street_football_gr_23_11-12-86.u3c", 0x4000, 0x4000, 0x5c5af726}, {"street_football_gr_45_11-12-86.u2c", 0x4000, 0x8000, 0xe767251e}, {"street_football_gr_67_11-12-86.u1c", 0x4000, 0xc000, 0x42452a7a} };
#define SPRITES_END 0x10000
#elif ARC_VARIANT == 21
static const arc_rom kMain[] = { {"spiker_u_r_ab01_5-05-86.u8a", 0x4000, 0x0000, 0x2d53d023}, {"spiker_u_r_ab23_5-05-86.u7a", 0x4000, 0x4000, 0x3be87edf}, {"spiker_u_r_cd_6_ef_6-09-86.u1a", 0x4000, 0x1c000, 0x5b5a6d86} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"spiker_u_r_gr01_5-05-86.u4c", 0x4000, 0x0000, 0x0caa6e3e}, {"spiker_u_r_gr23_5-05-86.u3c", 0x4000, 0x4000, 0x970c81f6}, {"spiker_u_r_gr45_5-05-86.u2c", 0x4000, 0x8000, 0x90ddd737} };
#define SPRITES_END 0xc000
#elif ARC_VARIANT == 22
static const arc_rom kMain[] = { {"ab 01.u8a", 0x4000, 0x0000, 0x46f428c6}, {"ab 23.u7a", 0x4000, 0x4000, 0x0e13132f}, {"ab 45.u6a", 0x4000, 0x8000, 0x6ed26069}, {"ab 67.u5a", 0x4000, 0xc000, 0x7f63b516}, {"cd 23.u3a", 0x4000, 0x14000, 0x52b29048}, {"cd 6 ef.u1a", 0x4000, 0x1c000, 0xb880961a} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"gr 01.u4c", 0x4000, 0x0000, 0x14ffdd1e}, {"gr 23.u3c", 0x4000, 0x4000, 0x761abb80}, {"gr 45.u2c", 0x4000, 0x8000, 0x0d2cf2e6}, {"gr 67.u2c", 0x4000, 0xc000, 0x2bab2784} };
#define SPRITES_END 0x10000
#elif ARC_VARIANT == 23
static const arc_rom kMain[] = { {"namethattune_ur_ab_01_3-31-86.u8a", 0x4000, 0x0000, 0xf99054f1}, {"namethattune_ur_ab_01_3-31-86.u8a", 0x4000, 0x20000, 0xf99054f1}, {"namethattune_ur_ab_23_3-31-86.u7a", 0x4000, 0x4000, 0xf2b8f7fa}, {"namethattune_ur_ab_23_3-31-86.u7a", 0x4000, 0x24000, 0xf2b8f7fa}, {"namethattune_ur_ab_45_3-31-86.u6a", 0x4000, 0x8000, 0x89e1c769}, {"namethattune_ur_ab_45_3-31-86.u6a", 0x4000, 0x28000, 0x89e1c769}, {"namethattune_ur_ab_67_3-31-86.u5a", 0x4000, 0xc000, 0x7e5572a1}, {"namethattune_ur_ab_67_3-31-86.u5a", 0x4000, 0x2c000, 0x7e5572a1}, {"namethattune_ur_cd_01_3-31-86.u4a", 0x4000, 0x10000, 0xdb9d6154}, {"namethattune_ur_cd_01_3-31-86.u4a", 0x4000, 0x30000, 0xdb9d6154}, {"namethattune_ur_cd_23_3-31-86.u3a", 0x4000, 0x14000, 0x9d2e458f}, {"namethattune_ur_cd_23_3-31-86.u3a", 0x4000, 0x34000, 0x9d2e458f}, {"namethattune_ur_cd_45_3-31-86.u2a", 0x4000, 0x18000, 0x9a4b87aa}, {"namethattune_ur_cd_45_3-31-86.u2a", 0x4000, 0x38000, 0x9a4b87aa}, {"namethattune_ur_cd_6_ef_3-31-86.u1a", 0x4000, 0x1c000, 0x0459e6f8}, {"namethattune_ur_cd_6_ef_3-31-86.u1a", 0x4000, 0x3c000, 0x0459e6f8} };
#define MAIN_END 0x40000
static const arc_rom kSprites[] = { {"namethattune_ur_gr_0_3-31-86.u3c", 0x8000, 0x0000, 0x6b75bb4b} };
#define SPRITES_END 0x8000
#elif ARC_VARIANT == 24
static const arc_rom kMain[] = { {"ab 1.a10", 0x8000, 0x0000, 0x33a76b47}, {"ab 12.a12", 0x8000, 0x8000, 0x7c7a9f12}, {"cd 8.a16", 0x8000, 0x10000, 0x90917a43}, {"cd 12.a18", 0x8000, 0x18000, 0x0450e9d7} };
#define MAIN_END 0x20000
static const arc_rom kSprites[] = { {"gr 0.a5", 0x8000, 0x0000, 0xe0dfc133}, {"gr 4.a7", 0x8000, 0x8000, 0x952ade30} };
#define SPRITES_END 0x10000
#endif

#if ARC_VARIANT == 0
#define ID "sentetst"
#define TITLE "Sente Diagnostic Cartridge"
#define YEAR 1984
#elif ARC_VARIANT == 1
#define ID "cshift"
#define TITLE "Chicken Shift"
#define YEAR 1984
#elif ARC_VARIANT == 2
#define ID "hattrick"
#define TITLE "Hat Trick"
#define YEAR 1984
#elif ARC_VARIANT == 3
#define ID "gghost"
#define TITLE "Goalie Ghost"
#define YEAR 1984
#elif ARC_VARIANT == 4
#define ID "otwalls"
#define TITLE "Off the Wall"
#define YEAR 1984
#elif ARC_VARIANT == 5
#define ID "snakepit"
#define TITLE "Snake Pit"
#define YEAR 1984
#elif ARC_VARIANT == 6
#define ID "triviag1"
#define TITLE "Trivial Pursuit (Genus I)"
#define YEAR 1984
#elif ARC_VARIANT == 7
#define ID "snakjack"
#define TITLE "Snacks'n Jaxson"
#define YEAR 1984
#elif ARC_VARIANT == 8
#define ID "stocker"
#define TITLE "Stocker"
#define YEAR 1984
#elif ARC_VARIANT == 9
#define ID "triviabb"
#define TITLE "Trivial Pursuit (Baby Boomer Edition)"
#define YEAR 1984
#elif ARC_VARIANT == 10
#define ID "triviag2"
#define TITLE "Trivial Pursuit (Genus II Edition)"
#define YEAR 1984
#elif ARC_VARIANT == 11
#define ID "triviayp"
#define TITLE "Trivial Pursuit (Young Players Edition)"
#define YEAR 1984
#elif ARC_VARIANT == 12
#define ID "triviasp"
#define TITLE "Trivial Pursuit (All Star Sports Edition)"
#define YEAR 1984
#elif ARC_VARIANT == 13
#define ID "gimeabrk"
#define TITLE "Gimme A Break"
#define YEAR 1985
#elif ARC_VARIANT == 14
#define ID "minigolf"
#define TITLE "Mini Golf"
#define YEAR 1985
#elif ARC_VARIANT == 15
#define ID "teamht"
#define TITLE "Team Hat Trick"
#define YEAR 1985
#elif ARC_VARIANT == 16
#define ID "grudge"
#define TITLE "Grudge Match"
#define YEAR 1985
#elif ARC_VARIANT == 17
#define ID "triviaes"
#define TITLE "Trivial Pursuit (Spanish Edition)"
#define YEAR 1987
#elif ARC_VARIANT == 18
#define ID "toggle"
#define TITLE "Toggle"
#define YEAR 1985
#elif ARC_VARIANT == 19
#define ID "nstocker"
#define TITLE "Night Stocker"
#define YEAR 1986
#elif ARC_VARIANT == 20
#define ID "sfootbal"
#define TITLE "Street Football"
#define YEAR 1986
#elif ARC_VARIANT == 21
#define ID "spiker"
#define TITLE "Spiker"
#define YEAR 1986
#elif ARC_VARIANT == 22
#define ID "stompin"
#define TITLE "Stompin'"
#define YEAR 1986
#elif ARC_VARIANT == 23
#define ID "nametune"
#define TITLE "Name That Tune"
#define YEAR 1986
#elif ARC_VARIANT == 24
#define ID "rescraid"
#define TITLE "Rescue Raider"
#define YEAR 1987
#endif

#define SENTETST 0
#define CSHIFT 1
#define HATTRICK 2
#define GGHOST 3
#define OTWALLS 4
#define SNAKEPIT 5
#define TRIVIAG1 6
#define SNAKJACK 7
#define STOCKER 8
#define TRIVIABB 9
#define TRIVIAG2 10
#define TRIVIAYP 11
#define TRIVIASP 12
#define GIMEABRK 13
#define MINIGOLF 14
#define TEAMHT 15
#define GRUDGE 16
#define TRIVIAES 17
#define TOGGLE 18
#define NSTOCKER 19
#define SFOOTBAL 20
#define SPIKER 21
#define STOMPIN 22
#define NAMETUNE 23
#define RESCRAID 24
#define GAME ARC_VARIANT
#define ROMBANK2 (GAME == NSTOCKER || GAME == SFOOTBAL || GAME == SPIKER || GAME == STOMPIN || GAME == NAMETUNE)

#define W 256
#define H 240
#define FPS 60.0
#define SCANLINES 264
#define MAIN_CLOCK (20000000 / 16)
#define SOUND_CLOCK 4000000
#define HTOTAL 0x140
#define HBSTART 0x100
#define VBEND 0x10
#define VBSTART 0x100
#define RATE 44100
#define POLY17 ((1u << 17) - 1)
#define UART_DELAY 625
#define EXPAND_ALL 0x00
#define EXPAND_NONE 0x3f
#define SWAP_HALVES 0x80
#define ROM_SIZE (GAME == NAMETUNE ? 0x40000u : 0x20000u)
#define ROM_LEN (MAIN_END > ROM_SIZE && GAME != NAMETUNE ? MAIN_END : ROM_SIZE)

static const char *const kSets[] = { ID, NULL };
const arc_info arc_game = { ID, TITLE, "Bally/Sente", YEAR, W, H, FPS, RATE, kSets, { "B1", "B2", NULL }, ARC_JOY_8WAY };
static const arc_rom kSound[] = {{"8002-10 9-25-84.5", 0x2000, 0x0000, 0x4dd0a525}};

typedef struct {
    bool timer_active;
    int32_t initial, count;
    uint8_t gate, out, mode, readbyte, writebyte;
    int64_t remaining;                          // 2 MHz clocks until OUT fires (counters 1 / 2)
} counter_t;

static struct state {
    m6809d main;
    zd_z80 snd;
    cem3394d cem[6];
    uint8_t *rom, *sprite_rom;
    uint32_t sprite_mask;
    uint8_t sound_rom[0x2000], sound_ram[0x10000];
    uint8_t spriteram[0x800], videoram[0x7800], expanded[256 * 240], pal_ram[0x1000], novram[0x200];
    uint16_t pal[1024];
    const uint8_t *bankab[16], *bankcd[16], *bankef[2];
    int bank_ab, bank_cd, bank_ef, num_banks;
    uint8_t palbank, dsw_h, dsw_g, in0, in1, adc_value;
    bool vblank;
    uint8_t rand17[POLY17 + 1];
    uint64_t main_total;
    uint8_t u_status, u_control, u_input, u_output, u_tx_byte;
    bool u_data_ready, u_tx_pending;
    int u_tx_delay;
    uint8_t s_status, s_control, s_input, s_output;
    counter_t counter[3];
    uint8_t counter_control, counter_0_ff;
    bool c0_active;
    double c0_period, c0_phase;
    uint16_t dac_value;
    uint8_t dac_register, chip_select;
    uint8_t sp_color, sp_bgcolor, sp_bits;
    double audio_error;
    int16_t audio[2048];
    int naudio;
    uint16_t fb[W * H];
} *B_;
#define B (*B_)

// ---------------------------------------------------------------------------
// ROM banks (MAME's expand_roms)

static void expand_roms(uint8_t mask)
{
    memset(B.bankab, 0, sizeof B.bankab);
    memset(B.bankcd, 0, sizeof B.bankcd);
    memset(B.bankef, 0, sizeof B.bankef);
    B.num_banks = ROM_LEN > 0x20000 ? 16 : 8;
    const uint32_t bx = (mask & SWAP_HALVES) ? 0x2000u : 0;
    for (int b = 0; b < B.num_banks; b += 8) {
        const uint8_t *ab = B.rom + 0x4000u * (uint32_t)b, *cd = ab + 0x10000;
        const uint8_t *cd_common = ab + (0x1c000u ^ bx), *ef_common = ab + (0x1e000u ^ bx);
        B.bankef[b / 8] = ef_common;
        for (int i = 0; i < 8; i++) {
            B.bankab[b + i] = ab + ((0x2000u * (uint32_t)i) ^ bx);
            B.bankcd[b + i] = (i < 6 && (mask & (1u << i))) ? cd + ((0x2000u * (uint32_t)i) ^ bx) : cd_common;
        }
    }
    B.bank_ab = B.bank_cd = B.bank_ef = 0;
}

static void game_init(void)
{
    switch (GAME) {
    case TRIVIABB: case TRIVIAG2: case TRIVIAYP: case TRIVIASP: case MINIGOLF: case GRUDGE: case RESCRAID: expand_roms(EXPAND_NONE); break;
    case TRIVIAES: case NAMETUNE: case NSTOCKER: expand_roms(EXPAND_NONE | SWAP_HALVES); break;
    case SFOOTBAL: case SPIKER: expand_roms(EXPAND_ALL | SWAP_HALVES); break;
    case STOMPIN: expand_roms(0x0c | SWAP_HALVES); break;
    default: expand_roms(EXPAND_ALL); break;
    }
}

// ---------------------------------------------------------------------------
// The soft 6850s between the boards

static void uart_update(void)
{
    if (!(B.s_status & 0x02)) {                     // sound -> main
        if (B.u_status & 0x01) B.u_status |= 0x20;
        B.u_input = B.s_output;
        B.u_status |= 0x01;
        B.s_status |= 0x02;
    }
    if (B.u_data_ready) {                           // main -> sound
        if (B.s_status & 0x01) B.s_status |= 0x20;
        B.s_input = B.u_output;
        B.s_status |= 0x01;
        B.u_status |= 0x02;
        B.u_data_ready = false;
    }
    if ((B.u_control & 3) == 3) {
        B.u_status = 0x02;
        B.u_data_ready = false;
        B.u_tx_pending = false;
    }
    if ((B.s_control & 3) == 3) B.s_status = 0x02;
    bool on = ((B.u_control & 0x80) && (B.u_status & 0x21)) || ((B.u_control & 0x60) == 0x20 && (B.u_status & 0x02));
    if (on && !(B.u_status & 0x80)) {
        m6809d_set_firq(&B.main, M6809D_ASSERT);
        B.u_status |= 0x80;
    } else if (!on && (B.u_status & 0x80)) {
        m6809d_set_firq(&B.main, M6809D_CLEAR);
        B.u_status &= (uint8_t)~0x80;
    }
    on = ((B.s_control & 0x80) && (B.s_status & 0x21)) || ((B.s_control & 0x60) == 0x20 && (B.s_status & 0x02));
    if (!(B.counter_control & 0x20)) on = false;
    if (on && !(B.s_status & 0x80)) {
        zd_set_nmi(&B.snd, ZD_ASSERT);
        B.s_status |= 0x80;
    } else if (!on && (B.s_status & 0x80)) {
        zd_set_nmi(&B.snd, ZD_CLEAR);
        B.s_status &= (uint8_t)~0x80;
    }
}

static void advance_uart(int c)
{
    if (!B.u_tx_pending) return;
    B.u_tx_delay -= c;
    if (B.u_tx_delay <= 0) {
        B.u_tx_pending = false;
        B.u_output = B.u_tx_byte;
        B.u_data_ready = true;
        uart_update();
    }
}

// ---------------------------------------------------------------------------
// The 8253 (as incomplete as classic MAME's)

static void counter_set_gate(int which, int gate);

static void counter_start(int w)
{
    if (w == 0) return;
    if (B.counter[w].gate && !B.counter[w].timer_active) {
        B.counter[w].timer_active = true;
        B.counter[w].remaining = B.counter[w].count;
    }
}

static void counter_stop(int w)
{
    B.counter[w].timer_active = false;
    B.counter[w].remaining = 0;
}

static void counter_update_count(int w)
{
    if (B.counter[w].timer_active) B.counter[w].count = B.counter[w].remaining < 0 ? 0 : (int32_t)B.counter[w].remaining;
}

static void counter_set_out(int w, int out)
{
    if (w == 2) zd_set_irq(&B.snd, out ? ZD_ASSERT : ZD_CLEAR, 0xff);
    else if (w == 0) counter_set_gate(1, !out);
    B.counter[w].out = (uint8_t)out;
}

static void counter_set_gate(int w, int gate)
{
    const int old = B.counter[w].gate;
    B.counter[w].gate = (uint8_t)gate;
    if (!gate && old) {
        counter_update_count(w);
        counter_stop(w);
    } else if (gate && !old) {
        if (B.counter[w].mode == 1) {
            counter_set_out(w, 0);
            B.counter[w].count = B.counter[w].initial + 1;
        }
        counter_start(w);
    }
}

static void counter_callback(int w)
{
    B.counter[w].timer_active = false;
    B.counter[w].count = 0;
    B.counter[w].remaining = 0;
    if (B.counter[w].mode == 0 || B.counter[w].mode == 1) counter_set_out(w, 1);
}

static uint8_t counter_r(int o)
{
    const int w = o & 3;
    if (w == 3) return 0;
    counter_update_count(w);
    if (B.counter[w].readbyte == 0) {
        B.counter[w].readbyte = 1;
        return (uint8_t)(B.counter[w].count & 0xff);
    }
    B.counter[w].readbyte = 0;
    return (uint8_t)((B.counter[w].count >> 8) & 0xff);
}

static void counter_w(int o, uint8_t d)
{
    if ((o & 3) != 3) {
        const int w = o & 3;
        counter_t *c = &B.counter[w];
        if (c->mode == 0) counter_set_out(w, 0);
        if (c->writebyte == 0) {
            c->count = (c->count & 0xff00) | d;
            c->initial = (c->initial & 0xff00) | d;
            c->writebyte = 1;
        } else {
            c->count = (c->count & 0x00ff) | ((d << 8) & 0xff00);
            c->initial = (c->initial & 0x00ff) | ((d << 8) & 0xff00);
            c->writebyte = 0;
            if (c->count == 0) c->count = c->initial = 0x10000;
            counter_stop(w);
            if (c->mode == 0) counter_start(w);
            if (c->mode == 1) counter_set_out(w, 1);
        }
    } else {
        const int w = d >> 6;
        if (w == 3) return;
        if (((B.counter[w].mode >> 1) & 7) == 0) counter_set_out(w, 0);
        B.counter[w].mode = (d >> 1) & 7;
        if (B.counter[w].mode == 0) counter_set_out(w, 0);
    }
}

static void set_counter_0_ff(int s)
{
    if (B.counter_0_ff && !s && B.counter[0].count > 0 && B.counter[0].gate) {
        B.counter[0].count--;
        if (B.counter[0].count == 0) counter_callback(0);
    }
    B.counter_0_ff = (uint8_t)s;
}

static void update_counter_0_timer(void)
{
    B.c0_active = false;
    B.c0_period = 0;
    double maxf = 0.0;
    for (int i = 0; i < 6; i++)
        if (cem3394d_get_parameter(&B.cem[i], CEM_FINAL_GAIN) < 10.0) {
            const double f = cem3394d_get_parameter(&B.cem[i], CEM_FILTER_RESONANCE) > 0.9 ? cem3394d_get_parameter(&B.cem[i], CEM_FILTER_FREQUENCY)
                                                                                          : cem3394d_get_parameter(&B.cem[i], CEM_VCO_FREQUENCY);
            if (f > maxf) maxf = f;
        }
    if (maxf > 0.0) {
        B.c0_active = true;
        B.c0_period = (double)SOUND_CLOCK / maxf;
        if (B.c0_period < 1.0) B.c0_period = 1.0;
    }
}

static void counter_control_w(uint8_t d)
{
    const uint8_t diff = B.counter_control ^ d;
    B.counter_control = d;
    if (!B.counter[0].gate && (d & 2) && !B.c0_active) update_counter_0_timer();
    else if (B.counter[0].gate && !(d & 2) && B.c0_active) {
        B.c0_active = false;
        B.c0_period = 0;
    }
    counter_set_gate(0, (d >> 1) & 1);
    if (!(d & 4)) set_counter_0_ff(1);
    if (!(d & 0x10)) set_counter_0_ff(0);
    if (diff & 0x20) uart_update();
}

static void advance_counters(int c)
{
    const int clocks = c / 2;
    if (clocks > 0)
        for (int w = 1; w <= 2; w++) {
            if (!B.counter[w].timer_active) continue;
            B.counter[w].remaining -= clocks;
            if (B.counter[w].remaining <= 0) counter_callback(w);
        }
    if (B.c0_active && B.c0_period > 0) {
        B.c0_phase += (double)c;
        while (B.c0_phase >= B.c0_period) {
            B.c0_phase -= B.c0_period;
            set_counter_0_ff((B.counter_control >> 3) & 1);
        }
    }
}

static void chip_select_w(uint8_t d)
{
    static const int kMap[8] = {CEM_VCO_FREQUENCY, CEM_FINAL_GAIN, CEM_FILTER_RESONANCE, CEM_FILTER_FREQUENCY,
                                CEM_MIXER_BALANCE, CEM_MODULATION_AMOUNT, CEM_PULSE_WIDTH, CEM_WAVE_SELECT};
    const double v = (double)B.dac_value * (8.0 / 4096.0) - 4.0;
    const int diff = d ^ B.chip_select, reg = kMap[B.dac_register & 7];
    B.chip_select = d;
    for (int i = 0; i < 6; i++)
        if ((diff & (1 << i)) && (d & (1 << i))) cem3394d_set_voltage(&B.cem[i], reg, v);
    if (B.c0_active) update_counter_0_timer();
}

static void dac_w(int o, uint8_t d)
{
    if (o & 1) B.dac_value = (uint16_t)((B.dac_value & 0xfc0) | ((d >> 2) & 0x03f));
    else B.dac_value = (uint16_t)((B.dac_value & 0x03f) | ((d << 6) & 0xfc0));
    if ((B.chip_select & 0x3f) != 0x3f) {
        const uint8_t t = B.chip_select;
        chip_select_w(0x3f);
        chip_select_w(t);
    }
}

// ---------------------------------------------------------------------------
// The sound board

static uint8_t snd_rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x2000) return B.sound_rom[a];
    if (a < 0x6000) return B.sound_ram[a];
    if (a >= 0xe000) {
        if (!(a & 1)) return B.s_status;
        const uint8_t r = B.s_input;
        B.s_status &= (uint8_t)~0x21;
        uart_update();
        return r;
    }
    return 0xff;
}

static void snd_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x2000 && a < 0x6000) B.sound_ram[a] = v;
    else if (a >= 0x6000 && a < 0x8000) {
        if (!(a & 1)) B.s_control = v;
        else {
            B.s_output = v;
            B.s_status &= (uint8_t)~0x02;
        }
        uart_update();
    }
}

static uint8_t snd_in(void *u, uint16_t p)
{
    (void)u;
    p &= 0xff;
    if ((p & 0xf8) == 0x00) return counter_r(p & 3);
    if ((p & 0xf8) == 0x08) return (uint8_t)((!B.counter_0_ff) | (B.counter[0].out ? 2 : 0));
    return 0xff;
}

static void snd_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    p &= 0xff;
    if ((p & 0xfc) == 0x00) counter_w(p & 3, v);
    else if ((p & 0xfe) == 0x08) counter_control_w(v);
    else if ((p & 0xfe) == 0x0a) dac_w(p & 1, v);
    else if ((p & 0xfe) == 0x0c) B.dac_register = v & 7;
    else if ((p & 0xfe) == 0x0e) chip_select_w(v);
}

// ---------------------------------------------------------------------------
// The 6809

static uint8_t main_rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x0800) return B.spriteram[a];
    if (a < 0x8000) return B.videoram[a - 0x0800];
    if (a < 0x9000) return B.pal_ram[a - 0x8000];
    if (a >= 0x9400 && a <= 0x9401) return B.adc_value;
    if (a == 0x9900) return B.dsw_h;
    if (a == 0x9901) return B.dsw_g;
    if (a == 0x9902) return B.in0;
    if (a == 0x9903) return (uint8_t)((B.in1 & 0x7f) | (B.vblank ? 0x80 : 0));
    if (a >= 0x9a00 && a <= 0x9a03) {               // the noise (100 kHz, the CPU at 1.25 MHz: x12.5)
        uint64_t cc = B.main_total;
        cc = (cc << 3) + (cc << 2) + (cc >> 1);
        return B.rand17[cc & POLY17];
    }
    if (a == 0x9a04) return B.u_status;
    if (a == 0x9a05) {
        const uint8_t r = B.u_input;
        B.u_status &= (uint8_t)~0x21;
        uart_update();
        return r;
    }
    if (a >= 0x9b00 && a <= 0x9cff) return B.novram[a - 0x9b00];
    if (GAME == SPIKER && a >= 0x9f80 && a <= 0x9f8f) {
        B.sp_bits = (uint8_t)(((B.sp_bits << 1) & 0xee) | ((B.sp_bits >> 3) & 0x11));
        const uint8_t l = (B.sp_bits & 0x10) ? B.sp_color : B.sp_bgcolor, r = (B.sp_bits & 0x01) ? B.sp_color : B.sp_bgcolor;
        B.sp_bgcolor = 0;
        return (uint8_t)((l & 0xf0) | (r & 0x0f));
    }
    if (a >= 0xa000 && a <= 0xbfff) {
        const uint8_t *b = B.bankab[B.bank_ab & (B.num_banks - 1)];
        return b ? b[a - 0xa000] : 0xff;
    }
    if (a >= 0xc000 && a <= 0xdfff) {
        const uint8_t *b = B.bankcd[B.bank_cd & (B.num_banks - 1)];
        return b ? b[a - 0xc000] : 0xff;
    }
    if (a >= 0xe000) {
        const uint8_t *b = B.bankef[B.bank_ef & 1];
        return b ? b[a - 0xe000] : 0xff;
    }
    return 0xff;
}

static void main_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x0800) B.spriteram[a] = v;
    else if (a < 0x8000) {
        const uint16_t o = a - 0x0800;
        B.videoram[o] = v;
        const size_t px = (size_t)o * 2;
        if (px + 1 < sizeof B.expanded) {
            B.expanded[px] = v >> 4;
            B.expanded[px + 1] = v & 0x0f;
        }
    } else if (a < 0x9000) {
        const uint16_t o = a - 0x8000;
        B.pal_ram[o] = v;
        const int i = o >> 2;
        const uint8_t *p = &B.pal_ram[i * 4];
        const unsigned r = (p[0] & 15) * 0x11, g = (p[1] & 15) * 0x11, b = (p[2] & 15) * 0x11;
        B.pal[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    } else if (a >= 0x9000 && a <= 0x9007) B.adc_value = 0x80;   // (the analog inputs centred)
    else if (a >= 0x98a0 && a <= 0x98bf) B.bank_ab = B.bank_cd = (v >> 4) & 7;
    else if (a >= 0x98c0 && a <= 0x98df) B.palbank = v & 3;
    else if (a == 0x9a04) {
        B.u_control = v;
        uart_update();
    } else if (a == 0x9a05) {
        B.u_status &= (uint8_t)~0x02;
        uart_update();
        B.u_tx_byte = v;
        B.u_tx_pending = true;
        B.u_tx_delay = UART_DELAY;
    } else if (a >= 0x9b00 && a <= 0x9cff) B.novram[a - 0x9b00] = v;
    else if (ROMBANK2 && a == 0x9f00) {
        int bank = v & 7;
        if (ROM_LEN > 0x20000) bank |= (v >> 4) & 8;
        B.bank_ab = bank;
        B.bank_cd = (v & 0x20) ? 6 : bank;
    } else if (GAME == SPIKER && a >= 0x9f80 && a <= 0x9f8f) {
        const int o = a - 0x9f80;
        if (o == 0) B.sp_bits = v;
        else if (o == 1) B.sp_bgcolor = v;
        else if (o == 2) B.sp_color = v;
    }
}

// ---------------------------------------------------------------------------
// Video

static void draw_sprite(const uint8_t *s)
{
    const int flags = s[0], image = s[1] | ((flags & 7) << 8), xpos = s[3];
    int ypos = s[2] + 17;
    const uint8_t *src = &B.sprite_rom[(64u * (uint32_t)image) & B.sprite_mask];
    if (flags & 0x80) src += 4 * 15;
    const uint16_t *pens = &B.pal[B.palbank * 256];
    for (int y = 0; y < 16; y++, ypos = (ypos + 1) & 255) {
        if (ypos >= 16 && ypos < H) {
            const uint8_t *old = &B.expanded[ypos * 256 + xpos];
            int cx = xpos;
            uint16_t *d = &B.fb[ypos * W];
            if (!(flags & 0x40)) {
                for (int x = 0; x < 4; x++, old += 2) {
                    const int px = *src++, l = px & 0xf0, r = (px << 4) & 0xf0;
                    if (l && cx >= 0 && cx < 256) d[cx] = pens[l | old[0]];
                    cx++;
                    if (r && cx >= 0 && cx < 256) d[cx] = pens[r | old[1]];
                    cx++;
                }
            } else {
                src += 4;
                for (int x = 0; x < 4; x++, old += 2) {
                    const int px = *--src, l = (px << 4) & 0xf0, r = px & 0xf0;
                    if (l && cx >= 0 && cx < 256) d[cx] = pens[l | old[0]];
                    cx++;
                    if (r && cx >= 0 && cx < 256) d[cx] = pens[r | old[1]];
                    cx++;
                }
                src += 4;
            }
        } else src += 4;
        if (flags & 0x80) src -= 2 * 4;
    }
}

static void render_frame(void)
{
    const uint16_t *pens = &B.pal[B.palbank * 256];
    for (int i = 0; i < W * H; i++) B.fb[i] = pens[B.expanded[i]];
    for (int i = 0; i < 40; i++) draw_sprite(&B.spriteram[(0xe0 + i * 4) & 0xff]);
}

static void gen_audio(int c)
{
    B.audio_error += (double)c * ((double)RATE / (double)MAIN_CLOCK);
    const int n = (int)B.audio_error;
    B.audio_error -= n;
    const bool on = B.counter_control & 1;
    for (int i = 0; i < n; i++) {
        int32_t m = 0;
        if (on) {
            for (int k = 0; k < 6; k++) m += cem3394d_update(&B.cem[k]);
            m /= 3;
        }
        if (B.naudio < 2048) B.audio[B.naudio++] = arc_clamp16(m);
    }
}

// ---------------------------------------------------------------------------

bool arc_init(void)
{
    if (!(B_ = (struct state *)calloc(1, sizeof(struct state))) || !(B.rom = (uint8_t *)malloc(ROM_LEN)) ||
        !(B.sprite_rom = (uint8_t *)calloc(1, SPRITES_END)))
        return false;
#if GAME == NAMETUNE
    // ROM_LOAD + ROM_CONTINUE: each 32 KB file in two 16 KB halves, at dest and dest + 0x20000
    static const arc_rom kChunks[] = {
        {"namethattune_ur_ab_01_3-31-86.u8a", 0x8000, 0x00000, 0xf99054f1}, {"namethattune_ur_ab_23_3-31-86.u7a", 0x8000, 0x04000, 0xf2b8f7fa},
        {"namethattune_ur_ab_45_3-31-86.u6a", 0x8000, 0x08000, 0x89e1c769}, {"namethattune_ur_ab_67_3-31-86.u5a", 0x8000, 0x0c000, 0x7e5572a1},
        {"namethattune_ur_cd_01_3-31-86.u4a", 0x8000, 0x10000, 0xdb9d6154}, {"namethattune_ur_cd_23_3-31-86.u3a", 0x8000, 0x14000, 0x9d2e458f},
        {"namethattune_ur_cd_45_3-31-86.u2a", 0x8000, 0x18000, 0x9a4b87aa}, {"namethattune_ur_cd_6_ef_3-31-86.u1a", 0x8000, 0x1c000, 0x0459e6f8}};
    memset(B.rom, 0xff, ROM_LEN);
    uint8_t t[0x8000];
    for (int i = 0; i < 8; i++) {
        arc_rom r = kChunks[i];
        r.offset = 0;
        if (!arc_load(&r, 1, t, sizeof t)) return false;
        memcpy(B.rom + kChunks[i].offset, t, 0x4000);
        memcpy(B.rom + kChunks[i].offset + 0x20000, t + 0x4000, 0x4000);
    }
#else
    memset(B.rom, 0, MAIN_END);                     // (dsp-cpp: what no ROM covers is 0, past them 0xff)
    if (ROM_LEN > MAIN_END) memset(B.rom + MAIN_END, 0xff, ROM_LEN - MAIN_END);
    if (!ARC_LOAD(kMain, B.rom, ROM_LEN)) return false;
#endif
    if (!ARC_LOAD(kSprites, B.sprite_rom, SPRITES_END)) return false;
    B.sprite_mask = SPRITES_END - 1;
    memset(B.sound_rom, 0xff, sizeof B.sound_rom);
    if (!ARC_LOAD(kSound, B.sound_rom, sizeof B.sound_rom)) return false;
    uint32_t x = 0;                                 // the 17-bit noise
    for (uint32_t i = 0; i <= POLY17; i++) {
        B.rand17[i] = (uint8_t)(x >> 3);
        x = ((x << 7) + (x >> 10) + 0x18000) & POLY17;
    }
    game_init();
    B.dsw_h = B.dsw_g = 0xff;
    B.in0 = 0xff;
    B.in1 = 0x7f;
    for (int i = 0; i < 6; i++) cem3394d_init(&B.cem[i], 431.894, 1300.0);
    B.main.read = main_rd;
    B.main.write = main_wr;
    B.snd.read = snd_rd;
    B.snd.write = snd_wr;
    B.snd.in = snd_in;
    B.snd.out = snd_out;
    zd_init(&B.snd);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m6809d_reset(&B.main);
    zd_reset(&B.snd);
    for (int i = 0; i < 6; i++) cem3394d_reset(&B.cem[i]);
    memset(B.spriteram, 0, sizeof B.spriteram);
    memset(B.videoram, 0, sizeof B.videoram);
    memset(B.expanded, 0, sizeof B.expanded);
    memset(B.pal_ram, 0, sizeof B.pal_ram);
    memset(B.pal, 0, sizeof B.pal);
    memset(B.novram, 0, sizeof B.novram);
    memset(B.sound_ram, 0, sizeof B.sound_ram);
    memset(B.fb, 0, sizeof B.fb);
    B.audio_error = 0;
    B.palbank = 0;
    B.bank_ab = B.bank_cd = B.bank_ef = 0;
    B.adc_value = 0x80;
    B.main_total = 0;
    B.vblank = false;
    memset(B.counter, 0, sizeof B.counter);
    B.counter[2].gate = 1;
    B.counter_control = B.counter_0_ff = 0;
    B.c0_active = false;
    B.c0_period = B.c0_phase = 0;
    B.dac_value = 0;
    B.dac_register = 0;
    B.chip_select = 0x3f;
    B.u_status = 0x02;
    B.u_control = 3;
    B.u_input = B.u_output = 0;
    B.u_data_ready = B.u_tx_pending = false;
    B.u_tx_delay = 0;
    B.s_status = 0x02;
    B.s_control = 3;
    B.s_input = B.s_output = 0;
    uart_update();
    B.sp_color = B.sp_bgcolor = B.sp_bits = 0;
}

static int run_main(int n)
{
    const int r = m6809d_run(&B.main, n);
    B.main_total += (uint64_t)r;
    advance_uart(r);
    gen_audio(r);
    return r;
}

int arc_run_frame(const int16_t **audio)
{
    const double mpl = (double)MAIN_CLOCK / FPS / SCANLINES, spl = (double)SOUND_CLOCK / FPS / SCANLINES;
    double mc = 0, sc = 0;
    B.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        B.vblank = line < VBEND || line >= VBSTART;
        const bool irq = (line % 64) == 0;
        mc += mpl;
        sc += spl;
        const int mb = (int)mc > 1 ? (int)mc : 1, sb = (int)sc > 1 ? (int)sc : 1;
        mc -= mb;
        sc -= sb;
        int hm = mb * HBSTART / HTOTAL, hs = sb * HBSTART / HTOTAL;
        if (hm < 1) hm = 1;
        if (hs < 1) hs = 1;
        const int am = mb - hm > 0 ? mb - hm : 0, as = sb - hs > 0 ? sb - hs : 0;
        if (irq) m6809d_set_irq(&B.main, M6809D_ASSERT);
        run_main(hm);
        advance_counters(zd_run(&B.snd, hs));
        if (irq) m6809d_set_irq(&B.main, M6809D_CLEAR);
        if (am > 0) run_main(am);
        if (as > 0) advance_counters(zd_run(&B.snd, as));
        if (line == VBSTART) render_frame();
    }
    if (!B.vblank) render_frame();
    if (audio) *audio = B.audio;
    return B.naudio;
}

const uint16_t *arc_frame(void) { return B.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    B.in0 = 0xff;
    B.in1 = 0x7f;
#if GAME == CSHIFT
    if (p1 & AP_B1) B.in0 &= (uint8_t)~0x01;
    if (p1 & AP_B2) B.in0 &= (uint8_t)~0x02;
    if (in->coin1) B.in0 &= (uint8_t)~0x40;
    if (in->service) B.in0 &= (uint8_t)~0x80;
    if (p1 & AP_START) B.in1 &= (uint8_t)~0x01;
    if (p2 & AP_START) B.in1 &= (uint8_t)~0x02;
    if (in->coin2) B.in1 &= (uint8_t)~0x40;
#else
    if (p1 & AP_UP) B.in0 &= (uint8_t)~0x01;
    if (p1 & AP_DOWN) B.in0 &= (uint8_t)~0x02;
    if (p1 & AP_RIGHT) B.in0 &= (uint8_t)~0x04;
    if (p1 & AP_LEFT) B.in0 &= (uint8_t)~0x08;
    if (p1 & AP_B1) B.in0 &= (uint8_t)~0x10;
    if (p2 & AP_B1) B.in0 &= (uint8_t)~0x20;
    if (in->coin1) B.in0 &= (uint8_t)~0x40;
    if (in->service) B.in0 &= (uint8_t)~0x80;
    if (p1 & AP_START) B.in1 &= (uint8_t)~0x01;
    if (p2 & AP_START) B.in1 &= (uint8_t)~0x02;
    if (p2 & AP_UP) B.in1 &= (uint8_t)~0x04;
    if (p2 & AP_DOWN) B.in1 &= (uint8_t)~0x08;
    if (p2 & AP_RIGHT) B.in1 &= (uint8_t)~0x10;
    if (p2 & AP_LEFT) B.in1 &= (uint8_t)~0x20;
    if (in->coin2) B.in1 &= (uint8_t)~0x40;
#endif
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) B.dsw_h = v;
    else if (bank == 1) B.dsw_g = v;
}
