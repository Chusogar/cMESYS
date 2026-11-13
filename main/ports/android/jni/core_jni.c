// core_jni.c - the calls of com.chusogar.cmesys.app.Core, the same for every
// machine of the Android port: they go to the m_<machine>.c linked with it
// (core.h). Only the emulation thread calls them once it runs.
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"

#define JNI_FN(name) Java_com_chusogar_cmesys_app_Core_##name

static bool ready;
static uint8_t keys[MC_KEYS];
static uint32_t *frame;
static int frame_px;

JNIEXPORT jboolean JNICALL JNI_FN(init)(JNIEnv *env, jclass cls, jint model, jobjectArray roms, jlong unix_time)
{
    (void)cls;
    int n = roms ? (*env)->GetArrayLength(env, roms) : 0;
    if (n > 8) n = 8;
    uint8_t *data[8] = { 0 };
    int lens[8] = { 0 };
    for (int i = 0; i < n; i++) {
        jbyteArray a = (jbyteArray)(*env)->GetObjectArrayElement(env, roms, i);
        if (!a) continue;
        lens[i] = (*env)->GetArrayLength(env, a);
        data[i] = (uint8_t *)malloc(lens[i] > 0 ? (size_t)lens[i] : 1);
        if (data[i]) (*env)->GetByteArrayRegion(env, a, 0, lens[i], (jbyte *)data[i]);
        (*env)->DeleteLocalRef(env, a);
    }
    memset(keys, 0, sizeof keys);
    ready = mc_init(model, data, lens, n, (int64_t)unix_time);
    return ready ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(reset)(JNIEnv *env, jclass cls)
{
    (void)env, (void)cls;
    if (ready) mc_reset();
}

JNIEXPORT jint JNICALL JNI_FN(width)(JNIEnv *env, jclass cls) { (void)env, (void)cls; return mc_width(); }
JNIEXPORT jint JNICALL JNI_FN(height)(JNIEnv *env, jclass cls) { (void)env, (void)cls; return mc_height(); }
JNIEXPORT jdouble JNICALL JNI_FN(fps)(JNIEnv *env, jclass cls) { (void)env, (void)cls; return mc_fps(); }
JNIEXPORT jint JNICALL JNI_FN(audioRate)(JNIEnv *env, jclass cls) { (void)env, (void)cls; return mc_audio_rate(); }

// One frame. pixels: width x height ARGB (null: not drawn); returns the
// samples put in audio.
JNIEXPORT jint JNICALL JNI_FN(runFrame)(JNIEnv *env, jclass cls, jintArray pixels, jshortArray audio)
{
    (void)cls;
    if (!ready) return 0;
    const int16_t *samples = NULL;
    int n = mc_run_frame(keys, &samples);
    if (audio && samples && n > 0) {
        jsize cap = (*env)->GetArrayLength(env, audio);
        if (n > cap) n = cap;
        (*env)->SetShortArrayRegion(env, audio, 0, n, (const jshort *)samples);
    } else {
        n = 0;
    }
    int px = mc_width() * mc_height();
    if (pixels && (*env)->GetArrayLength(env, pixels) >= px) {
        if (frame_px < px) {
            free(frame);
            frame = (uint32_t *)malloc((size_t)px * 4);
            frame_px = frame ? px : 0;
        }
        if (frame) {
            mc_render(frame);
            (*env)->SetIntArrayRegion(env, pixels, 0, px, (const jint *)frame);
        }
    }
    return n;
}

JNIEXPORT void JNICALL JNI_FN(key)(JNIEnv *env, jclass cls, jint code, jboolean down)
{
    (void)env, (void)cls;
    if (code >= 0 && code < MC_KEYS) keys[code] = down != JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(keysClear)(JNIEnv *env, jclass cls)
{
    (void)env, (void)cls;
    memset(keys, 0, sizeof keys);
}

JNIEXPORT void JNICALL JNI_FN(joy)(JNIEnv *env, jclass cls, jint port, jint bits)
{
    (void)env, (void)cls;
    if (ready) mc_joy(port, (unsigned)bits);
}

JNIEXPORT jstring JNICALL JNI_FN(load)(JNIEnv *env, jclass cls, jstring path, jint slot)
{
    (void)cls;
    if (!ready) return (*env)->NewStringUTF(env, "not started");
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    if (!p) return (*env)->NewStringUTF(env, "out of memory");
    const char *err = mc_load(p, slot);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return err ? (*env)->NewStringUTF(env, err[0] ? err : "?") : NULL;
}

JNIEXPORT void JNICALL JNI_FN(eject)(JNIEnv *env, jclass cls, jint slot)
{
    (void)env, (void)cls;
    if (ready) mc_eject(slot);
}

JNIEXPORT void JNICALL JNI_FN(turbo)(JNIEnv *env, jclass cls, jboolean on)
{
    (void)env, (void)cls;
    if (ready) mc_turbo(on != JNI_FALSE);
}

JNIEXPORT jint JNICALL JNI_FN(status)(JNIEnv *env, jclass cls)
{
    (void)env, (void)cls;
    return ready ? mc_status() : 0;
}

JNIEXPORT void JNICALL JNI_FN(command)(JNIEnv *env, jclass cls, jint cmd)
{
    (void)env, (void)cls;
    if (ready) mc_command(cmd);
}
