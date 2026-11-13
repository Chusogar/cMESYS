package com.chusogar.cmesys.arcade;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Process;
import android.os.SystemClock;

/** The game's library (libgame_<id>.so: the game's desktop frontend on
 *  jni/arcade_port.c) and what it calls back on the emulation thread: the
 *  window's opening, the sound and each frame.
 *
 *  Pacing as on the desktop (main/ports/sdl2/sdl_port.c): by the clock at
 *  the game's frame rate, the sound queued with a little to spare (more is
 *  dropped). The frame time is nudged by up to 0.5 % by how full the sound
 *  queue is, so that the AudioTrack's clock and the frames' do not drift
 *  apart (no gaps, no drops). */
final class Native {
    static native void run(String dir);
    static native void setKeys(int bits);
    static native void requestReset();
    static native void requestQuit();
    static native void setTurbo(boolean on);

    // sdl_port.h controls (SP_*)
    static final int COIN1 = 1, COIN2 = 2, START1 = 4, START2 = 8, UP = 16, DOWN = 32, LEFT = 64, RIGHT = 128,
            B1 = 256, B2 = 512, B3 = 1024;

    interface Screen {
        void showFrame(int[] pixels, int w, int h, float fps);
    }

    private static Screen screen;
    private static int width, height;
    private static double frameRate = 60;
    private static AudioTrack track;
    private static int rate;
    private static long written;          // samples written to the track
    private static int target, cap;       // samples queued: aimed at, at most
    private static boolean playing;
    private static long next;
    private static volatile boolean paused, suspended, turboOn, sound = true;
    private static final Object lock = new Object();
    private static float fps;
    private static int fpsFrames, turboSkip;
    private static long fpsStart;

    private Native() {}

    static void load(String game) {
        System.loadLibrary("game_" + game);
    }

    static void setScreen(Screen s) { screen = s; }

    static void setPaused(boolean p) {
        synchronized (lock) {
            paused = p;
            lock.notifyAll();
        }
    }

    static boolean isPaused() { return paused; }

    static void setSuspended(boolean s) {
        synchronized (lock) {
            suspended = s;
            lock.notifyAll();
        }
    }

    static void turbo(boolean on) {
        turboOn = on;
        setTurbo(on);
    }

    static boolean isTurbo() { return turboOn; }

    static void setSound(boolean s) { sound = s; }

    // --- controls ----------------------------------------------------------------------
    // What the player holds (UI thread), and the coins: each one held for
    // COIN_FRAMES of the game's frames and let go as long (counted in frames,
    // as MAME's coin impulse: a pulse in milliseconds is a different length
    // to the game on a slow device, and some games see a "COIN ERROR").
    private static final int COIN_FRAMES = 8;
    private static volatile int held;
    private static int coinsQueued, coinPhase;     // (emulation thread, under lock)
    private static int sentKeys = -1;

    static void setHeld(int bits) {
        held = bits;
    }

    static void insertCoin(int bit) {
        synchronized (lock) {
            coinsQueued++;
            coinBit = bit;
        }
    }

    private static int coinBit = COIN1;
    private static int tapBits, tapFrames;

    /** Presses these for a few of the game's frames (the menu's START 2). */
    static void tap(int bits) {
        synchronized (lock) {
            tapBits |= bits;
            tapFrames = COIN_FRAMES;
        }
    }

    /** Each frame: the keys for the next one. */
    private static void frameKeys() {
        int coin = 0;
        synchronized (lock) {
            if (coinPhase > 0) {
                coinPhase--;
                if (coinPhase >= COIN_FRAMES) coin = coinBit;
            } else if (coinsQueued > 0) {
                coinsQueued--;
                coinPhase = COIN_FRAMES * 2 - 1;
                coin = coinBit;
            }
        }
        int tap = 0;
        synchronized (lock) {
            if (tapFrames > 0) {
                tapFrames--;
                tap = tapBits;
            } else {
                tapBits = 0;
            }
        }
        int k = held | coin | tap;
        if (k != sentKeys) {
            sentKeys = k;
            setKeys(k);
        }
    }

    /** On the emulation thread: runs the game until requestQuit(). */
    static void runGame(String dir) {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
        run(dir);
        if (track != null) {
            track.release();
            track = null;
        }
    }

    // --- called by arcade_port.c -------------------------------------------------------
    /** The game's window: returns the sound rate the device plays (0: none). */
    static int onOpen(String title, int w, int h, int audioRate, double gameFps) {
        width = w;
        height = h;
        frameRate = gameFps > 1 ? gameFps : 60;
        next = System.nanoTime();
        fpsStart = SystemClock.elapsedRealtime();
        if (audioRate <= 0) return 0;
        // the game's own rate; if the device will not take it, 48 / 44.1 kHz (resampled)
        for (int r : new int[] {audioRate, 48000, 44100}) {
            if (openTrack(r)) return rate;
        }
        return 0;
    }

    private static boolean openTrack(int r) {
        int min = AudioTrack.getMinBufferSize(r, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        if (min <= 0) return false;
        // queue: about 60 ms aimed at, 120 ms (and the device's minimum) at most
        target = Math.max(r * 60 / 1000, min / 2);
        cap = Math.max(r * 120 / 1000, target + min / 2);
        int size = Math.max(min * 2, (cap + r / 20) * 2) & ~3;
        try {
            AudioTrack t = new AudioTrack(
                    new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build(),
                    new AudioFormat.Builder().setSampleRate(r).setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build(),
                    size, AudioTrack.MODE_STREAM, AudioManager.AUDIO_SESSION_ID_GENERATE);
            if (t.getState() != AudioTrack.STATE_INITIALIZED) {
                t.release();
                return false;
            }
            track = t;
            rate = r;
            written = 0;
            playing = false;
            return true;
        } catch (IllegalArgumentException e) {
            return false;
        }
    }

    private static int queued() {
        if (track == null) return 0;
        long head = track.getPlaybackHeadPosition() & 0xffffffffL;
        long q = written - head;
        return q < 0 ? 0 : (int) Math.min(q, Integer.MAX_VALUE);
    }

    /** The game's samples of a frame (rate: what onOpen returned). */
    static void onAudio(short[] s, int n) {
        if (track == null || !sound || turboOn || n <= 0) return;
        if (playing && queued() + n > cap) return;          // far ahead: dropped (as the desktop)
        track.write(s, 0, n);
        written += n;
        if (!playing && written >= target) {                // a little queued before playing
            track.play();
            playing = true;
        }
    }

    private static void stopSound() {
        if (track != null && playing) {
            track.pause();
            track.flush();
        }
        playing = false;
        written = 0;
    }

    /** Each frame: shows it, then waits for the next frame time (and while paused). */
    static void onFrame(int[] pixels) {
        frameKeys();
        if (!turboOn || (turboSkip++ & 3) == 0) {
            Screen s = screen;
            if (s != null) s.showFrame(pixels, width, height, fps);
        }
        fpsFrames++;
        long now = SystemClock.elapsedRealtime();
        if (now - fpsStart >= 1000) {
            fps = fpsFrames * 1000f / (now - fpsStart);
            fpsFrames = 0;
            fpsStart = now;
        }
        if (paused || suspended) {
            stopSound();
            synchronized (lock) {
                while (paused || suspended) {
                    try {
                        lock.wait();
                    } catch (InterruptedException e) {
                        return;
                    }
                }
            }
            next = System.nanoTime();
            return;
        }
        if (turboOn) {
            stopSound();
            next = System.nanoTime();
            return;
        }
        if (!sound && playing) stopSound();
        double frameNs = 1e9 / frameRate;
        if (playing) {
            // nudge: a fuller queue than aimed at slows the frames, an emptier one hurries them
            double off = (queued() - target) / (double) Math.max(1, target);
            frameNs *= 1 + 0.005 * Math.max(-1, Math.min(1, off));
        }
        next += (long) frameNs;
        long wait = next - System.nanoTime();
        if (wait > 0) {
            try {
                Thread.sleep(wait / 1000000, (int) (wait % 1000000));
            } catch (InterruptedException ignored) {
            }
        } else if (wait < -frameNs * 5) {
            next = System.nanoTime();         // fell behind: do not race to catch up
        }
    }
}
