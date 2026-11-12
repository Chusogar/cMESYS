package com.chusogar.cmesys.ql;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/** The on-screen QL keyboard (as Hataroid's virtual ST keyboard): the QL's
 *  keys in its layout, several fingers at once, and SHIFT / CTRL / ALT that
 *  stay down for the next key when tapped (twice: locked until tapped again). */
final class QLKeyboardView extends View {
    interface Listener {
        void onQLKey(int code, boolean down);
        void onKeyboardAction(int action);   // ACTION_*
    }

    static final int ACTION_MENU = -2, ACTION_HIDE = -3;

    private static final class Key {
        final String label, shifted;
        final int code;          // QL key, or ACTION_*
        final float units;       // width in 1/15 of the keyboard (0: a gap)
        final RectF rect = new RectF();
        int fingers;             // pointers on it

        Key(String label, String shifted, int code, float units) {
            this.label = label;
            this.shifted = shifted;
            this.code = code;
            this.units = units;
        }
    }

    private final List<List<Key>> rows = new ArrayList<>();
    private final List<Key> keys = new ArrayList<>();
    private final Key[] pointerKey = new Key[16];
    private final Paint keyPaint = new Paint(Paint.ANTI_ALIAS_FLAG), downPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint latchPaint = new Paint(Paint.ANTI_ALIAS_FLAG), lockPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fnPaint = new Paint(Paint.ANTI_ALIAS_FLAG), edgePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG), small = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float density;
    private Listener listener;
    private boolean haptic = true;
    // Sticky modifiers: 0 = off, 1 = for the next key, 2 = locked
    private final int[] sticky = new int[3];
    private static final int[] MODS = {QLKeys.SHIFT, QLKeys.CTRL, QLKeys.ALT};
    private final long[] lastTap = new long[3];

    QLKeyboardView(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        keyPaint.setColor(Color.rgb(34, 34, 38));
        fnPaint.setColor(Color.rgb(58, 52, 40));
        downPaint.setColor(Color.rgb(80, 120, 185));
        latchPaint.setColor(Color.rgb(170, 70, 40));
        lockPaint.setColor(Color.rgb(200, 40, 30));
        edgePaint.setColor(Color.rgb(90, 90, 96));
        edgePaint.setStyle(Paint.Style.STROKE);
        edgePaint.setStrokeWidth(density);
        label.setColor(Color.WHITE);
        label.setTextAlign(Paint.Align.CENTER);
        small.setColor(Color.rgb(190, 190, 190));
        build();
    }

    void setListener(Listener l) { listener = l; }

    void setHaptic(boolean h) { haptic = h; }

    private List<Key> row(Object... spec) {
        List<Key> r = new ArrayList<>();
        for (int i = 0; i < spec.length; i += 4) {
            Key k = new Key((String) spec[i], (String) spec[i + 1], (Integer) spec[i + 2], ((Number) spec[i + 3]).floatValue());
            r.add(k);
            if (k.units > 0 && k.code != -1) keys.add(k);
        }
        rows.add(r);
        return r;
    }

    private void build() {
        row("F1", null, QLKeys.F1, 2, "F2", null, QLKeys.F2, 2, "F3", null, QLKeys.F3, 2, "F4", null, QLKeys.F4, 2,
            "F5", null, QLKeys.F5, 2, "", null, -1, 1, "MENÚ", null, ACTION_MENU, 2, "▼", null, ACTION_HIDE, 2);
        row("ESC", null, QLKeys.ESC, 1, "1", "!", QLKeys.N1, 1, "2", "@", QLKeys.N2, 1, "3", "#", QLKeys.N3, 1,
            "4", "$", QLKeys.N4, 1, "5", "%", QLKeys.N5, 1, "6", "^", QLKeys.N6, 1, "7", "&", QLKeys.N7, 1,
            "8", "*", QLKeys.N8, 1, "9", "(", QLKeys.N9, 1, "0", ")", QLKeys.N0, 1, "-", "_", QLKeys.MINUS, 1,
            "=", "+", QLKeys.EQUALS, 1, "£", "~", QLKeys.POUND, 1, "\\", "|", QLKeys.BACKSLASH, 1);
        row("TAB", null, QLKeys.TAB, 1.5, "Q", null, QLKeys.Q, 1, "W", null, QLKeys.W, 1, "E", null, QLKeys.E, 1,
            "R", null, QLKeys.R, 1, "T", null, QLKeys.T, 1, "Y", null, QLKeys.Y, 1, "U", null, QLKeys.U, 1,
            "I", null, QLKeys.I, 1, "O", null, QLKeys.O, 1, "P", null, QLKeys.P, 1, "[", "{", QLKeys.LBRACKET, 1,
            "]", "}", QLKeys.RBRACKET, 1, "", null, -1, 1.5);
        row("CAPS", null, QLKeys.CAPS, 1.75, "A", null, QLKeys.A, 1, "S", null, QLKeys.S, 1, "D", null, QLKeys.D, 1,
            "F", null, QLKeys.F, 1, "G", null, QLKeys.G, 1, "H", null, QLKeys.H, 1, "J", null, QLKeys.J, 1,
            "K", null, QLKeys.K, 1, "L", null, QLKeys.L, 1, ";", ":", QLKeys.SEMICOLON, 1, "'", "\"", QLKeys.QUOTE, 1,
            "ENTER", null, QLKeys.ENTER, 2.25);
        row("SHIFT", null, QLKeys.SHIFT, 2.25, "Z", null, QLKeys.Z, 1, "X", null, QLKeys.X, 1, "C", null, QLKeys.C, 1,
            "V", null, QLKeys.V, 1, "B", null, QLKeys.B, 1, "N", null, QLKeys.N, 1, "M", null, QLKeys.M, 1,
            ",", "<", QLKeys.COMMA, 1, ".", ">", QLKeys.PERIOD, 1, "/", "?", QLKeys.SLASH, 1, "SHIFT", null, QLKeys.SHIFT, 2.75);
        row("CTRL", null, QLKeys.CTRL, 1.5, "ALT", null, QLKeys.ALT, 1.5, "←", null, QLKeys.LEFT, 1,
            "", null, QLKeys.SPACE, 7, "→", null, QLKeys.RIGHT, 1, "↑", null, QLKeys.UP, 1, "↓", null, QLKeys.DOWN, 1,
            "ALT", null, QLKeys.ALT, 1);
    }

    private float keyHeight(int width) {
        return Math.min(width / 15f * 0.9f, 46 * density);
    }

    @Override
    protected void onMeasure(int wSpec, int hSpec) {
        int w = MeasureSpec.getSize(wSpec);
        setMeasuredDimension(w, (int) (keyHeight(w) * rows.size() + 6 * density));
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        float unit = w / 15f, kh = keyHeight(w), gap = 2 * density;
        float y = 3 * density;
        for (List<Key> r : rows) {
            float x = 0;
            for (Key k : r) {
                k.rect.set(x + gap, y + gap, x + k.units * unit - gap, y + kh - gap);
                x += k.units * unit;
            }
            y += kh;
        }
        label.setTextSize(Math.min(kh * 0.38f, 16 * density));
        small.setTextSize(Math.min(kh * 0.26f, 11 * density));
    }

    private int modIndex(int code) {
        for (int i = 0; i < 3; i++) if (MODS[i] == code) return i;
        return -1;
    }

    @Override
    protected void onDraw(Canvas c) {
        c.drawColor(Color.argb(200, 8, 8, 12));
        float r = 4 * density;
        for (Key k : keys) {
            int m = modIndex(k.code);
            Paint p = k.fingers > 0 ? downPaint
                    : m >= 0 && sticky[m] == 2 ? lockPaint
                    : m >= 0 && sticky[m] == 1 ? latchPaint
                    : (k.code < 0 || k.code == QLKeys.F1 || k.code == QLKeys.F2 || k.code == QLKeys.F3
                       || k.code == QLKeys.F4 || k.code == QLKeys.F5) ? fnPaint : keyPaint;
            c.drawRoundRect(k.rect, r, r, p);
            c.drawRoundRect(k.rect, r, r, edgePaint);
            float cy = k.rect.centerY() - (label.descent() + label.ascent()) / 2;
            if (k.shifted != null) {
                c.drawText(k.shifted, k.rect.left + 4 * density, k.rect.top + small.getTextSize() + 2 * density, small);
                c.drawText(k.label, k.rect.centerX() + 3 * density, cy + 3 * density, label);
            } else {
                c.drawText(k.label, k.rect.centerX(), cy, label);
            }
        }
    }

    private Key keyAt(float x, float y) {
        for (Key k : keys) if (k.rect.contains(x, y)) return k;
        return null;
    }

    private void press(Key k) {
        k.fingers++;
        if (k.fingers > 1) return;
        if (haptic) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        if (k.code < 0) {
            if (listener != null) listener.onKeyboardAction(k.code);
            return;
        }
        int m = modIndex(k.code);
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
        if (listener != null) listener.onQLKey(k.code, true);
    }

    private final boolean[] modsSent = new boolean[3];

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

    private void release(Key k) {
        if (k.fingers == 0) return;
        k.fingers--;
        if (k.fingers > 0 || k.code < 0 || modIndex(k.code) >= 0) return;
        if (listener != null) listener.onQLKey(k.code, false);
        // a one-shot SHIFT / CTRL / ALT ends with the key it was for (after a
        // frame or two, so that the QL sees them together)
        boolean changed = false;
        for (int i = 0; i < 3; i++) if (sticky[i] == 1) {
            sticky[i] = 0;
            changed = true;
        }
        if (changed) postDelayed(new Runnable() {
            @Override public void run() { applyMods(); invalidate(); }
        }, 60);
    }

    /** Lets go of everything (the keyboard is hidden, the app paused). */
    void releaseAll() {
        for (int i = 0; i < pointerKey.length; i++) {
            if (pointerKey[i] != null) {
                Key k = pointerKey[i];
                pointerKey[i] = null;
                k.fingers = 1;
                release(k);
            }
        }
        for (int i = 0; i < 3; i++) sticky[i] = 0;
        applyMods();
        invalidate();
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
                Key k = keyAt(e.getX(idx), e.getY(idx));
                pointerKey[id] = k;
                if (k != null) press(k);
                break;
            }
            case MotionEvent.ACTION_MOVE:
                // a finger sliding to another key (not for the modifiers and actions)
                for (int i = 0; i < e.getPointerCount(); i++) {
                    int id = e.getPointerId(i);
                    if (id >= pointerKey.length) continue;
                    Key old = pointerKey[id];
                    if (old != null && (old.code < 0 || modIndex(old.code) >= 0)) continue;
                    Key k = keyAt(e.getX(i), e.getY(i));
                    if (k != old && k != null && k.code >= 0 && modIndex(k.code) < 0) {
                        if (old != null) release(old);
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
                Key k = pointerKey[id];
                pointerKey[id] = null;
                if (k != null) release(k);
                break;
            }
            default:
                break;
        }
        invalidate();
        return true;
    }
}
