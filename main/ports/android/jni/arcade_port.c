// arcade_port.c - the desktop frontend layer of the arcade drivers
// (main/ports/sdl2/sdl_port.h) for Android: each game's own <game>_sdl.c
// (compiled with -Dmain=cmesys_game_main) runs unchanged on the app's
// emulation thread, so every game plays and sounds as on the desktop.
// Its pictures, sound and the frame pacing go to the Java side
// (com.chusogar.cmesys.arcade.Native), the controls come from it.
//
// One library per game (libgame_<id>.so), all with these same JNI names: a
// game runs in its own process (the app's ":game"), which loads only its
// library.
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sdl_port.h"

extern int cmesys_game_main(int argc, char **argv);
extern int chdir(const char *path);

static JavaVM *vm;
static JNIEnv *env;                 // the emulation thread's (run)
static jclass cls;
static jmethodID m_open, m_audio, m_frame;
static jshortArray jaudio;
static jintArray jpixels;
#define AUDIO_CHUNK 8192

static sdl_port_cfg cfg;
static uint32_t *argb;
static volatile uint32_t keys;
static volatile int quit, reset_req, turbo;

// Sound: the game's rate, or resampled (linearly) to the one the device took
static int out_rate;
static uint32_t step;               // 16.16 input samples per output sample
static uint32_t frac;
static int16_t last_in;
static int16_t chunk[AUDIO_CHUNK];

JNIEXPORT jint JNI_OnLoad(JavaVM *jvm, void *reserved)
{
    (void)reserved;
    vm = jvm;
    return JNI_VERSION_1_6;
}

// --- sdl_port.h ------------------------------------------------------------------
void sdl_port_args(int *argc, char **argv) { (void)argc, (void)argv; }

bool sdl_port_open(const sdl_port_cfg *c)
{
    cfg = *c;
    argb = (uint32_t *)malloc((size_t)cfg.width * cfg.height * 4);
    if (!argb) return false;
    jintArray p = (*env)->NewIntArray(env, cfg.width * cfg.height);
    jshortArray a = (*env)->NewShortArray(env, AUDIO_CHUNK);
    if (!p || !a) return false;
    jpixels = (jintArray)(*env)->NewGlobalRef(env, p);
    jaudio = (jshortArray)(*env)->NewGlobalRef(env, a);
    jstring title = (*env)->NewStringUTF(env, cfg.title ? cfg.title : "");
    out_rate = (*env)->CallStaticIntMethod(env, cls, m_open, title, cfg.width, cfg.height, cfg.audio_rate, cfg.fps);
    if (out_rate > 0 && cfg.audio_rate > 0 && out_rate != cfg.audio_rate)
        step = (uint32_t)(((uint64_t)cfg.audio_rate << 16) / (uint32_t)out_rate);
    else
        step = 0;
    frac = 0;
    last_in = 0;
    return true;
}

void sdl_port_close(void)
{
    if (jpixels) (*env)->DeleteGlobalRef(env, jpixels);
    if (jaudio) (*env)->DeleteGlobalRef(env, jaudio);
    jpixels = NULL;
    jaudio = NULL;
    free(argb);
    argb = NULL;
}

bool sdl_port_poll(bool *reset)
{
    *reset = __atomic_exchange_n(&reset_req, 0, __ATOMIC_SEQ_CST) != 0;
    return !quit;
}

uint32_t sdl_port_keys(void) { return keys; }
bool sdl_port_paused(void) { return false; }        // (the Java side waits in onFrame)
bool sdl_port_key(int scancode) { (void)scancode; return false; }
int sdl_port_next_press(void) { return -1; }
void sdl_port_set_title(const char *title) { (void)title; }
char *sdl_port_next_drop(void) { return NULL; }
void sdl_port_queue_drop(const char *path) { (void)path; }
void sdl_port_set_turbo(bool on) { turbo = on; }
bool sdl_port_turbo(void) { return turbo != 0; }

static void send_audio(const int16_t *s, int n)
{
    while (n > 0) {
        int k = n < AUDIO_CHUNK ? n : AUDIO_CHUNK;
        (*env)->SetShortArrayRegion(env, jaudio, 0, k, s);
        (*env)->CallStaticVoidMethod(env, cls, m_audio, jaudio, k);
        s += k;
        n -= k;
    }
}

void sdl_port_audio(const int16_t *s, int n)
{
    if (n <= 0 || out_rate <= 0 || turbo || !jaudio) return;
    if (!step) {
        send_audio(s, n);
        return;
    }
    // linear resampling: frac is the position between last_in (input -1) and s[i]
    int m = 0;
    for (int i = 0; i < n;) {
        int16_t a = i ? s[i - 1] : last_in, b = s[i];
        chunk[m++] = (int16_t)(a + (((int32_t)(b - a) * (int32_t)(frac & 0xffff)) >> 16));
        if (m == AUDIO_CHUNK) {
            send_audio(chunk, m);
            m = 0;
        }
        frac += step;
        i += (int)(frac >> 16);
        frac &= 0xffff;
    }
    last_in = s[n - 1];
    if (m) send_audio(chunk, m);
}

// Shows the picture (0xRRGGBB) and waits for the next frame time (Java side)
void sdl_port_present(const uint32_t *rgb)
{
    if (!jpixels) return;
    int n = cfg.width * cfg.height;
    for (int i = 0; i < n; i++) argb[i] = 0xff000000u | rgb[i];
    (*env)->SetIntArrayRegion(env, jpixels, 0, n, (const jint *)argb);
    (*env)->CallStaticVoidMethod(env, cls, m_frame, jpixels);
}

// --- JNI: com.chusogar.cmesys.arcade.Native --------------------------------------------
#define JNI_FN(name) Java_com_chusogar_cmesys_arcade_Native_##name

// Runs the game (its desktop main) until requestQuit(); dir: where it keeps
// its NVRAM / EEPROM files.
JNIEXPORT void JNICALL JNI_FN(run)(JNIEnv *e, jclass c, jstring dir)
{
    env = e;
    cls = (jclass)(*e)->NewGlobalRef(e, c);
    m_open = (*e)->GetStaticMethodID(e, c, "onOpen", "(Ljava/lang/String;IIID)I");
    m_audio = (*e)->GetStaticMethodID(e, c, "onAudio", "([SI)V");
    m_frame = (*e)->GetStaticMethodID(e, c, "onFrame", "([I)V");
    if (!m_open || !m_audio || !m_frame) return;
    const char *d = (*e)->GetStringUTFChars(e, dir, NULL);
    if (d) {
        chdir(d);
        (*e)->ReleaseStringUTFChars(e, dir, d);
    }
    quit = 0;
    static char name[] = "cmesys";
    char *argv[] = {name, NULL};
    cmesys_game_main(1, argv);
}

JNIEXPORT void JNICALL JNI_FN(setKeys)(JNIEnv *e, jclass c, jint bits)
{
    (void)e, (void)c;
    keys = (uint32_t)bits;
}

JNIEXPORT void JNICALL JNI_FN(requestReset)(JNIEnv *e, jclass c)
{
    (void)e, (void)c;
    reset_req = 1;
}

JNIEXPORT void JNICALL JNI_FN(requestQuit)(JNIEnv *e, jclass c)
{
    (void)e, (void)c;
    quit = 1;
}

JNIEXPORT void JNICALL JNI_FN(setTurbo)(JNIEnv *e, jclass c, jboolean on)
{
    (void)e, (void)c;
    turbo = on != JNI_FALSE;
}
