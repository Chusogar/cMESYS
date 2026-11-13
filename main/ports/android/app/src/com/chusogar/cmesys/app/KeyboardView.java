package com.chusogar.cmesys.app;

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

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** The machine's on-screen keyboard, the same as dsp-cpp's (its
 *  VirtualKeyboard and the C64's own): a picture of the machine's keyboard
 *  (assets/keyboard.png, or the QL's photo with its red backdrop keyed out)
 *  with the key tiles of assets/keyboard.txt (tools/import_dsp_keyboards.py,
 *  gen_msx_vkb.py, gen_lynx_vkb.py), a key that is down sinking into the
 *  picture. On top of dsp-cpp's: several fingers at once, modifiers that
 *  stay down for the next key when tapped (twice: locked until tapped
 *  again), and the app's MENÚ and ▼ (hide) buttons drawn on a free part of
 *  the picture. */
final class KeyboardView extends View {
    interface Listener {
        void onMachineKey(int code, boolean down);
        void onKeyboardAction(int action);   // ACTION_*
    }

    static final int ACTION_MENU = -2, ACTION_HIDE = -3;
    static final int F_MODIFIER = 1, F_LOCK = 2, F_RESTORE = 4, F_SHIFT_LOCK = 8;
    private static final int SLACK = 4;   // the grooves between tiles count as the nearest key

    private static final class Key {
        int code, flags;
        int x1, y1, x2, y2;
        int[] front;        // C64: front face rect and PETSCII codes (C=, SHIFT)
        int fingers;
        boolean locked;     // a lock key (per tile)
    }

    private final List<Key> keys = new ArrayList<>();
    private final Key[] pointerKey = new Key[16];
    private final Paint pic = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint sunk = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint locked = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint wellPaint = new Paint();
    private final Paint button = new Paint(Paint.ANTI_ALIAS_FLAG), buttonEdge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect src = new Rect();
    private final RectF dst = new RectF();
    private final float density;
    private Bitmap photo;
    private int picW = 1, picH = 1, travel;
    private boolean hasWell;
    private float scale = 1, left;
    private Listener listener;
    private boolean haptic = true;
    // Sticky modifiers by key code: 1 = for the next key, 2 = locked
    private final Map<Integer, Integer> sticky = new HashMap<>();
    private final Map<Integer, Long> lastTap = new HashMap<>();
    private final Map<Integer, Boolean> modsSent = new HashMap<>();

    KeyboardView(Context context, Machine machine, byte[] glyphRom) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        // A key that is down gets darker (13/16, as dsp-cpp); a locked one also redder
        sunk.setColorFilter(new LightingColorFilter(0xffd0d0d0, 0));
        locked.setColorFilter(new LightingColorFilter(0xffe08070, 0));
        button.setColor(Color.argb(170, 52, 52, 58));
        buttonEdge.setColor(Color.argb(200, 120, 120, 128));
        buttonEdge.setStyle(Paint.Style.STROKE);
        buttonEdge.setStrokeWidth(density);
        label.setColor(Color.rgb(225, 225, 225));
        label.setTextAlign(Paint.Align.CENTER);
        label.setFakeBoldText(true);
        load(context, glyphRom);
    }

    // --- the picture and the keys --------------------------------------------------
    private void load(Context context, byte[] glyphRom) {
        String file = "keyboard.png";
        int cropX = 0, cropY = 0;
        boolean keyRed = false;
        try (InputStream in = context.getAssets().open("keyboard.txt")) {
            BufferedReader r = new BufferedReader(new InputStreamReader(in, "UTF-8"));
            String line;
            while ((line = r.readLine()) != null) {
                int hash = line.indexOf('#');
                if (hash >= 0) line = line.substring(0, hash);
                String[] f = line.trim().split("\\s+");
                if (f.length < 2) continue;
                switch (f[0]) {
                    case "picture":
                        file = f[1];
                        picW = Integer.parseInt(f[2]);
                        picH = Integer.parseInt(f[3]);
                        break;
                    case "crop":
                        cropX = Integer.parseInt(f[1]);
                        cropY = Integer.parseInt(f[2]);
                        keyRed = "1".equals(f[3]);
                        break;
                    case "travel":
                        travel = Integer.parseInt(f[1]);
                        break;
                    case "well":
                        wellPaint.setColor(0xff000000 | Integer.parseInt(f[1], 16));
                        hasWell = true;
                        break;
                    case "button":
                    case "key": {
                        Key k = new Key();
                        int i = 1;
                        if (f[0].equals("button")) {
                            k.code = "menu".equals(f[1]) ? ACTION_MENU : ACTION_HIDE;
                            i = 2;
                        } else {
                            k.code = Integer.parseInt(f[1]);
                            k.flags = Integer.parseInt(f[2]);
                            i = 3;
                        }
                        k.x1 = Integer.parseInt(f[i]);
                        k.y1 = Integer.parseInt(f[i + 1]);
                        k.x2 = Integer.parseInt(f[i + 2]);
                        k.y2 = Integer.parseInt(f[i + 3]);
                        if (f.length >= i + 10) {
                            k.front = new int[6];
                            for (int j = 0; j < 6; j++) k.front[j] = Integer.parseInt(f[i + 4 + j]);
                        }
                        keys.add(k);
                        break;
                    }
                    default:
                        break;
                }
            }
        } catch (IOException | RuntimeException e) {
            // no keyboard: an empty picture
        }
        if (travel <= 0) travel = picH / 90 + 2;
        photo = loadPicture(context, file, cropX, cropY, keyRed);
        if (glyphRom != null) drawGlyphs(glyphRom);
    }

    /** The picture; a photo is cropped and its red backdrop keyed out to
     *  transparent (the same test as dsp-cpp: how much redder than green
     *  and blue). */
    private Bitmap loadPicture(Context context, String file, int cropX, int cropY, boolean keyRed) {
        Bitmap full = null;
        try (InputStream in = context.getAssets().open(file)) {
            BitmapFactory.Options o = new BitmapFactory.Options();
            o.inScaled = false;
            o.inPreferredConfig = Bitmap.Config.ARGB_8888;
            full = BitmapFactory.decodeStream(in, null, o);
        } catch (IOException e) {
            full = null;
        }
        int[] px = new int[picW * picH];
        if (full != null && full.getWidth() >= cropX + picW && full.getHeight() >= cropY + picH) {
            full.getPixels(px, 0, picW, cropX, cropY, picW, picH);
            full.recycle();
            if (keyRed) {
                for (int i = 0; i < px.length; i++) {
                    int p = px[i], r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
                    int excess = r - Math.max(g, b);
                    if (excess > 60) {
                        int a = Math.max(0, 255 - (excess - 60) * 255 / 60);
                        r = Math.max(g, b);
                        px[i] = a << 24 | r << 16 | g << 8 | b;
                    }
                }
            }
        } else {
            java.util.Arrays.fill(px, 0xff404040);
        }
        Bitmap bm = Bitmap.createBitmap(picW, picH, Bitmap.Config.ARGB_8888);
        bm.setPixels(px, 0, picW, 0, 0, picW, picH);
        return bm;
    }

    /** The C64's PETSCII graphics on the front of the keys, from the
     *  character ROM in use (C= graphic on the left, SHIFT graphic on the
     *  right), anti-aliased down to the height of the front skirt: dsp-cpp's
     *  C64::vkb_build(). */
    private void drawGlyphs(byte[] rom) {
        int[] px = new int[picW * picH];
        photo.getPixels(px, 0, picW, 0, 0, picW, picH);
        for (Key k : keys) {
            if (k.front == null) continue;
            int fx1 = k.front[0], fy1 = k.front[1], fx2 = k.front[2], fy2 = k.front[3];
            int fh = fy2 - fy1, size = Math.min(fh - 2, 13);
            if (size < 5) continue;
            int y0 = fy1 + (fh - size) / 2;
            int[] codes = {k.front[4], k.front[5]};
            float[] centres = {0.30f, 0.70f};
            for (int g = 0; g < 2; g++) {
                if (codes[g] == 255 || codes[g] * 8 + 8 > rom.length) continue;
                int x0 = fx1 + (int) ((fx2 - fx1) * centres[g]) - size / 2;
                for (int py = 0; py < size; py++) {
                    for (int pxx = 0; pxx < size; pxx++) {
                        int on = 0;
                        for (int sy = 0; sy < 4; sy++) {
                            for (int sx = 0; sx < 4; sx++) {
                                int gx = ((pxx * 4 + sx) * 8) / (size * 4);
                                int gy = ((py * 4 + sy) * 8) / (size * 4);
                                if ((rom[codes[g] * 8 + gy] & (0x80 >> gx)) != 0) on++;
                            }
                        }
                        if (on == 0) continue;
                        int a = on * 235 / 16;
                        int idx = (y0 + py) * picW + x0 + pxx;
                        if (idx < 0 || idx >= px.length) continue;
                        int p = px[idx];
                        int r = (((p >> 16) & 0xff) * (255 - a) + 230 * a) / 255;
                        int gg = (((p >> 8) & 0xff) * (255 - a) + 224 * a) / 255;
                        int b = ((p & 0xff) * (255 - a) + 208 * a) / 255;
                        px[idx] = 0xff000000 | r << 16 | gg << 8 | b;
                    }
                }
            }
        }
        photo.setPixels(px, 0, picW, 0, 0, picW, picH);
    }

    void setListener(Listener l) { listener = l; }

    void setHaptic(boolean h) { haptic = h; }

    // --- size and drawing --------------------------------------------------------------
    @Override
    protected void onMeasure(int wSpec, int hSpec) {
        int w = MeasureSpec.getSize(wSpec);
        // The whole width, but no more than about half the screen's height
        // (landscape): then narrower, in the middle
        int h = w * picH / picW;
        int screenH = getResources().getDisplayMetrics().heightPixels;
        h = Math.min(h, Math.max((int) (screenH * 0.48f), (int) (150 * density)));
        setMeasuredDimension(w, h);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        scale = Math.min(w / (float) picW, h / (float) picH);
        left = (w - picW * scale) / 2;
        label.setTextSize(30 * scale * picW / 1300f);
    }

    private void map(int x1, int y1, int x2, int y2, RectF out) {
        out.set(left + x1 * scale, y1 * scale, left + x2 * scale, y2 * scale);
    }

    private int stickyOf(int code) {
        Integer s = sticky.get(code);
        return s != null ? s : 0;
    }

    /** 0 up, 1 down, 2 locked. A key is down when held by a finger (or the
     *  same key elsewhere on the picture: the QL's two-piece ENTER, the two
     *  SHIFTs), latched or locked. */
    private int state(Key k) {
        if ((k.flags & (F_LOCK | F_SHIFT_LOCK)) != 0) return k.locked ? 2 : 0;
        if ((k.flags & F_MODIFIER) != 0) {
            int s = stickyOf(k.code);
            if (s > 0) return s;
        }
        for (Key o : keys) {
            if (o.code == k.code && o.fingers > 0 && (o.flags & (F_LOCK | F_SHIFT_LOCK)) == 0) return 1;
        }
        return 0;
    }

    @Override
    protected void onDraw(Canvas c) {
        c.drawColor(Color.argb(200, 8, 8, 12));
        map(0, 0, picW, picH, dst);
        c.drawBitmap(photo, null, dst, pic);
        for (Key k : keys) {
            if (k.code < 0) {
                map(k.x1, k.y1, k.x2, k.y2, dst);
                float r = 8 * scale * picW / 1300f;
                if (k.fingers > 0) dst.offset(0, travel * scale);
                c.drawRoundRect(dst, r, r, button);
                c.drawRoundRect(dst, r, r, buttonEdge);
                c.drawText(k.code == ACTION_MENU ? "MENÚ" : "▼", dst.centerX(),
                        dst.centerY() - (label.descent() + label.ascent()) / 2, label);
                continue;
            }
            int s = state(k);
            if (s == 0) continue;
            Paint p = s == 2 ? locked : sunk;
            if (hasWell) {
                // C64: the cap moves down into its well; the band it uncovers is the well
                map(k.x1, k.y1, k.x2, Math.min(k.y2, k.y1 + travel), dst);
                c.drawRect(dst, wellPaint);
                src.set(k.x1, k.y1, k.x2, k.y2 - travel);
                map(k.x1, k.y1 + travel, k.x2, k.y2, dst);
            } else {
                // The cap moves down: each row shows the row `travel` above it
                src.set(k.x1, Math.max(0, k.y1 - travel), k.x2, k.y2 - travel);
                map(k.x1, k.y2 - (src.bottom - src.top), k.x2, k.y2, dst);
            }
            c.drawBitmap(photo, src, dst, p);
        }
    }

    // --- touch -----------------------------------------------------------------------
    private Key keyAt(float vx, float vy) {
        float x = (vx - left) / scale, y = vy / scale;
        Key best = null;
        float bestD = SLACK + 1;
        for (Key k : keys) {
            float dx = Math.max(Math.max(k.x1 - x, x - k.x2), 0);
            float dy = Math.max(Math.max(k.y1 - y, y - k.y2), 0);
            float d = Math.max(dx, dy);
            if (d < bestD) {       // (the first one wins a tie: the L-shaped keys come last)
                bestD = d;
                best = k;
            }
        }
        return bestD <= SLACK ? best : null;
    }

    private void send(int code, boolean down) {
        if (listener != null) listener.onMachineKey(code, down);
    }

    private void press(Key k) {
        k.fingers++;
        if (k.fingers > 1) return;
        if (haptic) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        if (k.code < 0) return;          // MENÚ / ▼: on release
        if ((k.flags & (F_LOCK | F_SHIFT_LOCK)) != 0) {
            k.locked = !k.locked;
            send(k.code, k.locked);
            return;
        }
        if ((k.flags & F_MODIFIER) != 0) {
            // tap: for the next key; tap again soon: locked; tap when locked: off
            int s = stickyOf(k.code);
            long now = System.currentTimeMillis();
            Long last = lastTap.get(k.code);
            if (s == 2) s = 0;
            else if (s == 1 && last != null && now - last < 400) s = 2;
            else if (s == 1) s = 0;
            else s = 1;
            sticky.put(k.code, s);
            lastTap.put(k.code, now);
            applyMods();
            return;
        }
        send(k.code, true);
    }

    private void applyMods() {
        for (Map.Entry<Integer, Integer> e : sticky.entrySet()) {
            boolean down = e.getValue() > 0;
            Boolean was = modsSent.get(e.getKey());
            if (was == null || was != down) {
                modsSent.put(e.getKey(), down);
                send(e.getKey(), down);
            }
        }
    }

    private void release(Key k, boolean act) {
        if (k.fingers == 0) return;
        k.fingers--;
        if (k.fingers > 0 || (k.flags & (F_MODIFIER | F_LOCK | F_SHIFT_LOCK)) != 0) return;
        if (k.code < 0) {
            if (act && listener != null) listener.onKeyboardAction(k.code);
            return;
        }
        send(k.code, false);
        // a one-shot modifier ends with the key it was for (after a frame or
        // two, so that the machine sees them together)
        boolean changed = false;
        for (Map.Entry<Integer, Integer> e : sticky.entrySet()) {
            if (e.getValue() == 1) {
                e.setValue(0);
                changed = true;
            }
        }
        if (changed) postDelayed(new Runnable() {
            @Override public void run() { applyMods(); invalidate(); }
        }, 60);
    }

    /** Lets go of everything (the keyboard is hidden, the app paused). */
    void releaseAll() {
        for (int i = 0; i < pointerKey.length; i++) {
            Key k = pointerKey[i];
            pointerKey[i] = null;
            if (k != null) {
                k.fingers = 1;
                release(k, false);
            }
        }
        for (Key k : keys) {
            k.fingers = 0;
            if (k.locked) {
                k.locked = false;
                send(k.code, false);
            }
        }
        for (Map.Entry<Integer, Integer> e : sticky.entrySet()) e.setValue(0);
        applyMods();
        invalidate();
    }

    private static boolean slides(Key k) {
        return k != null && k.code >= 0 && (k.flags & (F_MODIFIER | F_LOCK | F_SHIFT_LOCK)) == 0;
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
                // a finger sliding to another key (not for the modifiers and buttons)
                for (int p = 0; p < e.getPointerCount(); p++) {
                    int id = e.getPointerId(p);
                    if (id >= pointerKey.length) continue;
                    Key old = pointerKey[id];
                    if (old != null && !slides(old)) continue;
                    Key k = keyAt(e.getX(p), e.getY(p));
                    if (k != old && slides(k) && (old == null || k.code != old.code)) {
                        if (old != null) release(old, false);
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
                if (k != null) release(k, true);
                break;
            }
            default:
                break;
        }
        invalidate();
        return true;
    }
}
