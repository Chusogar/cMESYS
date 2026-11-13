// core.h - what each machine of the Android port gives core_jni.c (one
// m_<machine>.c is linked into each APK's libcore.so). The Java side
// (com.chusogar.cmesys.app.Core) owns everything that is Android; the
// machine files run frames, draw ARGB pixels and take the media.
//
// Keys: the machine's own key codes (its matrix row * 8 + bit, as in its
// core's header and in the keyboard pictures), all the keys held in one
// array, given before each frame. Joysticks: generic bits (MC_JOY_*), turned
// into the machine's own by the machine file.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MC_KEYS 128

enum { MC_JOY_UP = 1, MC_JOY_DOWN = 2, MC_JOY_LEFT = 4, MC_JOY_RIGHT = 8, MC_JOY_FIRE = 16, MC_JOY_FIRE2 = 32 };

// status bits (lights shown over the picture and the menu's state)
enum {
    MC_LED1 = 1, MC_LED2 = 2,               // drive / microdrive lights
    MC_TAPE_LOADED = 4, MC_TAPE_PLAYING = 8,
    MC_HAS_DISK = 16,                       // a disk ROM / drive is there
};

// commands (Core.command)
enum {
    MC_CMD_TAPE_TOGGLE = 1,                 // play / pause the tape
    MC_CMD_AUTOBOOT = 2,                    // QL: F1 and boot MDV1
    MC_CMD_MAGIC = 3,                       // Spectrum clones: the magic button (NMI)
    MC_CMD_MONITOR = 4,                     // CPC: colour / green monitor
    MC_CMD_FAST_LOAD_ON = 5,                // C64: LOAD from the .d64 at once
    MC_CMD_FAST_LOAD_OFF = 6,
};

// roms[i] (malloc'd, lens[i] bytes, NULL when absent) are the machine's:
// the machine keeps them (the cores read them in place) and frees the old
// ones. False: they are not right.
bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time);
void mc_reset(void);
int mc_width(void);
int mc_height(void);
double mc_fps(void);
int mc_audio_rate(void);
// One frame with these keys held; *audio gets its samples (mono).
int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio);
void mc_render(uint32_t *argb);                     // mc_width() x mc_height()
void mc_joy(int port, unsigned bits);              // port 1 or 2
// A file of the phone (copied to the app's cache with its own name): slot 0
// is the machine's default, others are its drives (QL: 1 = MDV1, 2 = MDV2,
// 0 = MDV1 and boot). NULL: loaded; otherwise why not.
const char *mc_load(const char *path, int slot);
void mc_eject(int slot);
void mc_turbo(bool on);
int mc_status(void);
void mc_command(int cmd);
