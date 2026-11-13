package com.chusogar.cmesys.ql;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LightingColorFilter;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.io.InputStream;

/** The on-screen QL keyboard, the same as dsp-cpp's (src/machine/virtual_keyboard
 *  and the QL driver): the photo of the QL (assets/ql_keyboard.jpg, dsp-cpp's
 *  tools/ql_keyboard.jpg as it is) with its red backdrop keyed out, the key
 *  tiles measured on it (tools/gen_ql_vkb.py) and a key that is down sinking
 *  into the picture. Here only the keyboard part of the machine is shown (not
 *  the microdrives), several fingers can press keys at once, and SHIFT / CTRL /
 *  ALT stay down for the next key when tapped (twice: locked until tapped
 *  again). MENÚ and ▼ (hide) sit in the band above the keys. */
final class QLKeyboardView extends View {
    interface Listener {
        void onQLKey(int code, boolean down);
        void onKeyboardAction(int action);   // ACTION_*
    }

    static final int ACTION_MENU = -2, ACTION_HIDE = -3;

    // The part of the photo shown, as dsp-cpp's crop (kQlVkbCropX / Y) up to
    // the groove between the keyboard and the microdrives
    private static final int CROP_X = 30, CROP_Y = 24, PIC_W = 1268, PIC_H = 534;
    private static final int TRAVEL = 3;          // pixels a key that is down moves (set_travel(3))
    private static final int SLACK = 4;           // the grooves between tiles count as the nearest key

    // code (IPC matrix row * 8 + bit, as QLKeys), flags (1 modifier), the tile
    // in the shown picture: dsp-cpp's kQlVkbKeys (ql_vkb_data.inc)
    private static final int[][] KEYS = {
            {1, 0, 45, 117, 109, 184},   // F1
            {3, 0, 45, 190, 109, 257},   // F2
            {4, 0, 45, 263, 109, 330},   // F3
            {0, 0, 45, 336, 109, 403},   // F4
            {5, 0, 45, 409, 109, 476},   // F5
            {11, 0, 141, 117, 206, 184},   // ESC
            {35, 0, 212, 117, 278, 184},   // 1
            {49, 0, 284, 117, 352, 184},   // 2
            {33, 0, 358, 117, 425, 184},   // 3
            {6, 0, 431, 117, 500, 184},   // 4
            {2, 0, 506, 117, 574, 184},   // 5
            {50, 0, 580, 117, 649, 184},   // 6
            {7, 0, 655, 117, 724, 184},   // 7
            {48, 0, 730, 117, 796, 184},   // 8
            {40, 0, 802, 117, 870, 184},   // 9
            {53, 0, 876, 117, 943, 184},   // 0
            {45, 0, 949, 117, 1017, 184},   // -
            {29, 0, 1023, 117, 1091, 184},   // =
            {21, 0, 1097, 117, 1165, 184},   // £
            {13, 0, 1171, 117, 1240, 184},   // backslash
            {43, 0, 141, 190, 241, 257},   // TABULATE
            {51, 0, 247, 190, 314, 257},   // Q
            {41, 0, 320, 190, 388, 257},   // W
            {52, 0, 394, 190, 463, 257},   // E
            {44, 0, 469, 190, 537, 257},   // R
            {54, 0, 543, 190, 612, 257},   // T
            {46, 0, 618, 190, 687, 257},   // Y
            {55, 0, 693, 190, 760, 257},   // U
            {42, 0, 766, 190, 834, 257},   // I
            {47, 0, 840, 190, 906, 257},   // O
            {37, 0, 912, 190, 980, 257},   // P
            {24, 0, 986, 190, 1053, 257},   // [
            {16, 0, 1059, 190, 1127, 257},   // ]
            {25, 0, 141, 263, 260, 330},   // CAPS LOCK
            {36, 0, 266, 263, 333, 330},   // A
            {27, 0, 339, 263, 407, 330},   // S
            {38, 0, 413, 263, 481, 330},   // D
            {28, 0, 487, 263, 555, 330},   // F
            {30, 0, 561, 263, 630, 330},   // G
            {34, 0, 636, 263, 705, 330},   // H
            {39, 0, 711, 263, 779, 330},   // J
            {26, 0, 785, 263, 851, 330},   // K
            {32, 0, 857, 263, 924, 330},   // L
            {31, 0, 930, 263, 997, 330},   // ;
            {23, 0, 1003, 263, 1070, 330},   // '
            {8, 0, 1133, 190, 1201, 257},   // ENTER (upper)
            {8, 0, 1076, 263, 1201, 330},   // ENTER
            {56, 1, 141, 336, 296, 403},   // SHIFT (left)
            {17, 0, 302, 336, 369, 403},   // Z
            {59, 0, 375, 336, 444, 403},   // X
            {19, 0, 450, 336, 517, 403},   // C
            {60, 0, 523, 336, 593, 403},   // V
            {20, 0, 599, 336, 667, 403},   // B
            {62, 0, 673, 336, 741, 403},   // N
            {22, 0, 747, 336, 814, 403},   // M
            {63, 0, 820, 336, 888, 403},   // ,
            {18, 0, 894, 336, 961, 403},   // .
            {61, 0, 967, 336, 1034, 403},   // /
            {56, 1, 1040, 336, 1201, 403},   // SHIFT (right)
            {57, 1, 141, 409, 261, 476},   // CTRL
            {9, 0, 267, 409, 332, 476},   // LEFT
            {12, 0, 338, 409, 405, 476},   // RIGHT
            {14, 0, 411, 409, 923, 476},   // SPACE
            {10, 0, 929, 409, 996, 476},   // UP
            {15, 0, 1002, 409, 1071, 476},   // DOWN
            {58, 1, 1077, 409, 1200, 476},   // ALT
            {ACTION_MENU, 0, 980, 34, 1108, 98},
            {ACTION_HIDE, 0, 1120, 34, 1240, 98},
    };

    private final int[] fingers = new int[KEYS.length];
    private final int[] pointerKey = new int[16];
    private final Paint pic = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint sunk = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint locked = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint button = new Paint(Paint.ANTI_ALIAS_FLAG), buttonEdge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect src = new Rect();
    private final RectF dst = new RectF();
    private final float density;
    private Bitmap photo;
    private float scale = 1, left;
    private Listener listener;
    private boolean haptic = true;
    // Sticky modifiers: 0 = off, 1 = for the next key, 2 = locked
    private final int[] sticky = new int[3];
    private static final int[] MODS = {QLKeys.SHIFT, QLKeys.CTRL, QLKeys.ALT};
    private final long[] lastTap = new long[3];
    private final boolean[] modsSent = new boolean[3];

    QLKeyboardView(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        java.util.Arrays.fill(pointerKey, -1);
        // A key that is down gets darker (13/16, as dsp-cpp); a locked
        // modifier also redder
        sunk.setColorFilter(new LightingColorFilter(0xffd0d0d0, 0));
        locked.setColorFilter(new LightingColorFilter(0xffe08070, 0));
        button.setColor(Color.argb(170, 52, 52, 58));
        buttonEdge.setColor(Color.argb(200, 120, 120, 128));
        buttonEdge.setStyle(Paint.Style.STROKE);
        buttonEdge.setStrokeWidth(density);
        label.setColor(Color.rgb(225, 225, 225));
        label.setTextAlign(Paint.Align.CENTER);
        label.setFakeBoldText(true);
        photo = loadPhoto(context);
    }

    /** The photo, cropped, with the red backdrop keyed out to transparent
     *  (the same test as dsp-cpp: how much redder than green and blue). */
    private static Bitmap loadPhoto(Context context) {
        Bitmap full;
        try (InputStream in = context.getAssets().open("ql_keyboard.jpg")) {
            BitmapFactory.Options o = new BitmapFactory.Options();
            o.inScaled = false;
            o.inPreferredConfig = Bitmap.Config.ARGB_8888;
            full = BitmapFactory.decodeStream(in, null, o);
        } catch (java.io.IOException e) {
            full = null;
        }
        int[] px = new int[PIC_W * PIC_H];
        if (full != null && full.getWidth() >= CROP_X + PIC_W && full.getHeight() >= CROP_Y + PIC_H) {
            full.getPixels(px, 0, PIC_W, CROP_X, CROP_Y, PIC_W, PIC_H);
            full.recycle();
            for (int i = 0; i < px.length; i++) {
                int p = px[i], r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
                int excess = r - Math.max(g, b);
                if (excess > 60) {
                    int a = Math.max(0, 255 - (excess - 60) * 255 / 60);
                    r = Math.max(g, b);
                    px[i] = a << 24 | r << 16 | g << 8 | b;
                }
            }
        } else {
            java.util.Arrays.fill(px, 0xff404040);
        }
        Bitmap bm = Bitmap.createBitmap(PIC_W, PIC_H, Bitmap.Config.ARGB_8888);
        bm.setPixels(px, 0, PIC_W, 0, 0, PIC_W, PIC_H);
        return bm;
    }

    void setListener(Listener l) { listener = l; }

    void setHaptic(boolean h) { haptic = h; }

    @Override
    protected void onMeasure(int wSpec, int hSpec) {
        int w = MeasureSpec.getSize(wSpec);
        // The whole width, but no more than about half the screen's height
        // (landscape): then narrower, in the middle
        int h = w * PIC_H / PIC_W;
        int screenH = getResources().getDisplayMetrics().heightPixels;
        h = Math.min(h, Math.max((int) (screenH * 0.48f), (int) (150 * density)));
        setMeasuredDimension(w, h);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        scale = Math.min(w / (float) PIC_W, h / (float) PIC_H);
        left = (w - PIC_W * scale) / 2;
        label.setTextSize(30 * scale);
    }

    private void map(int x1, int y1, int x2, int y2, RectF out) {
        out.set(left + x1 * scale, y1 * scale, left + x2 * scale, y2 * scale);
    }

    private int modIndex(int code) {
        for (int i = 0; i < 3; i++) if (MODS[i] == code) return i;
        return -1;
    }

    /** Down: held by a finger, its modifier latched, or the same key as one
     *  held (the QL's ENTER is two tiles, and there are two SHIFTs). */
    private int state(int i) {
        int code = KEYS[i][0];
        int m = modIndex(code);
        if (m >= 0 && sticky[m] == 2) return 2;
        if (m >= 0 && sticky[m] == 1) return 1;
        for (int j = 0; j < KEYS.length; j++) if (KEYS[j][0] == code && fingers[j] > 0) return 1;
        return 0;
    }

    @Override
    protected void onDraw(Canvas c) {
        c.drawColor(Color.argb(200, 8, 8, 12));
        map(0, 0, PIC_W, PIC_H, dst);
        c.drawBitmap(photo, null, dst, pic);
        for (int i = 0; i < KEYS.length; i++) {
            int[] k = KEYS[i];
            if (k[0] < 0) {
                map(k[2], k[3], k[4], k[5], dst);
                float r = 8 * scale;
                if (fingers[i] > 0) dst.offset(0, TRAVEL * scale);
                c.drawRoundRect(dst, r, r, button);
                c.drawRoundRect(dst, r, r, buttonEdge);
                c.drawText(k[0] == ACTION_MENU ? "MENÚ" : "▼", dst.centerX(),
                        dst.centerY() - (label.descent() + label.ascent()) / 2, label);
                continue;
            }
            int s = state(i);
            if (s == 0) continue;
            // The cap moves down: each row shows the row TRAVEL above it
            src.set(k[2], Math.max(0, k[3] - TRAVEL), k[4], k[5] - TRAVEL);
            map(k[2], k[5] - (src.bottom - src.top), k[4], k[5], dst);
            c.drawBitmap(photo, src, dst, s == 2 ? locked : sunk);
        }
    }

    private int keyAt(float vx, float vy) {
        float x = (vx - left) / scale, y = vy / scale;
        int best = -1;
        float bestD = SLACK + 1;
        for (int i = 0; i < KEYS.length; i++) {
            int[] k = KEYS[i];
            float dx = Math.max(Math.max(k[2] - x, x - k[4]), 0);
            float dy = Math.max(Math.max(k[3] - y, y - k[5]), 0);
            float d = Math.max(dx, dy);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        return bestD <= SLACK ? best : -1;
    }

    private void press(int i) {
        fingers[i]++;
        if (fingers[i] > 1) return;
        if (haptic) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        int code = KEYS[i][0];
        if (code < 0) return;          // MENÚ / ▼: on release
        int m = modIndex(code);
        if (m >= 0) {
            // tap: for the next key; tap again soon: locked; tap when locked: off
            long now = System.currentTimeMillis();
            if (sticky[m] == 2) sticky[m] = 0;
            else if (sticky[m] == 1 && now - lastTap[m] < 400) sticky[m] = 2;
            else if (sticky[m] == 1) sticky[m] = 0;
            else sticky[m] = 1;
            lastTap[m] = now;
            applyMods();
            return;
        }
        if (listener != null) listener.onQLKey(code, true);
    }

    private void applyMods() {
        if (listener == null) return;
        for (int i = 0; i < 3; i++) {
            boolean down = sticky[i] > 0;
            if (down != modsSent[i]) {
                modsSent[i] = down;
                listener.onQLKey(MODS[i], down);
            }
        }
    }

    private void release(int i, boolean act) {
        if (fingers[i] == 0) return;
        fingers[i]--;
        int code = KEYS[i][0];
        if (fingers[i] > 0 || modIndex(code) >= 0) return;
        if (code < 0) {
            if (act && listener != null) listener.onKeyboardAction(code);
            return;
        }
        if (listener != null) listener.onQLKey(code, false);
        // a one-shot SHIFT / CTRL / ALT ends with the key it was for (after a
        // frame or two, so that the QL sees them together)
        boolean changed = false;
        for (int m = 0; m < 3; m++) if (sticky[m] == 1) {
            sticky[m] = 0;
            changed = true;
        }
        if (changed) postDelayed(new Runnable() {
            @Override public void run() { applyMods(); invalidate(); }
        }, 60);
    }

    /** Lets go of everything (the keyboard is hidden, the app paused). */
    void releaseAll() {
        for (int p = 0; p < pointerKey.length; p++) {
            int i = pointerKey[p];
            pointerKey[p] = -1;
            if (i >= 0) {
                fingers[i] = 1;
                release(i, false);
            }
        }
        java.util.Arrays.fill(fingers, 0);
        for (int m = 0; m < 3; m++) sticky[m] = 0;
        applyMods();
        invalidate();
    }

    private boolean slides(int i) {
        return i >= 0 && KEYS[i][0] >= 0 && modIndex(KEYS[i][0]) < 0;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int idx = e.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int id = e.getPointerId(idx);
                if (id >= pointerKey.length) break;
                int k = keyAt(e.getX(idx), e.getY(idx));
                pointerKey[id] = k;
                if (k >= 0) press(k);
                break;
            }
            case MotionEvent.ACTION_MOVE:
                // a finger sliding to another key (not for the modifiers and buttons)
                for (int p = 0; p < e.getPointerCount(); p++) {
                    int id = e.getPointerId(p);
                    if (id >= pointerKey.length) continue;
                    int old = pointerKey[id];
                    if (old >= 0 && !slides(old)) continue;
                    int k = keyAt(e.getX(p), e.getY(p));
                    if (k != old && slides(k) && (old < 0 || KEYS[k][0] != KEYS[old][0])) {
                        if (old >= 0) release(old, false);
                        pointerKey[id] = k;
                        press(k);
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                if (action == MotionEvent.ACTION_CANCEL) {
                    releaseAll();
                    break;
                }
                int id = e.getPointerId(idx);
                if (id >= pointerKey.length) break;
                int k = pointerKey[id];
                pointerKey[id] = -1;
                if (k >= 0) release(k, true);
                break;
            }
            default:
                break;
        }
        invalidate();
        return true;
    }
}
