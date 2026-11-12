package com.chusogar.cmesys.ql;

/** The cMESYS Sinclair QL core (libqlcore.so: ql_core.c through jni/ql_jni.c).
 *  Not thread safe: only the emulation thread calls it once it runs. */
final class QLNative {
    static {
        System.loadLibrary("qlcore");
    }

    static final int WIDTH = 512, HEIGHT = 256;
    static final double FPS = 50.08;
    static final int AUDIO_RATE = 22050, AUDIO_MAX = 480;
    static final int ROM_BYTES = 0xc000, IPC_BYTES = 0x800, MDV_BYTES = 255 * 686;

    private QLNative() {}

    static native boolean init(byte[] rom, byte[] ipc, long unixTime);
    static native void reset();
    /** One frame: pixels 512x256 ARGB (null: not drawn); returns the samples put in audio. */
    static native int runFrame(int[] pixels, short[] audio);
    static native void key(int code, boolean down);
    static native void keysClear();
    /** drive 0 = mdv1, 1 = mdv2; image null takes the cartridge out. */
    static native boolean cartInsert(int drive, byte[] image);
    /** mdv1 = image, then reset and boot it (F1 at the start-up prompt). */
    static native boolean mount(byte[] image);
    static native void autoboot();
    static native boolean cartLoaded(int drive);
    static native boolean cartMotor(int drive);
}
