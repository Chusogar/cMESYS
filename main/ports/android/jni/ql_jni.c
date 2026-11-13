// ql_jni.c - the cMESYS Sinclair QL core (ql_core.c) for the Android app
// (main/ports/android): the calls of com.chusogar.cmesys.ql.QLNative.
//
// The Java side owns everything that is Android (screen, sound, input, files,
// menus); this file runs frames, turns the ZX8301 screen into ARGB pixels and
// keeps the ROMs and the microdrive cartridges the core reads in place.
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "ql_core.h"
#include "ql_mdv.h"
#include "zx8301.h"

#define JNI_FN(name) Java_com_chusogar_cmesys_ql_QLNative_##name

static uint8_t *rom, *ipc;                 // kept: the core reads them in place
static uint8_t *ram[QL_RAM_BLOCKS];
static uint8_t *cart[2];                   // cartridge images (mdv1, mdv2)
static bool ready;
static uint32_t palette[8];

// A copy of a Java byte[] (NULL: none, or not `size` bytes when size > 0)
static uint8_t *copy_bytes(JNIEnv *env, jbyteArray a, jsize size)
{
    if (!a) return NULL;
    jsize n = (*env)->GetArrayLength(env, a);
    if (size > 0 && n != size) return NULL;
    uint8_t *p = (uint8_t *)malloc((size_t)n);
    if (p) (*env)->GetByteArrayRegion(env, a, 0, n, (jbyte *)p);
    return p;
}

// rom: the 48 KB system ROM (JS, JM, Minerva...), ipc: the 2 KB 8749 program.
JNIEXPORT jboolean JNICALL JNI_FN(init)(JNIEnv *env, jclass cls, jbyteArray rom_bytes, jbyteArray ipc_bytes,
                                         jlong unix_time)
{
    (void)cls;
    uint8_t *r = copy_bytes(env, rom_bytes, QL_ROM_BYTES);
    uint8_t *i = copy_bytes(env, ipc_bytes, QL_IPC_BYTES);
    if (!r || !i) {
        free(r);
        free(i);
        return JNI_FALSE;
    }
    for (int b = 0; b < QL_RAM_BLOCKS; b++) {
        if (!ram[b]) ram[b] = (uint8_t *)malloc(0x8000);
        if (!ram[b]) return JNI_FALSE;
        memset(ram[b], 0, 0x8000);
    }
    // (a new ROM: the cartridges stay in the drives)
    uint8_t *old_rom = rom, *old_ipc = ipc;
    rom = r;
    ipc = i;
    ql_init(rom, ipc, ram, (uint32_t)unix_time);
    free(old_rom);
    free(old_ipc);
    for (int d = 0; d < 2; d++) ql_cart_insert(d, cart[d]);
    for (int c = 0; c < 8; c++) palette[c] = 0xff000000u | zx8301_rgb(c);
    ready = true;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL JNI_FN(reset)(JNIEnv *env, jclass cls)
{
    (void)env, (void)cls;
    if (ready) ql_reset();
}

// One frame. pixels: 512x256 ARGB (NULL: not drawn), audio: at least
// QL_AUDIO_MAX samples (22050 Hz, mono). Returns the samples made.
JNIEXPORT jint JNICALL JNI_FN(runFrame)(JNIEnv *env, jclass cls, jintArray pixels, jshortArray audio)
{
    (void)cls;
    if (!ready) return 0;
    const int16_t *samples;
    int n = ql_run_frame(&samples);
    if (audio && n > 0) {
        jsize cap = (*env)->GetArrayLength(env, audio);
        if (n > cap) n = cap;
        (*env)->SetShortArrayRegion(env, audio, 0, n, (const jshort *)samples);
    }
    if (pixels && (*env)->GetArrayLength(env, pixels) >= QL_W * QL_H) {
        jint *out = (jint *)(*env)->GetPrimitiveArrayCritical(env, pixels, NULL);
        if (out) {
            const uint8_t *scr = ql_screen();
            uint8_t ctrl = ql_video_control();
            bool flash = ql_flash();
            uint8_t row[QL_W];
            for (int y = 0; y < QL_H; y++) {
                zx8301_render_row(scr, ctrl, flash, y, row);
                jint *o = out + y * QL_W;
                for (int x = 0; x < QL_W; x++) o[x] = (jint)palette[row[x] & 7];
            }
            (*env)->ReleasePrimitiveArrayCritical(env, pixels, out, 0);
        }
    }
    return n;
}

JNIEXPORT void JNICALL JNI_FN(key)(JNIEnv *env, jclass cls, jint code, jboolean down)
{
    (void)env, (void)cls;
    if (ready && code >= 0 && code < 64) ql_key(code, down != JNI_FALSE);
}

JNIEXPORT void JNICALL JNI_FN(keysClear)(JNIEnv *env, jclass cls)
{
    (void)env, (void)cls;
    if (ready) ql_keys_clear();
}

// Puts a QLAY .mdv (174930 bytes) in drive 0 (mdv1) or 1 (mdv2); NULL takes
// the cartridge out. False: not a cartridge image.
JNIEXPORT jboolean JNICALL JNI_FN(cartInsert)(JNIEnv *env, jclass cls, jint drive, jbyteArray image)
{
    (void)cls;
    drive &= 1;
    uint8_t *img = NULL;
    if (image) {
        img = copy_bytes(env, image, QL_MDV_IMAGE_BYTES);
        if (!img) return JNI_FALSE;
    }
    if (ready) ql_cart_insert(drive, img);
    free(cart[drive]);
    cart[drive] = img;
    return JNI_TRUE;
}

// mdv1 = image, then reset and boot it (F1 at the start-up prompt).
JNIEXPORT jboolean JNICALL JNI_FN(mount)(JNIEnv *env, jclass cls, jbyteArray image)
{
    (void)cls;
    uint8_t *img = NULL;
    if (image) {
        img = copy_bytes(env, image, QL_MDV_IMAGE_BYTES);
        if (!img) return JNI_FALSE;
    }
    if (ready) ql_mount(img);
    free(cart[0]);
    cart[0] = img;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL JNI_FN(autoboot)(JNIEnv *env, jclass cls)
{
    (void)env, (void)cls;
    if (ready) ql_autoboot();
}

JNIEXPORT jboolean JNICALL JNI_FN(cartLoaded)(JNIEnv *env, jclass cls, jint drive)
{
    (void)env, (void)cls;
    return ready && ql_cart_loaded(drive & 1) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL JNI_FN(cartMotor)(JNIEnv *env, jclass cls, jint drive)
{
    (void)env, (void)cls;
    return ready && ql_cart_motor(drive & 1) ? JNI_TRUE : JNI_FALSE;
}
