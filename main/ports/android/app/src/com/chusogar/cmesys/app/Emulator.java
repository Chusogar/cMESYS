package com.chusogar.cmesys.app;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Process;
import android.os.SystemClock;

import java.util.ArrayDeque;

/** The emulation thread: runs the machine frame by frame (paced by the
 *  sound when it is on), sends the picture to the screen and the samples to
 *  an AudioTrack, and carries out the commands of the interface (they run
 *  here, between frames: the cores are not thread safe). */
final class Emulator implements Runnable {
    interface Screen {
        /** A frame's picture (w x h ARGB), on the emulation thread. */
        void showFrame(int[] pixels, int w, int h, float fps, int status);
    }

    static final int KEYS = 128;

    private final Screen screen;
    private int[] pixels = new int[0];
    private final short[] audio = new short[Core.AUDIO_MAX];
    private final ArrayDeque<Runnable> commands = new ArrayDeque<>();
    // Keys held: by source (physical keyboard / pad, on-screen keyboard, taps
    // from the menu), as counts so that two sources can hold the same key.
    private final int[] held = new int[KEYS];
    private final int[] taps = new int[KEYS];          // frames left of a tapped key
    // Presses and releases in the order they came, taken in one at a time:
    // each one lasts 2 frames at least before the next change, so that fast
    // typing (or a slow device) does not give the machine keys at once.
    private final ArrayDeque<int[]> keyEvents = new ArrayDeque<>();
    private int keyHold;                               // frames before the next change
    private final int[] joy = new int[3];              // ports 1, 2
    private final int[] joySent = new int[3];

    private Thread thread;
    private volatile boolean running;
    private volatile boolean paused;       // by the user (menu)
    private volatile boolean suspended;    // the app is in the background
    private volatile boolean turbo;
    private volatile boolean sound = true;
    private volatile boolean started;      // Core.init done
    private AudioTrack track;
    private int trackRate;
    private float fps;
    private volatile int status;

    Emulator(Screen screen) {
        this.screen = screen;
    }

    // --- life cycle -------------------------------------------------------
    synchronized void start() {
        if (thread != null) return;
        running = true;
        thread = new Thread(this, "machine");
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

    void setStarted(boolean s) { started = s; }

    synchronized void setSuspended(boolean s) {
        suspended = s;
        notifyAll();
    }

    synchronized void setPaused(boolean p) {
        paused = p;
        notifyAll();
    }

    boolean isPaused() { return paused; }

    void setTurbo(final boolean t) {
        turbo = t;
        post(new Runnable() {
            @Override public void run() { if (started) Core.turbo(t); }
        });
    }

    boolean isTurbo() { return turbo; }

    void setSound(boolean s) { sound = s; }

    float fps() { return fps; }

    int status() { return status; }

    /** Runs r on the emulation thread before the next frame (even when paused). */
    synchronized void post(Runnable r) {
        commands.add(r);
        notifyAll();
    }

    // --- keys and joysticks ---------------------------------------------------
    synchronized void keyDown(int code) {
        if (code >= 0 && code < KEYS) keyEvents.add(new int[] {code, 1});
    }

    synchronized void keyUp(int code) {
        if (code >= 0 && code < KEYS) keyEvents.add(new int[] {code, 0});
    }

    /** Types: each chord (keys held together) pressed and let go in turn. */
    synchronized void type(int[][] chords) {
        for (int[] chord : chords) {
            if (chord.length == 1 && chord[0] < 0) {           // Machine.TYPE_PAUSE: ~2 s
                keyEvents.add(new int[] {-1, 100});
                continue;
            }
            for (int c : chord) keyDown(c);
            for (int i = chord.length - 1; i >= 0; i--) keyUp(chord[i]);
        }
    }

    synchronized void joy(int port, int bits, boolean down) {
        if (port < 1 || port > 2) return;
        if (down) joy[port] |= bits;
        else joy[port] &= ~bits;
    }

    synchronized void releaseAll() {
        java.util.Arrays.fill(held, 0);
        keyEvents.clear();
        keyHold = 0;
        joy[1] = joy[2] = 0;
    }

    /** Presses keys together for a few frames (menu shortcuts). */
    synchronized void tap(int... codes) {
        for (int c : codes) if (c >= 0 && c < KEYS) taps[c] = 6;
    }

    private synchronized void applyKeys() {
        if (keyHold > 0) keyHold--;
        while (keyHold == 0 && !keyEvents.isEmpty()) {
            int[] e = keyEvents.poll();
            if (e[0] < 0) {
                keyHold = e[1];        // a pause in typing
            } else if (e[1] == 1) {
                held[e[0]]++;
                keyHold = 2;
            } else if (held[e[0]] > 0) {
                held[e[0]]--;
                keyHold = 2;           // (seen up too: the same key twice is two presses)
            }
        }
        Core.keysClear();
        for (int c = 0; c < KEYS; c++) {
            if (held[c] > 0 || taps[c] > 0) Core.key(c, true);
            if (taps[c] > 0) taps[c]--;
        }
        for (int p = 1; p <= 2; p++) {
            if (joy[p] != joySent[p]) {
                joySent[p] = joy[p];
                Core.joy(p, joy[p]);
            }
        }
    }

    // --- the loop ----------------------------------------------------------
    private boolean waitWhileStopped() {
        synchronized (this) {
            while (running && (paused || suspended || !started) && commands.isEmpty()) {
                stopAudio();
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

    // Sound: about 6 frames of buffer (at least twice the device's minimum);
    // the writes block when it is full, which paces the frames. Playing
    // starts once 3 frames are queued, so that it does not run dry at once
    // (at the start and after each pause).
    private static final int BUFFER_FRAMES = 6, PRIME_FRAMES = 3;
    private int primed;

    private void openAudio(int rate, double frameRate) {
        if (track != null && trackRate == rate) return;
        if (track != null) track.release();
        track = null;
        trackRate = rate;
        primed = 0;
        int min = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        int perFrame = (int) (rate / Math.max(10.0, frameRate));
        int size = Math.max(Math.max(min, 0) * 2, perFrame * BUFFER_FRAMES * 2) & ~3;     // (whole samples)
        AudioTrack t;
        try {
            t = new AudioTrack(
                    new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build(),
                    new AudioFormat.Builder().setSampleRate(rate).setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build(),
                    size, AudioTrack.MODE_STREAM, AudioManager.AUDIO_SESSION_ID_GENERATE);
        } catch (IllegalArgumentException e) {
            return;
        }
        if (t.getState() != AudioTrack.STATE_INITIALIZED) {
            t.release();
            return;
        }
        track = t;
    }

    private void stopAudio() {
        if (track != null && track.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
            track.pause();
            track.flush();
        }
        primed = 0;
    }

    @Override
    public void run() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
        long next = System.nanoTime();
        long fpsStart = SystemClock.elapsedRealtime();
        int fpsFrames = 0, turboSkip = 0;
        try {
            while (running) {
                runCommands();
                if (paused || suspended || !started) {
                    if (!waitWhileStopped()) break;
                    next = System.nanoTime();
                    continue;
                }
                final double rate = Core.fps();
                final long frameNs = (long) (1e9 / rate);
                openAudio(Core.audioRate(), rate);
                applyKeys();
                int w = Core.width(), h = Core.height();
                if (pixels.length != w * h) pixels = new int[w * h];
                // In turbo only one frame in 8 is drawn (and none is heard)
                boolean draw = !turbo || (turboSkip++ & 7) == 0;
                int n = Core.runFrame(draw ? pixels : null, audio);
                status = Core.status();
                if (draw) screen.showFrame(pixels, w, h, fps, status);

                boolean audible = sound && !turbo && track != null && n > 0;
                if (audible) {
                    track.write(audio, 0, n);           // blocks when the buffer is full
                    if (primed < PRIME_FRAMES) {
                        if (++primed == PRIME_FRAMES) track.play();
                    } else if (track.getPlayState() != AudioTrack.PLAYSTATE_PLAYING) {
                        track.play();
                    }
                    next = System.nanoTime();
                } else {
                    stopAudio();
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
