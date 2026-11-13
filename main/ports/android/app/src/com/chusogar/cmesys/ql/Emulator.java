package com.chusogar.cmesys.ql;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.SystemClock;

import java.util.ArrayDeque;

/** The emulation thread: runs the QL frame by frame (50.08 Hz, paced by the
 *  sound when it is on), sends the picture to the screen and the samples to
 *  an AudioTrack, and carries out the commands of the interface (they run
 *  here, between frames: the core is not thread safe). */
final class Emulator implements Runnable {
    interface Screen {
        /** A frame's picture (512x256 ARGB), on the emulation thread. */
        void showFrame(int[] pixels, float fps, boolean mdv1, boolean mdv2);
    }

    private final Screen screen;
    private final int[] pixels = new int[QLNative.WIDTH * QLNative.HEIGHT];
    private final short[] audio = new short[QLNative.AUDIO_MAX];
    private final ArrayDeque<Runnable> commands = new ArrayDeque<>();
    // Keys held: by source (physical keyboard / pad, on-screen keyboard, taps
    // from the menu), as counts so that two sources can hold the same key.
    private final int[] held = new int[64];
    private final int[] taps = new int[64];           // frames left of a tapped key
    // Presses and releases in the order they came, taken in one at a time:
    // each one lasts 2 frames at least before the next change, so that fast
    // typing (or a slow device) does not give the QL keys at once.
    private final ArrayDeque<int[]> keyEvents = new ArrayDeque<>();
    private int keyHold;                               // frames before the next change

    private Thread thread;
    private volatile boolean running;
    private volatile boolean paused;       // by the user (menu)
    private volatile boolean suspended;    // the app is in the background
    private volatile boolean turbo;
    private volatile boolean sound = true;
    private AudioTrack track;
    private float fps;

    Emulator(Screen screen) {
        this.screen = screen;
    }

    // --- life cycle -------------------------------------------------------
    synchronized void start() {
        if (thread != null) return;
        running = true;
        thread = new Thread(this, "QL");
        thread.setPriority(Thread.MAX_PRIORITY);
        thread.start();
    }

    void stop() {
        Thread t;
        synchronized (this) {
            running = false;
            notifyAll();
            t = thread;
            thread = null;
        }
        if (t != null) {
            try {
                t.join(2000);
            } catch (InterruptedException ignored) {
            }
        }
    }

    synchronized void setSuspended(boolean s) {
        suspended = s;
        notifyAll();
    }

    synchronized void setPaused(boolean p) {
        paused = p;
        notifyAll();
    }

    boolean isPaused() { return paused; }

    void setTurbo(boolean t) { turbo = t; }

    boolean isTurbo() { return turbo; }

    synchronized void setSound(boolean s) {
        sound = s;
    }

    float fps() { return fps; }

    /** Runs r on the emulation thread before the next frame (even when paused). */
    synchronized void post(Runnable r) {
        commands.add(r);
        notifyAll();
    }

    // --- keys --------------------------------------------------------------
    synchronized void keyDown(int code) {
        if (code >= 0 && code < 64) keyEvents.add(new int[] {code, 1});
    }

    synchronized void keyUp(int code) {
        if (code >= 0 && code < 64) keyEvents.add(new int[] {code, 0});
    }

    synchronized void releaseAll() {
        java.util.Arrays.fill(held, 0);
        keyEvents.clear();
        keyHold = 0;
    }

    /** Presses keys together for a few frames (menu shortcuts: F1, CTRL+SPACE...). */
    synchronized void tap(int... codes) {
        for (int c : codes) if (c >= 0 && c < 64) taps[c] = 6;
    }

    private synchronized void applyKeys() {
        if (keyHold > 0) keyHold--;
        while (keyHold == 0 && !keyEvents.isEmpty()) {
            int[] e = keyEvents.poll();
            if (e[1] == 1) {
                held[e[0]]++;
                keyHold = 2;
            } else if (held[e[0]] > 0) {
                held[e[0]]--;
                keyHold = 2;           // (seen up too: the same key twice is two presses)
            }
        }
        QLNative.keysClear();
        for (int c = 0; c < 64; c++) {
            if (held[c] > 0 || taps[c] > 0) QLNative.key(c, true);
            if (taps[c] > 0) taps[c]--;
        }
    }

    // --- the loop ----------------------------------------------------------
    private boolean waitWhileStopped() {
        synchronized (this) {
            while (running && (paused || suspended) && commands.isEmpty()) {
                if (track != null) track.pause();
                try {
                    wait();
                } catch (InterruptedException e) {
                    return false;
                }
            }
            return running;
        }
    }

    private void runCommands() {
        while (true) {
            Runnable r;
            synchronized (this) {
                r = commands.poll();
            }
            if (r == null) return;
            r.run();
        }
    }

    private AudioTrack openAudio() {
        int min = AudioTrack.getMinBufferSize(QLNative.AUDIO_RATE, AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
        // ~4 frames of buffer: the writes block when it is full, which paces the frames
        int size = Math.max(min, QLNative.AUDIO_MAX * 2 * 4);
        AudioTrack t = new AudioTrack(AudioManager.STREAM_MUSIC, QLNative.AUDIO_RATE,
                AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT, size, AudioTrack.MODE_STREAM);
        if (t.getState() != AudioTrack.STATE_INITIALIZED) {
            t.release();
            return null;
        }
        return t;
    }

    @Override
    public void run() {
        track = openAudio();
        final long frameNs = (long) (1e9 / QLNative.FPS);
        long next = System.nanoTime();
        long fpsStart = SystemClock.elapsedRealtime();
        int fpsFrames = 0, turboSkip = 0;
        try {
            while (running) {
                runCommands();
                if (paused || suspended) {
                    if (!waitWhileStopped()) break;
                    next = System.nanoTime();
                    continue;
                }
                applyKeys();
                // In turbo only one frame in 8 is drawn (and none is heard)
                boolean draw = !turbo || (turboSkip++ & 7) == 0;
                int n = QLNative.runFrame(draw ? pixels : null, audio);
                if (draw) screen.showFrame(pixels, fps, QLNative.cartMotor(0), QLNative.cartMotor(1));

                boolean audible = sound && !turbo && track != null;
                if (audible) {
                    if (track.getPlayState() != AudioTrack.PLAYSTATE_PLAYING) track.play();
                    track.write(audio, 0, n);           // blocks when the buffer is full
                    next = System.nanoTime();
                } else {
                    if (track != null && track.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
                        track.pause();
                        track.flush();
                    }
                    if (!turbo) {
                        next += frameNs;
                        long wait = next - System.nanoTime();
                        if (wait > 0) {
                            try {
                                Thread.sleep(wait / 1000000, (int) (wait % 1000000));
                            } catch (InterruptedException ignored) {
                            }
                        } else if (wait < -frameNs * 5) {
                            next = System.nanoTime();   // fell behind: do not race to catch up
                        }
                    }
                }
                fpsFrames++;
                long now = SystemClock.elapsedRealtime();
                if (now - fpsStart >= 1000) {
                    fps = fpsFrames * 1000f / (now - fpsStart);
                    fpsFrames = 0;
                    fpsStart = now;
                }
            }
        } finally {
            if (track != null) {
                track.release();
                track = null;
            }
        }
    }
}
