package com.chusogar.cmesys.app;

/** The machine's core (libcore.so: jni/core_jni.c and the m_<machine>.c of
 *  this APK). Not thread safe: only the emulation thread calls it once it
 *  runs. */
final class Core {
    static {
        System.loadLibrary("core");
    }

    // joystick bits (jni/core.h MC_JOY_*)
    static final int JOY_UP = 1, JOY_DOWN = 2, JOY_LEFT = 4, JOY_RIGHT = 8, JOY_FIRE = 16, JOY_FIRE2 = 32;
    // status bits
    static final int LED1 = 1, LED2 = 2, TAPE_LOADED = 4, TAPE_PLAYING = 8, HAS_DISK = 16;
    // commands
    static final int CMD_TAPE_TOGGLE = 1, CMD_AUTOBOOT = 2, CMD_MAGIC = 3, CMD_MONITOR = 4,
            CMD_FAST_LOAD_ON = 5, CMD_FAST_LOAD_OFF = 6;

    static final int AUDIO_MAX = 4096;

    private Core() {}

    /** roms: the machine's ROMs (null entries: absent). */
    static native boolean init(int model, byte[][] roms, long unixTime);
    static native void reset();
    static native int width();
    static native int height();
    static native double fps();
    static native int audioRate();
    /** One frame: pixels width x height ARGB (null: not drawn); returns the samples put in audio. */
    static native int runFrame(int[] pixels, short[] audio);
    static native void key(int code, boolean down);
    static native void keysClear();
    static native void joy(int port, int bits);
    /** A file (in the app's cache, with its own name); null: loaded, otherwise why not. */
    static native String load(String path, int slot);
    static native void eject(int slot);
    static native void turbo(boolean on);
    static native int status();
    static native void command(int cmd);
}
